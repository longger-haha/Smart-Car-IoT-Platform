"""
mqtt_client.py — paho-mqtt 长连接客户端

功能:
    1. 连接 Mosquitto Broker（支持用户名/密码认证）
    2. 订阅 sensor/# 主题，接收设备遥测数据
    3. 对每条消息做设备白名单检查
    4. AES 解密 + HMAC 签名验证（Phase 2 基础版先做设备白名单，详细加解密在 Phase 3 完善）
    5. 解密成功后将遥测数据写入 telemetry_points 表，并更新设备心跳
    6. 鉴权失败写入 security_audit_logs(event_type='auth_fail')

启动方式（由 app.py 调用）:
    from src.utils.mqtt_client import start_mqtt, publish_command
    start_mqtt(app)                              # 订阅线程（后台守护进程）
    publish_command(device_id, payload_dict)    # 发布下行指令（由 vehicle 路由调用）
"""

import json
import threading
import logging
import time

import paho.mqtt.client as mqtt

logger = logging.getLogger(__name__)

# 全局 mqtt 客户端实例（供 publish_command 调用）
_client: mqtt.Client = None
_flask_app = None


# ── 回调函数 ──────────────────────────────────────────────────────────────────

def _on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        logger.info('[MQTT] Connected to broker successfully')
        client.subscribe('sensor/#', qos=1)
        logger.info('[MQTT] Subscribed to sensor/#')
    else:
        logger.error(f'[MQTT] Connection failed, rc={rc}')


def _on_disconnect(client, userdata, disconnect_flags, rc, properties=None):
    if rc != 0:
        logger.warning(f'[MQTT] Unexpected disconnect, rc={rc}. Will auto-reconnect.')


def _on_message(client, userdata, msg):
    """
    处理 sensor/<device_id> 上报的遥测消息。

    预期 payload 格式（AES 加密前的明文 JSON）:
        {
            "device_id":   "aa:bb:cc:dd:ee:ff",
            "timestamp":   1714567890,
            "signature":   "<hmac_sha256_hex>",
            "latitude":    39.9042,
            "longitude":   116.4074,
            "temperature": 26.5,
            "humidity":    65.0,
            "ultrasonic_cm": 120.3,
            "speed_pwm":   200
        }

    消息格式: AES 加密后的 base64 字符串（"<iv_b64>:<ct_b64>"）
    """
    topic   = msg.topic
    payload = msg.payload

    # 从 topic 中提取 device_id（格式: sensor/<device_id>）
    parts     = topic.split('/', 1)
    device_id = parts[1] if len(parts) > 1 else 'unknown'

    with _flask_app.app_context():
        _process_message(device_id, payload)


def _process_message(device_id: str, raw_payload: bytes):
    """在 Flask 应用上下文中处理消息，写入数据库"""
    from app import db
    from src.models.device import Device
    from src.models.telemetry import TelemetryPoint
    from src.models.audit_log import SecurityAuditLog
    from src.utils.crypto_tool import aes_decrypt, hmac_verify
    from flask import current_app
    from datetime import datetime

    # 记录原始密文（供审计展示）
    raw_ciphertext = raw_payload.decode('utf-8', errors='replace')

    try:
        # ── 1. AES 解密 ──────────────────────────────────────────────
        aes_key = current_app.config.get('AES_KEY', b'SmartRover2026!!')
        try:
            plaintext = aes_decrypt(aes_key, raw_ciphertext)
        except ValueError:
            # 解密失败：可能是非法设备发的乱数据
            logger.warning(f'[MQTT] AES decrypt failed for device_id={device_id!r}')
            SecurityAuditLog.record(
                event_type='auth_fail',
                target_device_id=device_id,
                detail='AES decryption failed — possible unauthorized device or tampered data',
            )
            db.session.commit()
            return

        # ── 2. JSON 解析 ─────────────────────────────────────────────
        try:
            data = json.loads(plaintext)
        except json.JSONDecodeError:
            logger.warning(f'[MQTT] JSON parse failed for device_id={device_id!r}')
            SecurityAuditLog.record(
                event_type='auth_fail',
                target_device_id=device_id,
                detail='JSON parse failed after decryption',
            )
            db.session.commit()
            return

        # ── 3. 设备白名单检查 ─────────────────────────────────────────
        msg_device_id = data.get('device_id', device_id)
        device = Device.query.filter_by(device_id=msg_device_id).first()

        if not device:
            logger.warning(f'[MQTT] Unregistered device: {msg_device_id!r}')
            SecurityAuditLog.record(
                event_type='auth_fail',
                target_device_id=msg_device_id,
                detail=f'Device {msg_device_id!r} not in whitelist — message rejected',
            )
            db.session.commit()
            return

        # ── 4. HMAC 签名验证 ──────────────────────────────────────────
        signature = data.pop('signature', None)
        if signature:
            # 将去掉 signature 的 payload 重新序列化来做校验
            msg_body = json.dumps({k: v for k, v in data.items()}, sort_keys=True)
            if not hmac_verify(device.device_secret, msg_body, signature):
                logger.warning(f'[MQTT] HMAC verify failed for device {msg_device_id!r}')
                SecurityAuditLog.record(
                    event_type='sig_invalid',
                    target_device_id=msg_device_id,
                    detail='HMAC-SHA256 signature mismatch',
                )
                db.session.commit()
                return
        else:
            logger.debug(f'[MQTT] No signature field, skipping HMAC check for {msg_device_id!r}')

        # ── 5. 写入遥测数据 ───────────────────────────────────────────
        point = TelemetryPoint(
            device_id      = msg_device_id,
            latitude       = data.get('latitude'),
            longitude      = data.get('longitude'),
            temperature    = data.get('temperature'),
            humidity       = data.get('humidity'),
            ultrasonic_cm  = data.get('ultrasonic_cm'),
            speed_pwm      = data.get('speed_pwm'),
            raw_ciphertext = raw_ciphertext,
            recorded_at    = datetime.utcnow(),
        )
        db.session.add(point)

        # ── 6. 更新设备心跳 / 在线状态 ───────────────────────────────
        device.touch()
        db.session.commit()

        # ── 7. 碰撞预防：超声波距离 < 50cm 时下发紧急停车指令 ────────
        SAFE_DISTANCE_CM = 50
        ultrasonic_val = data.get('ultrasonic_cm')
        if ultrasonic_val is not None and ultrasonic_val < SAFE_DISTANCE_CM:
            logger.warning(
                f'[MQTT] ⚠️ COLLISION WARNING: device={msg_device_id!r} '
                f'ultrasonic={ultrasonic_val}cm < {SAFE_DISTANCE_CM}cm — '
                f'sending emergency STOP command'
            )
            emergency_payload = {
                'command':   'stop',
                'device_id': msg_device_id,
                'timestamp': int(time.time()),
                'reason':    'collision_prevention',
            }
            publish_command(msg_device_id, emergency_payload)

        logger.debug(f'[MQTT] Telemetry saved for device {msg_device_id!r}')

    except Exception as exc:
        logger.error(f'[MQTT] Unexpected error processing message: {exc}', exc_info=True)
        try:
            db.session.rollback()
        except Exception:
            pass


# ── 启动函数 ──────────────────────────────────────────────────────────────────

def start_mqtt(app):
    """
    在后台守护线程中启动 MQTT 长连接，订阅 sensor/#。

    Args:
        app: Flask 应用实例
    """
    global _client, _flask_app
    _flask_app = app

    broker_host = app.config.get('MQTT_BROKER_HOST', 'localhost')
    broker_port = app.config.get('MQTT_BROKER_PORT', 1883)
    username    = app.config.get('MQTT_USERNAME', '')
    password    = app.config.get('MQTT_PASSWORD', '')

    _client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2,
                          client_id='smartrover-backend')

    if username:
        _client.username_pw_set(username, password)

    _client.on_connect    = _on_connect
    _client.on_disconnect = _on_disconnect
    _client.on_message    = _on_message

    def _run():
        while True:
            try:
                logger.info(f'[MQTT] Connecting to {broker_host}:{broker_port} ...')
                _client.connect(broker_host, broker_port, keepalive=60)
                _client.loop_forever()
            except Exception as e:
                logger.error(f'[MQTT] Connection error: {e}. Retrying in 5s ...')
                time.sleep(5)

    thread = threading.Thread(target=_run, daemon=True, name='mqtt-listener')
    thread.start()
    logger.info('[MQTT] Background listener thread started')


# ── 下行指令发布 ───────────────────────────────────────────────────────────────

def publish_command(device_id: str, payload: dict) -> bool:
    """
    向设备发布下行控制指令（明文 JSON，无需加密，因为 MQTT Broker 已做 TLS）。

    Args:
        device_id: 目标设备 ID
        payload:   指令字典，如 {'command': 'forward', 'timestamp': 1234567890}

    Returns:
        True 表示发布成功，False 表示失败
    """
    global _client
    if _client is None:
        logger.error('[MQTT] Client not initialized, call start_mqtt() first')
        return False

    topic   = f'cmd/{device_id}'
    message = json.dumps(payload, ensure_ascii=False)
    result  = _client.publish(topic, message, qos=1)

    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        logger.info(f'[MQTT] Published to {topic}: {message}')
        return True
    else:
        logger.error(f'[MQTT] Publish to {topic} failed, rc={result.rc}')
        return False

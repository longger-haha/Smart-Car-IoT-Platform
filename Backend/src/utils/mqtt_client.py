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
        client.subscribe('nav/#', qos=1)
        client.subscribe('heartbeat/#', qos=1)
        logger.info('[MQTT] Subscribed to sensor/#, nav/#, heartbeat/#')
    else:
        logger.error(f'[MQTT] Connection failed, rc={rc}')


def _on_disconnect(client, userdata, disconnect_flags, rc, properties=None):
    if rc != 0:
        logger.warning(f'[MQTT] Unexpected disconnect, rc={rc}. Will auto-reconnect.')


def _on_message(client, userdata, msg):
    """
    处理 MQTT 消息:
      - sensor/<device_id> → 遥测数据（加密）
      - nav/<device_id>   → 导航事件
      - heartbeat/<device_id> → 心跳保活
    """
    topic   = msg.topic
    payload = msg.payload

    parts     = topic.split('/', 1)
    topic_type = parts[0] if len(parts) > 1 else 'unknown'
    device_id = parts[1] if len(parts) > 1 else 'unknown'

    if _flask_app is None:
        logger.error("[MQTT] _flask_app is None, cannot process message")
        return

    with _flask_app.app_context():
        if topic_type == 'sensor':
            _process_telemetry(device_id, payload)
        elif topic_type == 'nav':
            _process_nav_event(device_id, payload)
        elif topic_type == 'heartbeat':
            _process_heartbeat(device_id, payload)
        else:
            logger.warning(f'[MQTT] Unknown topic: {topic}')


def _get_device_owner_id(device_id: str):
    """根据 device_id 反查设备所属用户 ID，失败返回 None。"""
    try:
        from src.models.device import Device
        d = Device.query.filter_by(device_id=device_id).first()
        return d.user_id if d else None
    except Exception:
        return None


def _process_telemetry(device_id: str, raw_payload: bytes):
    """在 Flask 应用上下文中处理消息，写入数据库"""
    from src.extensions import db
    from src.models.device import Device
    from src.models.telemetry import TelemetryPoint
    from src.models.audit_log import SecurityAuditLog
    from src.utils.crypto_tool import aes_decrypt, hmac_verify, xor_decrypt, xor_verify_checksum
    from flask import current_app
    from datetime import datetime

    # 记录原始密文（供审计展示）
    raw_ciphertext = raw_payload.decode('utf-8', errors='replace')

    try:
        # ── 1. 解密 (AES 优先, 失败后尝试 XOR 轻量解密) ─────────────
        aes_key = current_app.config.get('AES_KEY', b'SmartRover2026!!')
        xor_key = current_app.config.get('XOR_KEY', aes_key)  # 默认与 AES_KEY 相同
        plaintext = None
        encrypt_type = 'unknown'

        try:
            plaintext = aes_decrypt(aes_key, raw_ciphertext)
            encrypt_type = 'aes'
        except ValueError:
            # AES 解密失败, 尝试 XOR 解密 (UNO 设备)
            try:
                plaintext = xor_decrypt(xor_key, raw_ciphertext)
                encrypt_type = 'xor'
                logger.info(f'[MQTT] XOR decrypt OK for device_id={device_id!r}')
            except ValueError:
                # 两种解密都失败
                logger.warning(f'[MQTT] Both AES and XOR decrypt failed for device_id={device_id!r}')
                SecurityAuditLog.record(
                    event_type='auth_fail',
                    target_device_id=device_id,
                    detail='Decryption failed (neither AES nor XOR) — possible unauthorized device',
                    user_id=_get_device_owner_id(device_id),
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
                user_id=_get_device_owner_id(device_id),
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
                user_id=_get_device_owner_id(msg_device_id),
            )
            db.session.commit()
            return

        # ── 4. 签名验证 (AES 设备用 HMAC, XOR 设备用简易校验和) ────────
        signature = data.pop('signature', None)
        if signature:
            if encrypt_type == 'aes':
                # AES 设备: HMAC-SHA256 签名验证
                msg_body = json.dumps({k: v for k, v in data.items()}, sort_keys=True)
                if not hmac_verify(device.device_secret, msg_body, signature):
                    logger.warning(f'[MQTT] HMAC verify failed for device {msg_device_id!r}')
                    SecurityAuditLog.record(
                        event_type='sig_invalid',
                        target_device_id=msg_device_id,
                        detail='HMAC-SHA256 signature mismatch',
                        user_id=device.user_id,
                    )
                    db.session.commit()
                    return
            else:
                # XOR 设备 (UNO): 简易 XOR 校验和验证
                msg_body = json.dumps({k: v for k, v in data.items()}, sort_keys=True)
                if not xor_verify_checksum(device.device_secret, msg_body, signature):
                    logger.warning(f'[MQTT] XOR checksum verify failed for device {msg_device_id!r}')
                    SecurityAuditLog.record(
                        event_type='sig_invalid',
                        target_device_id=msg_device_id,
                        detail='XOR checksum mismatch',
                        user_id=device.user_id,
                    )
                    db.session.commit()
                    return
        else:
            logger.debug(f'[MQTT] No signature field, skipping check for {msg_device_id!r}')

        # ── 5. 写入遥测数据 ───────────────────────────────────────────
        point = TelemetryPoint(
            device_id      = msg_device_id,
            latitude       = data.get('latitude'),
            longitude      = data.get('longitude'),
            temperature    = data.get('temperature'),
            humidity       = data.get('humidity'),
            ultrasonic_cm  = data.get('ultrasonic_cm'),
            ir_obstacle    = data.get('ir_obstacle'),
            imu_heading    = data.get('imu_heading'),
            imu_gyro_z     = data.get('imu_gyro_z'),
            speed_pwm      = data.get('speed_pwm'),
            altitude       = data.get('altitude'),
            speed_kmh      = data.get('speed_kmh'),
            satellites     = data.get('satellites'),
            raw_ciphertext = raw_ciphertext,
            recorded_at    = datetime.now(),
        )
        db.session.add(point)

        # ── 6. 更新设备心跳 / 在线状态 ───────────────────────────────
        device.touch()
        db.session.commit()

        # ── 7. 碰撞预防：超声波或红外检测到障碍 ──────────────────────
        SAFE_DISTANCE_CM = 50
        ultrasonic_val = data.get('ultrasonic_cm')
        ir_val = data.get('ir_obstacle')
        collision_detected = (
            (ultrasonic_val is not None and ultrasonic_val < SAFE_DISTANCE_CM) or
            (ir_val is True)
        )
        if collision_detected:
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


def _process_nav_event(device_id: str, raw_payload: bytes):
    """处理导航事件消息，写入 navigation_events 表"""
    from src.extensions import db
    from src.models.navigation_event import NavigationEvent
    from flask import current_app
    from datetime import datetime

    try:
        data = json.loads(raw_payload)
    except (json.JSONDecodeError, UnicodeDecodeError):
        logger.warning(f'[NAV] JSON parse failed for device={device_id!r}')
        return

    event = NavigationEvent(
        device_id   = data.get('device_id', device_id),
        event_type  = data.get('event_type', 'UNKNOWN')[:32],
        detail      = data.get('detail', ''),
        lat         = data.get('lat'),
        lng         = data.get('lng'),
        wp_index    = data.get('wp_index'),
        wp_total    = data.get('wp_total'),
        state       = data.get('state'),
        occurred_at = datetime.now(),
    )
    db.session.add(event)

    try:
        db.session.commit()
        logger.info(
            f'[NAV] Event recorded: device={device_id!r} type={event.event_type!r} '
            f'detail={event.detail!r}'
        )
    except Exception as exc:
        db.session.rollback()
        logger.warning(f'[NAV] Failed to write nav event: {exc}')

    from src.routes.vehicle import _nav_status_cache
    if device_id in _nav_status_cache:
        _nav_status_cache[device_id]['state'] = data.get('state', 'unknown')
        wp_idx = data.get('wp_index')
        if wp_idx is not None:
            _nav_status_cache[device_id]['wp_index'] = wp_idx


def _process_heartbeat(device_id: str, raw_payload: bytes):
    """处理心跳消息，更新设备在线状态"""
    from src.extensions import db
    from src.models.device import Device
    from flask import current_app

    try:
        data = json.loads(raw_payload)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return

    device = Device.query.filter_by(device_id=device_id).first()
    if device:
        device.touch()
        try:
            db.session.commit()
        except Exception as exc:
            db.session.rollback()
            logger.warning(f'[HB] Heartbeat update failed for {device_id!r}: {exc}')

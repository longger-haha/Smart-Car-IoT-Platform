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
import requests
from datetime import datetime, timedelta, timezone

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

    # 调试: 打印实际收到的 topic 和 payload 前 80 字节
    logger.info(f'[MQTT] RECV topic={topic!r} len={len(payload)} '
                f'preview={payload[:80]}')

    parts      = topic.split('/')
    topic_type = parts[0] if len(parts) > 1 else 'unknown'
    device_id  = parts[1] if len(parts) > 1 else 'unknown'
    sub_type   = parts[2] if len(parts) > 2 else None

    if _flask_app is None:
        logger.error("[MQTT] _flask_app is None, cannot process message")
        return

    with _flask_app.app_context():
        if topic_type == 'sensor':
            _process_telemetry(device_id, payload, sub_type)
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


def _geolocate_wifi_aps(wifi_aps: list) -> dict:
    """
    通过 WiFi AP 列表反查坐标。
    优先使用 Mylnikov API (免费无需Key), 失败则尝试 Google Geolocation API。

    Args:
        wifi_aps: [{"mac":"AA:BB:CC:DD:EE:FF","rssi":-50,"ch":6}, ...]

    Returns:
        {"latitude": float, "longitude": float, "accuracy": float} 或空 dict
    """
    if not wifi_aps:
        return {}

    # ── 方案1: Mylnikov API (免费, 无需注册) ──────────────────────
    try:
        # 取信号最强的 AP 的 MAC 地址查询
        best_ap = max(wifi_aps, key=lambda a: a.get('rssi', -100))
        mac = best_ap.get('mac', '').replace(':', '')
        if len(mac) == 12:
            resp = requests.get(
                f'https://api.mylnikov.org/geolocation/wifi?v=1.1&bssid={mac}',
                timeout=5
            )
            if resp.status_code == 200:
                result = resp.json()
                if result.get('result') == 200:  # Mylnikov 成功码
                    data = result.get('data', {})
                    lat = data.get('lat')
                    lng = data.get('lon')
                    if lat and lng:
                        logger.info(f'[GEO] Mylnikov OK: lat={lat}, lng={lng}')
                        return {
                            'latitude': float(lat),
                            'longitude': float(lng),
                            'accuracy': float(data.get('range', 50)),
                        }
    except Exception as e:
        logger.debug(f'[GEO] Mylnikov error: {e}')

    # ── 方案2: Google Geolocation API (需Key) ─────────────────────
    try:
        from flask import current_app
        api_key = current_app.config.get('GOOGLE_GEOLOCATION_API_KEY', '')
        if api_key:
            wifi_access_points = []
            for ap in wifi_aps[:6]:
                entry = {"macAddress": ap.get('mac', '')}
                if 'rssi' in ap:
                    entry["signalStrength"] = ap['rssi']
                if 'ch' in ap:
                    entry["channel"] = ap['ch']
                wifi_access_points.append(entry)

            resp = requests.post(
                f'https://www.googleapis.com/geolocation/v1/geolocate?key={api_key}',
                json={"wifiAccessPoints": wifi_access_points},
                timeout=5
            )
            if resp.status_code == 200:
                result = resp.json()
                location = result.get('location', {})
                return {
                    'latitude': location.get('lat'),
                    'longitude': location.get('lng'),
                    'accuracy': result.get('accuracy'),
                }
    except Exception as e:
        logger.debug(f'[GEO] Google error: {e}')

    return {}


def _process_telemetry(device_id: str, raw_payload: bytes, sub_type: str = None):
    """在 Flask 应用上下文中处理消息，写入数据库

    sub_type: None 表示主遥测 (sensor/<id>), 'imu'/'gps' 表示子主题
    """
    from src.extensions import db
    from src.models.device import Device
    from src.models.telemetry import TelemetryPoint
    from src.models.audit_log import SecurityAuditLog
    from src.utils.crypto_tool import aes_decrypt, hmac_verify, xor_decrypt, xor_verify_checksum
    from flask import current_app
    from datetime import datetime

    # 记录原始密文（供审计展示）
    raw_ciphertext = raw_payload.decode('utf-8', errors='replace')
    logger.info(f'[MQTT] raw_payload for {device_id!r}: {raw_ciphertext!r}')

    try:
        # ── 1. 解密 (明文优先, 然后 AES, 最后 XOR) ─────────────
        aes_key = current_app.config.get('AES_KEY', b'SmartRover2026!!')
        xor_key = current_app.config.get('XOR_KEY', aes_key)  # 默认与 AES_KEY 相同
        plaintext = None
        encrypt_type = 'unknown'

        # ESP32 调试模式: PLAIN: 前缀的明文 JSON
        if raw_ciphertext.startswith('PLAIN:'):
            plaintext = raw_ciphertext[6:]
            encrypt_type = 'plain'
            logger.info(f'[MQTT] PLAIN text from device_id={device_id!r}')
        else:
            try:
                # ESP32 设备: AES:base64iv:base64ct 格式
                if raw_ciphertext.startswith('AES:'):
                    plaintext = aes_decrypt(aes_key, raw_ciphertext[4:])
                    encrypt_type = 'aes'
                else:
                    plaintext = aes_decrypt(aes_key, raw_ciphertext)
                    encrypt_type = 'aes'
            except ValueError as aes_err:
                # AES 解密失败, 尝试 XOR 解密 (UNO 设备)
                try:
                    plaintext = xor_decrypt(xor_key, raw_ciphertext)
                    encrypt_type = 'xor'
                    logger.info(f'[MQTT] XOR decrypt OK for device_id={device_id!r}')
                except ValueError as xor_err:
                    # 两种解密都失败
                    logger.warning(
                        f'[MQTT] Both AES and XOR decrypt failed for device_id={device_id!r} '
                        f'aes_err={aes_err!s} xor_err={xor_err!s} '
                        f'raw_len={len(raw_ciphertext)} raw_start={raw_ciphertext[:40]!r}'
                    )
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
        except json.JSONDecodeError as je:
            logger.warning(
                f'[MQTT] JSON parse failed for device_id={device_id!r} '
                f'encrypt={encrypt_type} plaintext_len={len(plaintext)} '
                f'plaintext_preview={plaintext[:200]!r} error={je}'
            )
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

        # ── 4. 签名验证 (AES/PLAIN 设备用 HMAC, XOR 设备用简易校验和) ────────
        signature = data.pop('signature', None)
        if signature:
            if encrypt_type in ('aes', 'plain'):
                # AES/PLAIN 设备: HMAC-SHA256 签名验证
                # 注意: ESP32 签名时用原始 JSON 顺序, 后端 sort_keys 重排后顺序不同
                # 调试阶段: PLAIN 模式跳过签名验证
                if encrypt_type == 'plain':
                    logger.info(f'[MQTT] PLAIN mode - skipping HMAC verify for device {msg_device_id!r}')
                else:
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
                # 固件签名时 JSON 结尾是 "last_field," (无 "signature" 和 "}")
                # 必须用原始顺序的 JSON，不能 sort_keys 重排
                # 注意: re.sub 去掉 signature 时要保留前面的逗号,
                # 因为固件计算签名时 JSON 是 "speed_pwm":150, (有逗号)
                import re
                msg_body = re.sub(r',"signature":"[A-Fa-f0-9]{8}"\}', ',', plaintext)
                logger.info(
                    f'[MQTT] SIG DEBUG device={msg_device_id!r} '
                    f'sig={signature!r} msg_body={msg_body[:200]!r} '
                    f'secret={device.device_secret[:16]!r}...'
                )
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

        # ── 5. IMU/GPS 子主题: 合并到最近的遥测记录 ──────────────────
        if sub_type in ('imu', 'gps'):
            recent = TelemetryPoint.query.filter_by(device_id=msg_device_id)\
                .order_by(TelemetryPoint.recorded_at.desc()).first()
            if recent and (datetime.now(timezone.utc) - recent.recorded_at).total_seconds() < 30:
                if sub_type == 'imu':
                    recent.imu_ax = data.get('imu_ax')
                    recent.imu_ay = data.get('imu_ay')
                    recent.imu_az = data.get('imu_az')
                    recent.imu_gx = data.get('imu_gx')
                    recent.imu_gy = data.get('imu_gy')
                    recent.imu_gz = data.get('imu_gz')
                elif sub_type == 'gps':
                    recent.latitude  = data.get('latitude')
                    recent.longitude = data.get('longitude')
                    recent.altitude  = data.get('altitude')
                    recent.speed_kmh = data.get('speed_kmh')
                    recent.satellites = data.get('satellites')
                device.touch()
                db.session.commit()
                logger.debug(f'[MQTT] Merged {sub_type} into recent telemetry for {msg_device_id!r}')
                return
            # 没有最近的记录可合并, 继续创建新记录

        # ── 6. 写入遥测数据 ───────────────────────────────────────────
        # 兼容新旧遥测格式:
        #   旧: ir_obstacle(bool), imu_heading, imu_gyro_z
        #   新: ir_l/ir_r(int), imu_ax/ay/az, imu_gx/gy/gz, bat_mv, seq
        ir_l = data.get('ir_l')
        ir_r = data.get('ir_r')
        ir_obstacle = data.get('ir_obstacle')
        if ir_l is not None or ir_r is not None:
            ir_obstacle_val = bool(ir_l or ir_r)
        else:
            ir_obstacle_val = ir_obstacle

        # ── 6.5 WiFi 定位 (ESP32 无 GPS 时) ──────────────────────────
        latitude = data.get('latitude')
        longitude = data.get('longitude')
        wifi_aps = data.get('wifi_aps')
        if (latitude is None or longitude is None) and wifi_aps:
            geo = _geolocate_wifi_aps(wifi_aps)
            if geo:
                latitude = geo.get('latitude')
                longitude = geo.get('longitude')
                logger.info(f'[GEO] WiFi geolocation for {msg_device_id!r}: '
                           f'lat={latitude}, lng={longitude}, '
                           f'accuracy={geo.get("accuracy")}m')

        point = TelemetryPoint(
            device_id      = msg_device_id,
            latitude       = latitude,
            longitude      = longitude,
            temperature    = data.get('temperature'),
            humidity       = data.get('humidity'),
            ultrasonic_cm  = data.get('ultrasonic_cm'),
            ir_obstacle    = ir_obstacle_val,
            ir_l           = ir_l,
            ir_r           = ir_r,
            imu_heading    = data.get('imu_heading'),
            imu_gyro_z     = data.get('imu_gyro_z'),
            imu_ax         = data.get('imu_ax'),
            imu_ay         = data.get('imu_ay'),
            imu_az         = data.get('imu_az'),
            imu_gx         = data.get('imu_gx'),
            imu_gy         = data.get('imu_gy'),
            imu_gz         = data.get('imu_gz'),
            speed_pwm      = data.get('speed_pwm'),
            altitude       = data.get('altitude'),
            speed_kmh      = data.get('speed_kmh'),
            satellites     = data.get('satellites'),
            bat_mv         = data.get('bat_mv'),
            seq            = data.get('seq'),
            raw_ciphertext = raw_ciphertext,
            recorded_at    = datetime.now(timezone.utc),
        )
        db.session.add(point)

        # ── 6. 更新设备心跳 / 在线状态 ───────────────────────────────
        device.touch()
        db.session.commit()

        # ── 7. 触发导航决策引擎 ──────────────────────────────────────
        try:
            from src.services.navigation_engine import nav_engine
            nav_engine.on_telemetry(msg_device_id, data)
        except Exception as nav_exc:
            logger.debug(f'[NAV] Engine error for {msg_device_id!r}: {nav_exc}')

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

    # 检查客户端连接状态
    if not _client.is_connected():
        logger.error(f'[MQTT] Client not connected! Cannot publish to cmd/{device_id}')
        return False

    topic   = f'cmd/{device_id}'
    message = json.dumps(payload, ensure_ascii=False)
    logger.info(f'[MQTT] Publishing to {topic}: {message}')
    result  = _client.publish(topic, message, qos=1)

    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        logger.info(f'[MQTT] Published OK to {topic}: {message}')
        return True
    else:
        logger.error(f'[MQTT] Publish to {topic} failed, rc={result.rc}')
        return False


def _process_heartbeat(device_id: str, raw_payload: bytes):
    """处理心跳消息，更新设备在线状态"""
    from src.extensions import db
    from src.models.device import Device
    from src.utils.crypto_tool import xor_decrypt
    from flask import current_app

    raw_str = raw_payload.decode('utf-8', errors='replace')

    # 心跳数据可能是 XOR 加密的 (UNO 设备), 也可能是明文 JSON
    try:
        if raw_str.startswith('XOR:'):
            xor_key = current_app.config.get('XOR_KEY',
                       current_app.config.get('AES_KEY', b'SmartRover2026!!'))
            plaintext = xor_decrypt(xor_key, raw_str)
            data = json.loads(plaintext)
        else:
            data = json.loads(raw_str)
    except (json.JSONDecodeError, UnicodeDecodeError, ValueError):
        return

    # 用 JSON 中的 device_id 或 topic 中的 device_id 查找设备
    hb_device_id = data.get('device_id', device_id)
    device = Device.query.filter_by(device_id=hb_device_id).first()
    if device:
        device.touch()
        try:
            db.session.commit()
        except Exception as exc:
            db.session.rollback()
            logger.warning(f'[HB] Heartbeat update failed for {hb_device_id!r}: {exc}')

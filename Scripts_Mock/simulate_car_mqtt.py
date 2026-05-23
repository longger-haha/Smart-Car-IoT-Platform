#!/usr/bin/env python3
"""
SmartRover 真实小车 MQTT 数据模拟器 v2.0
═══════════════════════════════════════════════════════════════
模拟一辆完整的 SmartRover 小车通过 MQTT 与后端通信的全过程:
  - 遥测数据 (XOR 加密 + 校验和签名)
  - 心跳保活
  - 导航事件 (航点到达、避障等)
  - 接收下行指令 (cmd/<device_id>)
  - 模拟小车沿路线自动巡航

使用方式:
  python simulate_car_mqtt.py                    # 默认配置
  python simulate_car_mqtt.py --device UNO_002   # 指定设备ID
  python simulate_car_mqtt.py --route            # 带自动巡航模拟
  python simulate_car_mqtt.py --attack           # 模拟攻击场景 (非法签名等)

依赖: pip install paho-mqtt
═══════════════════════════════════════════════════════════════
"""

import json
import time
import base64
import random
import math
import threading
import argparse
import logging
import hashlib
import hmac
import os
import sys
from datetime import datetime

# 尝试导入 paho-mqtt
try:
    import paho.mqtt.client as mqtt
except ImportError:
    print("请安装 paho-mqtt: pip install paho-mqtt")
    sys.exit(1)

# ═══════════════════════════════════════════════════════════════
#  配置区 (可通过命令行参数覆盖)
# ═══════════════════════════════════════════════════════════════

# 读取 .env 配置 (如果存在)
def _load_dotenv():
    """简易 .env 解析"""
    env_path = os.path.join(os.path.dirname(__file__), '..', '.env')
    config = {}
    if os.path.exists(env_path):
        with open(env_path, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith('#') and '=' in line:
                    k, v = line.split('=', 1)
                    config[k.strip()] = v.strip()
    return config

_env = _load_dotenv()

MQTT_BROKER_HOST  = _env.get('MQTT_BROKER_HOST', 'localhost')
MQTT_BROKER_PORT  = int(_env.get('MQTT_BROKER_PORT', '1883'))
MQTT_USERNAME     = _env.get('MQTT_USERNAME', '')
MQTT_PASSWORD     = _env.get('MQTT_PASSWORD', '')

DEVICE_ID         = "SMARTROVER_UNO_001"       # 模拟的设备ID
DEVICE_SECRET     = "72d579dd57432ef9c9614b1261a0a5ca3de4e8d0135f961b8c826f576b65f21f"
XOR_KEY           = b"SmartRover2026!!"

TELEMETRY_INTERVAL_S = 5.0                     # 遥测上报间隔
HEARTBEAT_INTERVAL_S  = 8.0                     # 心跳间隔
NAV_EVENT_INTERVAL_S  = 3.0                     # 导航事件间隔 (巡航模式下)

# ═══════════════════════════════════════════════════════════════
#  日志
# ═══════════════════════════════════════════════════════════════

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S',
)
logger = logging.getLogger('SimCar')

# ═══════════════════════════════════════════════════════════════
#  XOR 加密 + 校验和 (与 UNO 固件完全一致)
# ═══════════════════════════════════════════════════════════════

def xor_encrypt_base64(plaintext: str, key: bytes) -> str:
    """
    XOR 加密 + Base64 编码 (完全匹配 UNO 固件的 streamXor 逻辑).
    输出格式: "XOR:" + base64(xor_data)
    """
    plain_bytes = plaintext.encode('utf-8')
    key_len = len(key)
    xored = bytes(plain_bytes[i] ^ key[i % key_len] for i in range(len(plain_bytes)))
    return "XOR:" + base64.b64encode(xored).decode()


def xor_decrypt_base64(token: str, key: bytes) -> str:
    """XOR 解密 (反向操作, 与后端 xor_decrypt 一致)"""
    if not token.startswith("XOR:"):
        raise ValueError("Missing XOR: prefix")
    xored = base64.b64decode(token[4:])
    key_len = len(key)
    plain = bytes(xored[i] ^ key[i % key_len] for i in range(len(xored)))
    return plain.decode('utf-8')


def compute_xor_signature(json_body: str, secret: str) -> str:
    """
    计算 XOR 校验和签名 (完全匹配 UNO 固件 sendTelemetry 逻辑).

    UNO 固件在拼接完 JSON 主体后 (结尾为逗号, 无 "}" 和 signature),
    对每个字节做: sig[i%4] ^= json_byte ^ secret_byte
    """
    sig = [0, 0, 0, 0]
    s_len = len(secret)
    for i, ch in enumerate(json_body[:250]):
        sig[i % 4] ^= ord(ch) ^ ord(secret[i % s_len])
    return ''.join(f'{b:02X}' for b in sig)


# ═══════════════════════════════════════════════════════════════
#  HMAC 签名 (AES 设备用, 此处用于向后端 API 调用)
# ═══════════════════════════════════════════════════════════════

def hmac_sign(secret: str, message: str) -> str:
    return hmac.new(
        secret.encode('utf-8'),
        message.encode('utf-8'),
        hashlib.sha256,
    ).hexdigest()


# ═══════════════════════════════════════════════════════════════
#  传感器模拟器
# ═══════════════════════════════════════════════════════════════

class SensorSimulator:
    """模拟真实小车的传感器数据"""

    def __init__(self):
        self.temperature = 25.0       # 基础温度
        self.humidity = 55.0          # 基础湿度
        self.ultrasonic_cm = 150.0    # 超声波距离 (默认无遮挡)
        self.ir_left = False
        self.ir_right = False
        self.imu_heading = 0.0        # IMU 航向角
        self.imu_gyro_z = 0.0
        self.speed_pwm = 0            # 当前 PWM 速度
        self.speed_kmh = 0.0          # GPS 速度

        # GPS 模拟 (默认: 北京邮电大学附近)
        self.latitude = 39.9590
        self.longitude = 116.3370
        self.altitude = 45.0
        self.satellites = 12
        self.gps_fix = True

        # 避障模式
        self.obstacle_mode = False
        self.wp_index = 0
        self.wp_total = 0

    def randomize(self):
        """添加随机噪声, 模拟真实传感器波动"""
        self.temperature += random.gauss(0, 0.3)
        self.temperature = round(self.temperature, 1)
        self.humidity += random.gauss(0, 0.5)
        self.humidity = round(max(0, min(100, self.humidity)), 1)

        # 超声波: 偶然有波动
        if not self.obstacle_mode:
            self.ultrasonic_cm += random.gauss(0, 5)
            self.ultrasonic_cm = round(max(2, min(400, self.ultrasonic_cm)), 1)

        # GPS 微小漂移 (±0.00005 度 ≈ ±5m)
        self.latitude += random.gauss(0, 0.00005)
        self.longitude += random.gauss(0, 0.00005)
        self.latitude = round(self.latitude, 6)
        self.longitude = round(self.longitude, 6)

        # IMU 微小漂移
        self.imu_gyro_z += random.gauss(0, 0.5)
        self.imu_heading += self.imu_gyro_z * 0.1
        self.imu_heading %= 360.0
        self.imu_heading = round(self.imu_heading, 1)

    def set_obstacle_ahead(self, distance_cm: float = 15.0):
        """模拟前方出现障碍物"""
        self.ultrasonic_cm = distance_cm
        self.ir_left = True
        self.ir_right = True
        self.obstacle_mode = True

    def set_obstacle_left(self):
        """模拟左侧障碍物"""
        self.ir_left = True
        self.ir_right = False
        self.obstacle_mode = True

    def set_obstacle_right(self):
        """模拟右侧障碍物"""
        self.ir_left = False
        self.ir_right = True
        self.obstacle_mode = True

    def clear_obstacle(self):
        """清除障碍物"""
        self.obstacle_mode = False
        self.ir_left = False
        self.ir_right = False
        self.ultrasonic_cm = 150.0

    def move_towards(self, target_lat: float, target_lng: float, step: float = 0.0001):
        """向目标坐标移动一步"""
        dlat = target_lat - self.latitude
        dlng = target_lng - self.longitude
        dist = math.sqrt(dlat**2 + dlng**2)
        if dist > step:
            self.latitude += dlat / dist * step
            self.longitude += dlng / dist * step
        else:
            self.latitude = target_lat
            self.longitude = target_lng


# ═══════════════════════════════════════════════════════════════
#  JSON 构造 (完全匹配 UNO 固件的键顺序)
# ═══════════════════════════════════════════════════════════════

def build_telemetry_json(sensor: SensorSimulator, include_gps: bool = True,
                         include_mpu: bool = True) -> tuple:
    """
    构建与 UNO 固件完全相同键顺序的 JSON 字符串.

    返回: (json_body_for_sign, full_json_with_signature)
    - json_body_for_sign: 签名前的 JSON (结尾为逗号)
    - full_json_with_signature: 完整 JSON
    """
    # ⚠ 键顺序必须与 smartrover_uno.ino sendTelemetry() 完全一致!
    parts = []

    # device_id
    parts.append(f'"device_id":"{DEVICE_ID}"')

    # GPS
    if include_gps and sensor.gps_fix:
        parts.append(f'"latitude":{sensor.latitude:.4f}')
        parts.append(f'"longitude":{sensor.longitude:.4f}')
    else:
        parts.append('"latitude":null')
        parts.append('"longitude":null')

    # 温湿度
    parts.append(f'"temperature":{sensor.temperature:.1f}')
    parts.append(f'"humidity":{sensor.humidity:.1f}')

    # 超声波
    parts.append(f'"ultrasonic_cm":{sensor.ultrasonic_cm:.1f}')

    # 红外
    ir_obs = "true" if (sensor.ir_left or sensor.ir_right) else "false"
    ir_l = "true" if sensor.ir_left else "false"
    ir_r = "true" if sensor.ir_right else "false"
    parts.append(f'"ir_obstacle":{ir_obs}')
    parts.append(f'"ir_left":{ir_l}')
    parts.append(f'"ir_right":{ir_r}')

    # IMU (MPU6050)
    if include_mpu:
        parts.append(f'"imu_heading":{sensor.imu_heading:.1f}')
        parts.append(f'"imu_gyro_z":{sensor.imu_gyro_z:.1f}')

    # 速度
    parts.append(f'"speed_pwm":{sensor.speed_pwm}')

    # GPS 扩展字段
    if include_gps and sensor.gps_fix:
        parts.append(f'"altitude":{sensor.altitude:.1f}')
        parts.append(f'"speed_kmh":{sensor.speed_kmh:.1f}')
        parts.append(f'"satellites":{sensor.satellites}')

    # 拼接: 每个字段后跟逗号, 最后还有一个逗号!
    # (固件在签名计算时 JSON 以逗号结尾, 尚未追加 "signature" 和 "}")
    json_body = "{" + ",".join(parts) + ","

    # 计算 XOR 签名
    sig = compute_xor_signature(json_body, DEVICE_SECRET)

    # 完整 JSON
    full_json = f'{json_body}"signature":"{sig}"}}'

    return json_body, full_json


def build_heartbeat_json(uptime_s: float, wifi_ok: bool, mqtt_ok: bool) -> str:
    """构建心跳 JSON"""
    return json.dumps({
        "device_id": DEVICE_ID,
        "uptime_s": int(uptime_s),
        "wifi": "true" if wifi_ok else "false",
        "mqtt": "true" if mqtt_ok else "false",
    })


def build_nav_event_json(event_type: str, detail: str, lat: float, lng: float,
                          wp_index: int = None, wp_total: int = None,
                          state: str = "navigating") -> str:
    """构建导航事件 JSON"""
    evt = {
        "device_id": DEVICE_ID,
        "event_type": event_type,
        "detail": detail,
        "lat": lat,
        "lng": lng,
        "state": state,
        "timestamp": int(time.time()),
    }
    if wp_index is not None:
        evt["wp_index"] = wp_index
    if wp_total is not None:
        evt["wp_total"] = wp_total
    return json.dumps(evt)


# ═══════════════════════════════════════════════════════════════
#  MQTT 客户端
# ═══════════════════════════════════════════════════════════════

class SimCarClient:
    """模拟小车 MQTT 客户端"""

    def __init__(self, device_id: str = DEVICE_ID):
        self.device_id = device_id
        self.sensor = SensorSimulator()
        self.connected = False
        self.start_time = time.time()

        # 巡航状态
        self.route_waypoints = []     # [(lat, lng), ...]
        self.current_wp_index = 0
        self.cruising = False

        # 统计
        self.telemetry_count = 0
        self.heartbeat_count = 0
        self.nav_event_count = 0
        self.commands_received = []

        # MQTT 客户端
        self._client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION2,
            client_id=f'simcar-{device_id}',
        )
        if MQTT_USERNAME:
            self._client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)

        self._client.on_connect = self._on_connect
        self._client.on_disconnect = self._on_disconnect
        self._client.on_message = self._on_message

    # ── MQTT 回调 ──────────────────────────────────────────────

    def _on_connect(self, client, userdata, flags, rc, properties=None):
        if rc == 0:
            self.connected = True
            logger.info(f"✅ MQTT 已连接 (broker={MQTT_BROKER_HOST}:{MQTT_BROKER_PORT})")
            # 订阅下行指令主题
            topic = f'cmd/{self.device_id}'
            client.subscribe(topic, qos=1)
            logger.info(f"📡 已订阅 {topic}")
        else:
            logger.error(f"❌ MQTT 连接失败, rc={rc}")

    def _on_disconnect(self, client, userdata, flags, rc, properties=None):
        self.connected = False
        if rc != 0:
            logger.warning(f"⚠️  MQTT 意外断开, rc={rc}")

    def _on_message(self, client, userdata, msg):
        """处理下行指令"""
        try:
            payload = msg.payload.decode('utf-8')
            logger.info(f"📩 收到指令 [{msg.topic}]: {payload}")
            data = json.loads(payload)
            self.commands_received.append(data)
            self._handle_command(data)
        except Exception as e:
            logger.error(f"解析指令失败: {e}")

    def _handle_command(self, data: dict):
        """模拟执行下行指令"""
        cmd = data.get('command') or data.get('cmd', '')
        speed = data.get('speed_pwm', 150)

        if cmd == 'forward':
            self.sensor.speed_pwm = speed
            self.sensor.speed_kmh = speed / 255 * 2.0  # ~2km/h 最大
            logger.info(f"🚀 前进 (PWM={speed})")
        elif cmd == 'backward':
            self.sensor.speed_pwm = speed
            self.sensor.speed_kmh = -speed / 255 * 1.0
            logger.info(f"🔙 后退 (PWM={speed})")
        elif cmd == 'left':
            self.sensor.speed_pwm = speed
            self.sensor.imu_gyro_z = -50.0
            logger.info(f"⬅️  左转 (PWM={speed})")
        elif cmd == 'right':
            self.sensor.speed_pwm = speed
            self.sensor.imu_gyro_z = 50.0
            logger.info(f"➡️  右转 (PWM={speed})")
        elif cmd == 'stop':
            self.sensor.speed_pwm = 0
            self.sensor.speed_kmh = 0
            self.sensor.imu_gyro_z = 0
            self.cruising = False
            logger.info("🛑 停止")
        elif cmd == 'route':
            self._load_route(data)
        else:
            logger.warning(f"未知指令: {cmd}")

    def _load_route(self, data: dict):
        """加载巡航路线"""
        waypoints = data.get('waypoints', [])
        if len(waypoints) < 2:
            logger.warning("路线航点不足")
            return
        self.route_waypoints = [(wp['lat'], wp['lng']) for wp in waypoints]
        self.current_wp_index = 0
        self.cruising = True
        self.sensor.wp_total = len(waypoints)
        self.sensor.wp_index = 0
        logger.info(f"🗺️  加载路线: {len(waypoints)} 个航点, 开始巡航")

    # ── 连接 ───────────────────────────────────────────────────

    def connect(self):
        """连接 MQTT Broker"""
        logger.info(f"🔌 正在连接 MQTT Broker: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}...")
        try:
            self._client.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, keepalive=60)
            self._client.loop_start()
            time.sleep(1)  # 等待连接建立
        except Exception as e:
            logger.error(f"❌ 连接失败: {e}")
            raise

    def disconnect(self):
        """断开连接"""
        self._client.loop_stop()
        self._client.disconnect()
        self.connected = False
        logger.info("👋 已断开 MQTT 连接")

    # ── 数据发送 ───────────────────────────────────────────────

    def send_telemetry(self) -> bool:
        """发送加密遥测数据"""
        json_body, full_json = build_telemetry_json(self.sensor)

        # XOR 加密
        ciphertext = xor_encrypt_base64(full_json, XOR_KEY)

        topic = f'sensor/{self.device_id}'
        result = self._client.publish(topic, ciphertext, qos=1)

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            self.telemetry_count += 1
            logger.debug(
                f"📤 遥测 #{self.telemetry_count}: "
                f"T={self.sensor.temperature}°C H={self.sensor.humidity}% "
                f"US={self.sensor.ultrasonic_cm}cm PWM={self.sensor.speed_pwm} "
                f"GPS=({self.sensor.latitude:.4f},{self.sensor.longitude:.4f})"
            )
            return True
        else:
            logger.error(f"遥测发送失败, rc={result.rc}")
            return False

    def send_heartbeat(self) -> bool:
        """发送心跳"""
        uptime = time.time() - self.start_time
        payload = build_heartbeat_json(uptime, True, self.connected)
        topic = f'heartbeat/{self.device_id}'
        result = self._client.publish(topic, payload, qos=1)

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            self.heartbeat_count += 1
            logger.debug(f"💓 心跳 #{self.heartbeat_count} (uptime={int(uptime)}s)")
            return True
        else:
            logger.error(f"心跳发送失败, rc={result.rc}")
            return False

    def send_nav_event(self, event_type: str, detail: str = "") -> bool:
        """发送导航事件"""
        payload = build_nav_event_json(
            event_type=event_type,
            detail=detail,
            lat=self.sensor.latitude,
            lng=self.sensor.longitude,
            wp_index=self.sensor.wp_index if self.sensor.wp_total > 0 else None,
            wp_total=self.sensor.wp_total if self.sensor.wp_total > 0 else None,
            state="navigating" if self.cruising else "idle",
        )
        topic = f'nav/{self.device_id}'
        result = self._client.publish(topic, payload, qos=1)

        if result.rc == mqtt.MQTT_ERR_SUCCESS:
            self.nav_event_count += 1
            logger.info(f"🧭 导航事件 #{self.nav_event_count}: {event_type} - {detail}")
            return True
        return False

    # ── 巡航逻辑 ───────────────────────────────────────────────

    def cruise_step(self):
        """巡航模式: 向当前航点移动"""
        if not self.cruising or not self.route_waypoints:
            return

        if self.current_wp_index >= len(self.route_waypoints):
            self.cruising = False
            self.sensor.speed_pwm = 0
            self.sensor.speed_kmh = 0
            self.send_nav_event("ROUTE_COMPLETE", "巡航路线已完成")
            logger.info("🏁 巡航路线完成!")
            return

        target_lat, target_lng = self.route_waypoints[self.current_wp_index]

        # 计算到目标距离 (简化的平面近似)
        dlat = (target_lat - self.sensor.latitude) * 111320.0
        dlng = (target_lng - self.sensor.longitude) * 111320.0 * math.cos(
            math.radians(self.sensor.latitude)
        )
        dist_m = math.sqrt(dlat**2 + dlng**2)

        if dist_m < 2.0:  # 到达航点 (2米内)
            logger.info(f"📍 到达航点 #{self.current_wp_index + 1}/{len(self.route_waypoints)}")
            self.current_wp_index += 1
            self.sensor.wp_index = self.current_wp_index

            if self.current_wp_index < len(self.route_waypoints):
                self.send_nav_event(
                    "WP_REACHED",
                    f"到达航点 {self.current_wp_index}/{len(self.route_waypoints)}"
                )
            return

        # 向目标移动
        self.sensor.speed_pwm = 120
        self.sensor.speed_kmh = 1.0
        self.sensor.move_towards(target_lat, target_lng, step=0.00003)

    def get_stats(self) -> dict:
        """获取运行统计"""
        uptime = time.time() - self.start_time
        return {
            "device_id": self.device_id,
            "uptime_s": int(uptime),
            "connected": self.connected,
            "telemetry_sent": self.telemetry_count,
            "heartbeats_sent": self.heartbeat_count,
            "nav_events_sent": self.nav_event_count,
            "commands_received": len(self.commands_received),
            "sensor": {
                "temperature": self.sensor.temperature,
                "humidity": self.sensor.humidity,
                "ultrasonic_cm": self.sensor.ultrasonic_cm,
                "latitude": self.sensor.latitude,
                "longitude": self.sensor.longitude,
            },
        }


# ═══════════════════════════════════════════════════════════════
#  攻击模拟
# ═══════════════════════════════════════════════════════════════

def run_attack_simulation(client: SimCarClient):
    """模拟攻击场景测试安全机制"""
    logger.info("⚠️  开始攻击模拟...")

    # 1. 非法设备 (不在白名单中)
    logger.info("[攻击1] 模拟未注册设备发送数据...")
    attacker = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id='attacker-001')
    attacker.connect(MQTT_BROKER_HOST, MQTT_BROKER_PORT, keepalive=10)
    attacker.loop_start()
    fake_data = json.dumps({"device_id": "EVIL_DEVICE_999", "temperature": 999})
    attacker.publish('sensor/EVIL_DEVICE_999', fake_data, qos=1)
    time.sleep(1)
    attacker.loop_stop()
    attacker.disconnect()
    logger.info("  ↳ 后端应记录 auth_fail 审计日志")

    # 2. 篡改签名 (重放攻击模拟)
    logger.info("[攻击2] 模拟篡改签名...")
    json_body, full_json = build_telemetry_json(client.sensor)
    # 修改签名最后一位
    tampered = full_json[:-3] + "FFF}"
    ciphertext = xor_encrypt_base64(tampered, XOR_KEY)
    client._client.publish(f'sensor/{DEVICE_ID}', ciphertext, qos=1)
    time.sleep(1)
    logger.info("  ↳ 后端应记录 sig_invalid 审计日志")

    # 3. 模拟碰撞预警 (超声波 < 50cm)
    logger.info("[攻击3] 模拟碰撞预警 (ultrasonic=15cm)...")
    orig_cm = client.sensor.ultrasonic_cm
    client.sensor.set_obstacle_ahead(15.0)
    client.send_telemetry()
    time.sleep(1)
    client.sensor.clear_obstacle()
    client.sensor.ultrasonic_cm = orig_cm
    logger.info("  ↳ 后端应自动下发紧急停止指令 + 碰撞预警计数+1")

    logger.info("✅ 攻击模拟完成")


# ═══════════════════════════════════════════════════════════════
#  场景模拟: 小车绕圈巡航
# ═══════════════════════════════════════════════════════════════

def run_route_demo(client: SimCarClient):
    """演示: 模拟小车沿预设路线巡航"""
    logger.info("🗺️  加载演示路线...")

    # 预设巡航路线 (北京邮电大学附近小范围)
    base_lat, base_lng = 39.9590, 116.3370
    route = [
        (base_lat, base_lng),
        (base_lat + 0.0005, base_lng),
        (base_lat + 0.0005, base_lng + 0.0005),
        (base_lat, base_lng + 0.0005),
        (base_lat, base_lng),  # 回到起点
    ]

    client._load_route({"waypoints": [
        {"lat": lat, "lng": lng} for lat, lng in route
    ]})
    client.send_nav_event("ROUTE_START", f"开始巡航, 共{len(route)}个航点")
    logger.info(f"  航点: {route}")


# ═══════════════════════════════════════════════════════════════
#  主循环
# ═══════════════════════════════════════════════════════════════

def main():
    parser = argparse.ArgumentParser(
        description='SmartRover 小车 MQTT 模拟器',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  %(prog)s                          # 基础遥测+心跳
  %(prog)s --device UNO_002         # 指定设备ID
  %(prog)s --route                   # 带自动巡航
  %(prog)s --attack                  # 攻击场景模拟
  %(prog)s --obstacle                # 间歇性障碍物模拟
  %(prog)s --interval-telemetry 2    # 自定义遥测间隔
        """,
    )
    parser.add_argument('--device', default=DEVICE_ID,
                        help=f'设备ID (默认: {DEVICE_ID})')
    parser.add_argument('--interval-telemetry', type=float, default=TELEMETRY_INTERVAL_S,
                        help=f'遥测间隔秒 (默认: {TELEMETRY_INTERVAL_S})')
    parser.add_argument('--interval-heartbeat', type=float, default=HEARTBEAT_INTERVAL_S,
                        help=f'心跳间隔秒 (默认: {HEARTBEAT_INTERVAL_S})')
    parser.add_argument('--route', action='store_true',
                        help='启用巡航路线模拟')
    parser.add_argument('--attack', action='store_true',
                        help='运行攻击场景模拟')
    parser.add_argument('--obstacle', action='store_true',
                        help='间歇性障碍物模拟')
    parser.add_argument('--debug', action='store_true',
                        help='调试模式 (更多日志)')
    args = parser.parse_args()

    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)

    # 覆盖全局配置
    global DEVICE_ID
    DEVICE_ID = args.device

    # 创建客户端
    client = SimCarClient(device_id=args.device)

    logger.info("=" * 60)
    logger.info("  SmartRover 小车 MQTT 模拟器 v2.0")
    logger.info(f"  设备ID: {args.device}")
    logger.info(f"  MQTT: {MQTT_BROKER_HOST}:{MQTT_BROKER_PORT}")
    logger.info(f"  遥测间隔: {args.interval_telemetry}s | 心跳间隔: {args.interval_heartbeat}s")
    logger.info("=" * 60)

    # 连接
    try:
        client.connect()
    except Exception:
        logger.error("无法连接 MQTT Broker, 请确认 Mosquitto 已启动")
        sys.exit(1)

    if not client.connected:
        logger.error("MQTT 未就绪, 退出")
        sys.exit(1)

    # 巡航模式
    if args.route:
        run_route_demo(client)

    # 攻击模拟
    if args.attack:
        threading.Timer(10.0, lambda: run_attack_simulation(client)).start()

    # ── 主循环 ───────────────────────────────────────────────
    t_telemetry = 0.0
    t_heartbeat = 0.0
    t_nav = 0.0
    obstacle_cycle = 0

    logger.info("🔄 开始模拟数据流 (Ctrl+C 停止)...")

    try:
        while True:
            now = time.time()

            # 遥测
            if now - t_telemetry >= args.interval_telemetry:
                client.sensor.randomize()
                client.send_telemetry()
                t_telemetry = now

            # 心跳
            if now - t_heartbeat >= args.interval_heartbeat:
                client.send_heartbeat()
                t_heartbeat = now

            # 巡航
            if client.cruising:
                if now - t_nav >= NAV_EVENT_INTERVAL_S:
                    client.cruise_step()
                    t_nav = now

            # 间歇性障碍物模拟
            if args.obstacle:
                obstacle_cycle += 1
                if obstacle_cycle % 20 == 0:   # ~每100秒
                    client.sensor.set_obstacle_ahead(random.uniform(10, 40))
                    logger.info("🧱 模拟障碍物 (前方)")
                elif obstacle_cycle % 20 == 10:  # ~50秒后清除
                    client.sensor.clear_obstacle()
                    logger.info("✅ 障碍物清除")

            time.sleep(0.5)

    except KeyboardInterrupt:
        logger.info("\n🛑 收到停止信号...")

    finally:
        # 打印统计
        stats = client.get_stats()
        logger.info("=" * 60)
        logger.info("  运行统计")
        logger.info(f"    运行时长: {stats['uptime_s']}s")
        logger.info(f"    遥测发送: {stats['telemetry_sent']} 条")
        logger.info(f"    心跳发送: {stats['heartbeats_sent']} 条")
        logger.info(f"    导航事件: {stats['nav_events_sent']} 条")
        logger.info(f"    收到指令: {stats['commands_received']} 条")
        logger.info(f"    传感器: T={stats['sensor']['temperature']}°C "
                     f"H={stats['sensor']['humidity']}% "
                     f"US={stats['sensor']['ultrasonic_cm']}cm")
        logger.info("=" * 60)
        client.disconnect()


if __name__ == '__main__':
    main()

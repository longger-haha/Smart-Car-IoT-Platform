# 后端算法中心化架构方案

> **核心原则**：所有算法/决策逻辑在后端执行，UNO 只负责传感器采集 + 电机执行，传感器仍以 JSON 格式上报。

---

## 一、现状分析

### 1.1 当前固件端算法（需迁移至后端）

| 算法模块 | 当前位置 | 代码量 | 说明 |
|---------|---------|-------|------|
| PID 航向控制 | ESP32 autopilot | ~80 行 | computePIDSteering() |
| Waypoint 导航循环 | ESP32 autopilot | ~200 行 | 航点到达判定、状态机 |
| 避障决策 + 绕行 | ESP32 autopilot | ~100 行 | 紧急停车 + 绕行状态机 |
| IMU 互补滤波 | ESP32 autopilot | ~60 行 | COMP_ALPHA 融合 |
| IMU 航向积分 | UNO smartrover_uno | ~40 行 | gyroZ 积分 → heading |
| 电池低电量决策 | ESP32 autopilot | ~40 行 | 电压阈值 → 中止巡航 |
| GPS Haversine 公式 | ESP32 autopilot | ~20 行 | 距离 + 方位角计算 |

### 1.2 当前 UNO 传感器上报 JSON 格式

```json
{
  "device_id": "10.32.44.189",
  "latitude": 39.9042,
  "longitude": 116.4074,
  "temperature": 26.5,
  "humidity": 65.0,
  "ultrasonic_cm": 120.3,
  "ir_obstacle": "0,0",
  "imu_heading": 180.0,
  "imu_gyro_z": 3.8,
  "speed_pwm": 150,
  "altitude": 50.5,
  "speed_kmh": 2.3,
  "satellites": 8,
  "signature": "A1B2C3D4"
}
```

**问题**：`imu_heading` 是 UNO 端积分计算的结果，后端无法做更精确的融合。

### 1.3 当前后端已有模块

| 模块 | 文件 | 功能 |
|------|------|------|
| MQTT 客户端 | `src/utils/mqtt_client.py` | 订阅遥测、发布指令、解密验签 |
| 指令服务 | `src/services/command_service.py` | HMAC 签名 + MQTT 发布 |
| 巡航安全 | `src/utils/cruise_security.py` | 频率限制、重放检测、坐标异常 |
| 加解密 | `src/utils/crypto_tool.py` | AES/XOR 加解密、HMAC/XOR 签名 |
| 路由接口 | `src/routes/vehicle.py` | 指令下发、路线规划、轨迹查询 |

---

## 二、新架构设计

### 2.1 整体架构图

```
┌──────────────────────────────────────────────────────────────┐
│                     Backend (Flask)                           │
│                                                               │
│  ┌────────────────────────────────────────────────────────┐  │
│  │              遥测处理管道 (Telemetry Pipeline)           │  │
│  │  MQTT接收 → 解密验签 → JSON解析 → 写入DB → 触发决策    │  │
│  └───────────────────────┬────────────────────────────────┘  │
│                          │                                    │
│  ┌───────────┐  ┌───────┴────────┐  ┌────────────────────┐  │
│  │ IMU融合器  │  │  导航决策引擎   │  │   避障决策引擎     │  │
│  │           │  │               │  │                    │  │
│  │ 互补滤波   │  │ PID航向控制    │  │ 安全距离判定       │  │
│  │ 航向积分   │  │ 航点到达判定   │  │ 紧急停车           │  │
│  │ GPS/IMU融合│  │ 差速输出计算   │  │ 绕行策略           │  │
│  └─────┬─────┘  │ 电池状态决策   │  │ 红外避障方向       │  │
│        │        └───────┬────────┘  └─────────┬──────────┘  │
│        │                │                      │             │
│        └────────────────┼──────────────────────┘             │
│                         │                                     │
│  ┌──────────────────────┴──────────────────────────────────┐ │
│  │              指令调度器 (Command Dispatcher)              │ │
│  │                                                         │ │
│  │  手动模式: 直接转发前端指令                               │ │
│  │  自动模式: 导航引擎持续下发差速指令                       │ │
│  │  避障模式: 紧急停车 + 绕行指令                           │ │
│  │                                                         │ │
│  │  输出: {cmd, pwm_l, pwm_r, dur_ms}                      │ │
│  └────────────────────────┬────────────────────────────────┘ │
│                           │ MQTT cmd/<device_id>             │
└───────────────────────────┼──────────────────────────────────┘
                            │
                     ┌──────┴──────┐
                     │ MQTT Broker │
                     └──────┬──────┘
                            │
┌───────────────────────────┼──────────────────────────────────┐
│                    UNO (瘦客户端)                              │
│                                                               │
│  ┌────────────────────┐    ┌─────────────────────────────┐   │
│  │   传感器采集 (5s)   │    │    电机执行 (实时响应)       │   │
│  │                    │    │                             │   │
│  │ DHT11  → 温度/湿度 │    │ 收到 cmd → 解析 → 驱动电机  │   │
│  │ HC-SR04 → 超声波   │    │                             │   │
│  │ IR ×2  → 红外      │    │ forward/backward/left/right │   │
│  │ MPU6050→ 6轴原始值 │    │ stop                        │   │
│  │ GPS    → NMEA→经纬度│   │ diff (差速: pwm_l, pwm_r)   │   │
│  │ ADC    → 电池电压   │    │                             │   │
│  └────────┬───────────┘    └─────────────────────────────┘   │
│           │ JSON + XOR加密                                    │
│           │ MQTT sensor/<id>                                  │
└───────────┘
```

### 2.2 数据流闭环

```
  UNO 传感器采集
       │
       ▼
  JSON 构建 + XOR 加密
       │
       ▼ MQTT sensor/<device_id>
  后端接收 + 解密验签
       │
       ▼
  遥测处理管道
       │
       ├──→ 写入 telemetry_points 表
       ├──→ 更新设备心跳/在线状态
       ├──→ IMU 融合 (互补滤波 → 精确航向)
       ├──→ 避障判定 (超声波/红外 → 安全决策)
       ├──→ 导航决策 (PID → 差速指令)
       │
       ▼ MQTT cmd/<device_id>
  UNO 接收指令 → 电机执行
       │
       ▼
  传感器采集下一帧 → 循环
```

---

## 三、协议设计

### 3.1 UNO → 后端：传感器遥测 JSON

**新格式**（在原有基础上增加原始 IMU 和电池字段）：

```json
{
  "device_id": "10.32.44.189",
  "temperature": 26.5,
  "humidity": 65.0,
  "ultrasonic_cm": 120.3,
  "ir_l": 0,
  "ir_r": 0,
  "imu_ax": 0.12,
  "imu_ay": -0.03,
  "imu_az": 1.01,
  "imu_gx": -45.2,
  "imu_gy": 12.3,
  "imu_gz": 3.8,
  "latitude": 39.9042,
  "longitude": 116.4074,
  "altitude": 50.5,
  "speed_kmh": 2.3,
  "satellites": 8,
  "bat_mv": 12450,
  "speed_pwm": 150,
  "seq": 42,
  "signature": "A1B2C3D4"
}
```

**字段变更对比**：

| 字段 | 旧格式 | 新格式 | 说明 |
|------|--------|--------|------|
| `ir_obstacle` | `"0,0"` 字符串 | 拆分为 `ir_l` + `ir_r` 整数 | 后端可直接判断左右方向 |
| `imu_heading` | 浮点数 (UNO计算) | **移除** | 改由后端 IMU 融合计算 |
| `imu_gyro_z` | 单一Z轴 | 扩展为 `imu_gx/gy/gz` 三轴 | 后端需完整角速度做融合 |
| — | 无 | 新增 `imu_ax/ay/az` | 加速度计原始值，用于互补滤波 |
| — | 无 | 新增 `bat_mv` | 电池电压(mV)，后端做低电量决策 |
| — | 无 | 新增 `seq` | 消息序号，用于丢包检测 |

> **NMEA 解析保留在 UNO**：GPS NMEA → 经纬度的转换属于数据格式解析，不是算法决策，留在 UNO 端可减少上报数据量。

### 3.2 后端 → UNO：电机控制指令 JSON

**新增差速指令**，同时兼容原有简单指令：

```json
// 差速控制 (后端 PID 输出)
{"cmd": "diff", "pwm_l": 180, "pwm_r": 120, "dur_ms": 500}

// 简单指令 (手动控制，保持兼容)
{"cmd": "forward",  "pwm": 150}
{"cmd": "backward", "pwm": 150}
{"cmd": "left",     "pwm": 150}
{"cmd": "right",    "pwm": 150}
{"cmd": "stop"}

// 巡航路线下发 (保持现有格式)
{"cmd": "route", "device_id": "...", "waypoints": [...], "timestamp": ..., "signature": ...}
```

**差速指令字段说明**：

| 字段 | 类型 | 说明 |
|------|------|------|
| `cmd` | string | `"diff"` 表示差速模式 |
| `pwm_l` | int | 左侧电机 PWM (0~255, 负值=反转) |
| `pwm_r` | int | 右侧电机 PWM (0~255, 负值=反转) |
| `dur_ms` | int | 持续时间(ms), 0=持续到下一条指令 |

**差速指令到电机映射**：

```
pwm_l > 0 → 左前/左后 FORWARD,  speed = pwm_l
pwm_l < 0 → 左前/左后 BACKWARD, speed = |pwm_l|
pwm_l = 0 → 左侧 RELEASE

pwm_r > 0 → 右前/右后 FORWARD,  speed = pwm_r
pwm_r < 0 → 右前/右后 BACKWARD, speed = |pwm_r|
pwm_r = 0 → 右侧 RELEASE
```

### 3.3 UNO → 后端：心跳 JSON（保持不变）

```json
{
  "device_id": "10.32.44.189",
  "uptime_s": 3600,
  "wifi": true,
  "mqtt": true
}
```

---

## 四、后端新增模块设计

### 4.1 模块结构

```
Backend/src/
├── services/
│   ├── command_service.py      # 已有：指令签名+发布
│   ├── navigation_engine.py    # 新增：导航决策引擎（核心）
│   ├── pid_controller.py       # 新增：PID 控制器
│   ├── imu_fusion.py           # 新增：IMU 互补滤波
│   └── obstacle_avoidance.py   # 新增：避障决策引擎
├── utils/
│   ├── mqtt_client.py          # 修改：遥测回调接入导航引擎
│   ├── crypto_tool.py          # 已有：加解密
│   └── cruise_security.py      # 已有：巡航安全
└── routes/
    └── vehicle.py              # 修改：新增导航控制接口
```

### 4.2 `imu_fusion.py` — IMU 互补滤波

```python
"""
imu_fusion.py — 后端 IMU 互补滤波 + 航向融合

替代 UNO/ESP32 端的 IMU 航向积分和互补滤波算法。
接收原始6轴数据，输出精确航向角。

融合策略:
  - 低速/静止: 以加速度计姿态为主 (防陀螺漂移)
  - 高速运动: 以陀螺仪积分为主 (加速度计噪声大)
  - GPS 可用时: GPS 航向与 IMU 航向加权融合
"""

import math
import time
from typing import Optional


class IMUState:
    """单个设备的 IMU 融合状态"""

    COMP_ALPHA = 0.98          # 互补滤波系数
    GYRO_SCALE_2000 = 16.4    # ±2000°/s 量程 LSB/(°/s)
    ACCEL_SCALE_2G = 16384.0  # ±2g 量程 LSB/g

    def __init__(self):
        self.heading = 0.0            # 当前航向角 (0~360°)
        self.pitch = 0.0
        self.roll = 0.0
        self.gyro_z_dps = 0.0        # Z轴角速度 (°/s)
        self.last_update = None       # 上次更新时间戳
        self.initialized = False

    def update(self, ax: float, ay: float, az: float,
               gx: float, gy: float, gz: float,
               gps_speed_kmh: float = 0.0,
               gps_course: Optional[float] = None) -> float:
        """
        输入原始6轴数据，更新航向角。

        Args:
            ax, ay, az: 加速度计 (g)
            gx, gy, gz: 陀螺仪 (°/s, 已去零偏)
            gps_speed_kmh: GPS 速度 (km/h)
            gps_course: GPS 航向 (°, 0~360)

        Returns:
            融合后的航向角 (0~360°)
        """
        now = time.time()

        if not self.initialized:
            # 首次更新: 用加速度计初始化姿态
            self.roll = math.atan2(ay, az) * 180.0 / math.pi
            self.pitch = math.atan2(-ax,
                         math.sqrt(ay * ay + az * az)) * 180.0 / math.pi
            self.heading = gps_course if gps_course is not None else 0.0
            self.gyro_z_dps = gz
            self.last_update = now
            self.initialized = True
            return self.heading

        dt = now - self.last_update
        if dt <= 0 or dt > 2.0:
            dt = 0.05  # 默认50ms
        self.last_update = now

        # 1. 陀螺仪积分
        self.gyro_z_dps = gz
        gyro_heading_delta = gz * dt

        # 2. 加速度计姿态 (仅在动态较小时可信)
        acc_roll = math.atan2(ay, az) * 180.0 / math.pi
        acc_pitch = math.atan2(-ax,
                    math.sqrt(ay * ay + az * az)) * 180.0 / math.pi

        # 3. 互补滤波
        self.roll = (self.COMP_ALPHA * (self.roll + gx * dt) +
                     (1 - self.COMP_ALPHA) * acc_roll)
        self.pitch = (self.COMP_ALPHA * (self.pitch + gy * dt) +
                      (1 - self.COMP_ALPHA) * acc_pitch)

        # 4. 航向融合
        self.heading += gyro_heading_delta

        # GPS 航向修正 (速度 > 3km/h 时 GPS 航向可信)
        if gps_course is not None and gps_speed_kmh > 3.0:
            blend = min(gps_speed_kmh / 5.0, 1.0)
            diff = self._normalize_angle(gps_course - self.heading)
            self.heading += blend * diff * 0.1  # 柔和修正

        self.heading %= 360.0
        if self.heading < 0:
            self.heading += 360.0

        return self.heading

    @staticmethod
    def _normalize_angle(angle: float) -> float:
        while angle > 180:
            angle -= 360
        while angle < -180:
            angle += 360
        return angle
```

### 4.3 `pid_controller.py` — PID 控制器

```python
"""
pid_controller.py — 后端 PID 航向控制器

替代 ESP32 端的 computePIDSteering()。
输入目标方位角和当前航向，输出差速转向量。
"""

import math


class PIDController:
    """PID 航向控制器"""

    def __init__(self, kp: float = 2.5, ki: float = 0.02,
                 kd: float = 0.8, output_limit: float = 120.0):
        self.kp = kp
        self.ki = ki
        self.kd = kd
        self.output_limit = output_limit

        self.integral = 0.0
        self.last_error = 0.0
        self.integral_limit = 100.0

    def compute(self, target_bearing: float, current_heading: float,
                dt: float = 0.5) -> float:
        """
        计算 PID 转向输出。

        Args:
            target_bearing: 目标方位角 (°)
            current_heading: 当前航向角 (°)
            dt: 时间步长 (s)

        Returns:
            转向量 (-output_limit ~ +output_limit)
            正值 = 右转, 负值 = 左转
        """
        error = self._normalize_angle(target_bearing - current_heading)

        self.integral += error * dt
        self.integral = max(-self.integral_limit,
                            min(self.integral_limit, self.integral))

        derivative = (error - self.last_error) / dt if dt > 0 else 0.0
        self.last_error = error

        output = (self.kp * error +
                  self.ki * self.integral +
                  self.kd * derivative)

        return max(-self.output_limit, min(self.output_limit, output))

    def reset(self):
        self.integral = 0.0
        self.last_error = 0.0

    @staticmethod
    def _normalize_angle(angle: float) -> float:
        while angle > 180:
            angle -= 360
        while angle < -180:
            angle += 360
        return angle
```

### 4.4 `obstacle_avoidance.py` — 避障决策引擎

```python
"""
obstacle_avoidance.py — 后端避障决策引擎

替代 ESP32/UNO 端的避障逻辑。
根据超声波和红外数据生成避障指令。
"""

import logging
import time
from typing import Optional, Tuple

logger = logging.getLogger(__name__)


class ObstacleAvoidance:
    """避障决策引擎"""

    SAFE_DISTANCE_CM = 50.0
    CRITICAL_DISTANCE_CM = 20.0
    AVOID_COOLDOWN_S = 3.0  # 避障指令冷却时间

    def __init__(self):
        self._last_avoid_time = 0.0
        self._avoid_phase = 0
        self._avoid_start = 0.0

    def check(self, ultrasonic_cm: Optional[float],
              ir_l: int, ir_r: int) -> str:
        """
        检查障碍物状态。

        Returns:
            "clear" / "warning" / "critical"
        """
        us = ultrasonic_cm if ultrasonic_cm is not None else 999.0

        if us < self.CRITICAL_DISTANCE_CM and us > 0:
            return "critical"
        if us < self.SAFE_DISTANCE_CM and us > 0:
            return "warning"
        if ir_l or ir_r:
            return "warning"
        return "clear"

    def get_avoid_command(self, ultrasonic_cm: Optional[float],
                          ir_l: int, ir_r: int) -> Optional[dict]:
        """
        生成避障差速指令。

        Returns:
            差速指令 dict 或 None (无需避障)
        """
        status = self.check(ultrasonic_cm, ir_l, ir_r)

        if status == "clear":
            self._avoid_phase = 0
            return None

        now = time.time()

        # 紧急停车
        if status == "critical":
            self._avoid_phase = 0
            logger.warning(
                f'[AVOID] 紧急停车! us={ultrasonic_cm}cm '
                f'ir_l={ir_l} ir_r={ir_r}')
            return {"cmd": "stop"}

        # 避障绕行 (基于红外方向判断)
        if now - self._last_avoid_time < self.AVOID_COOLDOWN_S:
            return None  # 冷却中

        self._last_avoid_time = now

        if ir_l and not ir_r:
            # 左侧有障碍 → 右转避让
            return {"cmd": "diff", "pwm_l": 180, "pwm_r": 80,
                    "dur_ms": 800}
        elif ir_r and not ir_l:
            # 右侧有障碍 → 左转避让
            return {"cmd": "diff", "pwm_l": 80, "pwm_r": 180,
                    "dur_ms": 800}
        else:
            # 正前方障碍 → 后退
            return {"cmd": "diff", "pwm_l": -120, "pwm_r": -120,
                    "dur_ms": 500}
```

### 4.5 `navigation_engine.py` — 导航决策引擎（核心）

```python
"""
navigation_engine.py — 后端导航决策引擎

核心模块，替代固件端所有自动驾驶逻辑:
  - 航点管理 + 到达判定
  - PID 差速控制
  - IMU 融合
  - 避障决策
  - 电池状态决策
  - 导航状态机

由 mqtt_client.py 的遥测回调触发决策循环。
"""

import math
import time
import logging
from typing import Optional, Dict, List
from dataclasses import dataclass, field

from src.services.pid_controller import PIDController
from src.services.imu_fusion import IMUState
from src.services.obstacle_avoidance import ObstacleAvoidance
from src.utils.mqtt_client import publish_command

logger = logging.getLogger(__name__)

# ── 常量 ──────────────────────────────────────────────────────

EARTH_RADIUS_M = 6371000.0
ARRIVAL_RADIUS_M = 3.0
BASE_CRUISE_SPEED = 160
LOW_BATTERY_MV = 10500       # 10.5V
CRITICAL_BATTERY_MV = 10000  # 10.0V
COMMAND_INTERVAL_S = 1.0     # 自动巡航指令下发最小间隔


# ── 数据类 ────────────────────────────────────────────────────

@dataclass
class Waypoint:
    lat: float
    lng: float


class NavState:
    """导航状态枚举"""
    IDLE = "idle"
    CRUISING = "cruising"
    OBSTACLE_AVOID = "obstacle_avoid"
    ARRIVED = "arrived"
    ABORTED = "aborted"


@dataclass
class DeviceNavContext:
    """单个设备的导航上下文"""
    state: str = NavState.IDLE
    waypoints: List[Waypoint] = field(default_factory=list)
    current_wp_index: int = 0
    pid: PIDController = field(default_factory=PIDController)
    imu: IMUState = field(default_factory=IMUState)
    avoider: ObstacleAvoidance = field(default_factory=ObstacleAvoidance)
    last_command_time: float = 0.0
    cruise_start_time: float = 0.0


# ── 导航引擎 ──────────────────────────────────────────────────

class NavigationEngine:
    """
    后端导航决策引擎

    用法:
        engine = NavigationEngine()
        engine.start_cruise(device_id, waypoints)
        # 每次收到遥测时自动调用
        engine.on_telemetry(device_id, data)
    """

    def __init__(self):
        self._devices: Dict[str, DeviceNavContext] = {}

    # ── 公共接口 ──────────────────────────────────────────────

    def start_cruise(self, device_id: str,
                     waypoints: List[Dict]) -> dict:
        """
        启动巡航。

        Args:
            device_id: 设备ID
            waypoints: [{"lat": 39.9, "lng": 116.4}, ...]

        Returns:
            {"success": bool, "message": str}
        """
        if len(waypoints) < 2:
            return {"success": False, "message": "至少需要2个航点"}

        ctx = self._get_context(device_id)
        ctx.waypoints = [Waypoint(w['lat'], w['lng']) for w in waypoints]
        ctx.current_wp_index = 0
        ctx.state = NavState.CRUISING
        ctx.pid.reset()
        ctx.cruise_start_time = time.time()

        logger.info(
            f'[NAV] 巡航启动: device={device_id} '
            f'waypoints={len(ctx.waypoints)}')

        return {"success": True,
                "message": f"巡航启动，{len(ctx.waypoints)}个航点"}

    def stop_cruise(self, device_id: str) -> dict:
        """中止巡航"""
        ctx = self._get_context(device_id)
        ctx.state = NavState.ABORTED
        ctx.pid.reset()

        publish_command(device_id, {"cmd": "stop"})

        logger.info(f'[NAV] 巡航中止: device={device_id}')
        return {"success": True, "message": "巡航已中止"}

    def get_status(self, device_id: str) -> dict:
        """获取设备导航状态"""
        ctx = self._get_context(device_id)
        return {
            "state": ctx.state,
            "current_wp_index": ctx.current_wp_index,
            "total_waypoints": len(ctx.waypoints),
            "heading": ctx.imu.heading,
            "gyro_z": ctx.imu.gyro_z_dps,
        }

    def on_telemetry(self, device_id: str, data: dict):
        """
        遥测数据回调 — 核心决策循环。

        由 mqtt_client._process_telemetry() 在写入DB后调用。
        """
        ctx = self._get_context(device_id)

        # 1. IMU 融合
        heading = self._update_imu(ctx, data)

        # 2. 电池检查
        bat_mv = data.get('bat_mv')
        if bat_mv is not None and bat_mv < CRITICAL_BATTERY_MV:
            if ctx.state == NavState.CRUISING:
                ctx.state = NavState.ABORTED
                publish_command(device_id, {"cmd": "stop"})
                logger.warning(
                    f'[NAV] 电池严重不足 {bat_mv}mV, 巡航中止: '
                    f'device={device_id}')
            return

        # 3. 避障检查 (优先级最高)
        avoid_cmd = self._check_obstacle(ctx, data)
        if avoid_cmd:
            publish_command(device_id, avoid_cmd)
            return

        # 4. 巡航决策
        if ctx.state == NavState.CRUISING:
            self._cruise_step(device_id, ctx, data, heading)

    # ── 内部方法 ──────────────────────────────────────────────

    def _get_context(self, device_id: str) -> DeviceNavContext:
        if device_id not in self._devices:
            self._devices[device_id] = DeviceNavContext()
        return self._devices[device_id]

    def _update_imu(self, ctx: DeviceNavContext,
                    data: dict) -> float:
        """IMU 互补滤波融合"""
        ax = data.get('imu_ax', 0.0)
        ay = data.get('imu_ay', 0.0)
        az = data.get('imu_az', 0.0)
        gx = data.get('imu_gx', 0.0)
        gy = data.get('imu_gy', 0.0)
        gz = data.get('imu_gz', 0.0)

        gps_speed = data.get('speed_kmh', 0.0)
        gps_course = None
        if data.get('latitude') and data.get('longitude'):
            # GPS 航向可从速度方向推算，暂用 speed_kmh 判断
            pass

        return ctx.imu.update(ax, ay, az, gx, gy, gz,
                              gps_speed_kmh=gps_speed,
                              gps_course=gps_course)

    def _check_obstacle(self, ctx: DeviceNavContext,
                        data: dict) -> Optional[dict]:
        """避障检查"""
        us_cm = data.get('ultrasonic_cm')
        ir_l = data.get('ir_l', 0)
        ir_r = data.get('ir_r', 0)

        return ctx.avoider.get_avoid_command(us_cm, ir_l, ir_r)

    def _cruise_step(self, device_id: str, ctx: DeviceNavContext,
                     data: dict, heading: float):
        """单步巡航决策"""
        now = time.time()

        # 指令下发频率限制
        if now - ctx.last_command_time < COMMAND_INTERVAL_S:
            return

        # 航点已用完
        if ctx.current_wp_index >= len(ctx.waypoints):
            ctx.state = NavState.ARRIVED
            publish_command(device_id, {"cmd": "stop"})
            logger.info(f'[NAV] 到达终点: device={device_id}')
            return

        # 当前位置
        lat = data.get('latitude')
        lng = data.get('longitude')
        if lat is None or lng is None:
            return  # 无GPS，无法导航

        # 目标航点
        target_wp = ctx.waypoints[ctx.current_wp_index]

        # 计算距离和方位
        distance, bearing = self._calc_distance_bearing(
            lat, lng, target_wp.lat, target_wp.lng)

        # 到达判定
        if distance < ARRIVAL_RADIUS_M:
            ctx.current_wp_index += 1
            logger.info(
                f'[NAV] 到达航点 {ctx.current_wp_index}/'
                f'{len(ctx.waypoints)}: device={device_id}')

            if ctx.current_wp_index >= len(ctx.waypoints):
                ctx.state = NavState.ARRIVED
                publish_command(device_id, {"cmd": "stop"})
                return
            # 继续前往下一航点
            target_wp = ctx.waypoints[ctx.current_wp_index]
            _, bearing = self._calc_distance_bearing(
                lat, lng, target_wp.lat, target_wp.lng)

        # PID 计算
        steering = ctx.pid.compute(bearing, heading, dt=0.5)

        # 差速输出
        pwm_l = int(max(0, min(255, BASE_CRUISE_SPEED - steering)))
        pwm_r = int(max(0, min(255, BASE_CRUISE_SPEED + steering)))

        cmd = {
            "cmd": "diff",
            "pwm_l": pwm_l,
            "pwm_r": pwm_r,
            "dur_ms": 0  # 持续到下一条指令
        }

        publish_command(device_id, cmd)
        ctx.last_command_time = now

    @staticmethod
    def _calc_distance_bearing(lat1: float, lng1: float,
                               lat2: float, lng2: float):
        """Haversine 距离 + 方位角计算"""
        d_lat = math.radians(lat2 - lat1)
        d_lng = math.radians(lng2 - lng1)

        a = (math.sin(d_lat / 2) ** 2 +
             math.cos(math.radians(lat1)) *
             math.cos(math.radians(lat2)) *
             math.sin(d_lng / 2) ** 2)
        c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a))
        distance = EARTH_RADIUS_M * c

        y = math.sin(d_lng) * math.cos(math.radians(lat2))
        x = (math.cos(math.radians(lat1)) *
             math.sin(math.radians(lat2)) -
             math.sin(math.radians(lat1)) *
             math.cos(math.radians(lat2)) *
             math.cos(d_lng))
        bearing = math.degrees(math.atan2(y, x)) % 360

        return distance, bearing


# ── 全局单例 ──────────────────────────────────────────────────

nav_engine = NavigationEngine()
```

### 4.6 `mqtt_client.py` 修改点

在 `_process_telemetry()` 末尾增加导航引擎回调：

```python
# 在 _process_telemetry() 的 db.session.commit() 之后添加:

# ── 8. 触发导航决策引擎 ────────────────────────────────────
from src.services.navigation_engine import nav_engine
nav_engine.on_telemetry(msg_device_id, data)
```

### 4.7 `vehicle.py` 新增接口

```python
# 新增: 启动/停止自动巡航

@vehicle_bp.post('/cruise/start')
@jwt_required()
@require_device_ownership
def start_cruise():
    """启动自动巡航"""
    data = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    waypoints = data.get('waypoints', [])

    # 安全校验 (复用 cruise_security)
    # ...

    from src.services.navigation_engine import nav_engine
    result = nav_engine.start_cruise(device_id, waypoints)

    if result['success']:
        return jsonify(result), 200
    else:
        return jsonify({'error': result['message']}), 400


@vehicle_bp.post('/cruise/stop')
@jwt_required()
@require_device_ownership
def stop_cruise():
    """停止自动巡航"""
    data = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()

    from src.services.navigation_engine import nav_engine
    result = nav_engine.stop_cruise(device_id)
    return jsonify(result), 200


@vehicle_bp.get('/cruise/status/<string:device_id>')
@jwt_required()
def get_cruise_status_v2(device_id: str):
    """获取导航引擎状态 (含 IMU 融合结果)"""
    from src.services.navigation_engine import nav_engine
    status = nav_engine.get_status(device_id)
    return jsonify(status), 200
```

---

## 五、UNO 固件改造

### 5.1 改造原则

| 保留 | 移除 |
|------|------|
| 传感器采集 (DHT/HC-SR04/IR/GPS) | PID 控制器 |
| 电机驱动 (forward/backward/left/right/stop) | 航点导航循环 |
| ESP-01S MQTT 通信 | 避障绕行状态机 |
| XOR 加密 + 签名 | IMU 航向积分 (heading) |
| NMEA 解析 → 经纬度 | 电池低电量决策 |
| 心跳上报 | Haversine 距离计算 |

### 5.2 传感器 JSON 构建变更

**移除**：`imu_heading` 计算（不再做 gyroZ 积分）

**新增**：原始6轴数据 + 电池电压 + 红外拆分

```c
// 旧代码 (smartrover_uno.ino):
n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
  "\"imu_heading\":%.1f,", imuHeading);

// 新代码: 发送原始6轴数据
int16_t accel[3], gyro[3];
mpuReadBurst(0x3B, accel, 3);  // 加速度
mpuReadBurst(0x43, gyro, 3);   // 角速度

float ax = accel[0] / 16384.0;  // ±2g
float ay = accel[1] / 16384.0;
float az = accel[2] / 16384.0;
float gx = (gyro[0] - gyroZOff) / 16.4;  // ±2000°/s
float gy = (gyro[1]) / 16.4;
float gz = (gyro[2] - gyroZOff) / 16.4;

n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
  "\"imu_ax\":%.2f,\"imu_ay\":%.2f,\"imu_az\":%.2f,"
  "\"imu_gx\":%.1f,\"imu_gy\":%.1f,\"imu_gz\":%.1f,",
  ax, ay, az, gx, gy, gz);

// 红外拆分
n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
  "\"ir_l\":%d,\"ir_r\":%d,", irL ? 1 : 0, irR ? 1 : 0);

// 电池电压 (如果有 ADC 引脚)
int batRaw = analogRead(A7);  // 假设用 A7
int batMv = (int)(batRaw / 1023.0 * 5000.0 * (100.0 + 10.0) / 10.0);
n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
  "\"bat_mv\":%d,", batMv);

// 消息序号
n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
  "\"seq\":%d,", msgSeq++);
```

### 5.3 电机指令解析新增 diff 模式

```c
void executeCommand(const char* json) {
    // 解析 cmd 字段
    char cmd[16] = {0};
    extractJsonString(json, "command", cmd, sizeof(cmd));

    if (strcmp(cmd, "diff") == 0) {
        // 差速模式: 分别控制左右电机
        int pwmL = extractJsonInt(json, "pwm_l");
        int pwmR = extractJsonInt(json, "pwm_r");

        if (pwmL > 0) {
            motorLF.setSpeed(pwmL); motorLF.run(FORWARD);
            motorLB.setSpeed(pwmL); motorLB.run(FORWARD);
        } else if (pwmL < 0) {
            motorLF.setSpeed(-pwmL); motorLF.run(BACKWARD);
            motorLB.setSpeed(-pwmL); motorLB.run(BACKWARD);
        } else {
            motorLF.run(RELEASE); motorLB.run(RELEASE);
        }

        if (pwmR > 0) {
            motorRF.setSpeed(pwmR); motorRF.run(FORWARD);
            motorRB.setSpeed(pwmR); motorRB.run(FORWARD);
        } else if (pwmR < 0) {
            motorRF.setSpeed(-pwmR); motorRF.run(BACKWARD);
            motorRB.setSpeed(-pwmR); motorRB.run(BACKWARD);
        } else {
            motorRF.run(RELEASE); motorRB.run(RELEASE);
        }
    }
    else if (strcmp(cmd, "forward") == 0)  { fwd(extractJsonInt(json, "pwm")); }
    else if (strcmp(cmd, "backward") == 0) { bwd(extractJsonInt(json, "pwm")); }
    else if (strcmp(cmd, "left") == 0)     { rotL(extractJsonInt(json, "pwm")); }
    else if (strcmp(cmd, "right") == 0)    { rotR(extractJsonInt(json, "pwm")); }
    else if (strcmp(cmd, "stop") == 0)     { stopMotors(); }
}
```

### 5.4 心跳超时自动停车保护

```c
// 新增: 如果超过3个遥测周期未收到指令，自动停车
#define CMD_TIMEOUT_MS 15000  // 3 × 5s = 15s

unsigned long lastCmdTime = 0;

void loop() {
    unsigned long now = millis();

    // ... 传感器采集、MQTT 通信 ...

    // 心跳超时保护
    if (lastCmdTime > 0 && (now - lastCmdTime > CMD_TIMEOUT_MS)) {
        stopMotors();
        Serial.println(F("[SAFETY] 指令超时，自动停车"));
        lastCmdTime = 0;
    }
}

// 收到指令时更新时间戳
void onCommandReceived() {
    lastCmdTime = millis();
}
```

---

## 六、MQTT 主题与消息流汇总

### 6.1 主题定义

| 方向 | 主题 | QoS | 说明 |
|------|------|-----|------|
| UNO → 后端 | `sensor/<device_id>` | 1 | 传感器遥测 (XOR加密) |
| UNO → 后端 | `heartbeat/<device_id>` | 1 | 心跳 (XOR加密) |
| UNO → 后端 | `nav/<device_id>` | 1 | 导航事件 |
| 后端 → UNO | `cmd/<device_id>` | 1 | 控制指令 (明文JSON) |

### 6.2 消息流时序

```
时间轴 (自动巡航模式):

  UNO                          Backend
   │                              │
   │── sensor/xxx (遥测) ────────→│ 解密 → IMU融合 → 避障检查
   │                              │ → PID计算 → 差速指令
   │←── cmd/xxx (diff指令) ───────│
   │                              │
   │── sensor/xxx (遥测) ────────→│ 同上循环
   │←── cmd/xxx (diff指令) ───────│
   │                              │
   │── sensor/xxx (障碍!) ───────→│ 避障: 紧急停车
   │←── cmd/xxx (stop) ──────────│
   │                              │
   │── sensor/xxx (障碍清除) ────→│ 恢复巡航
   │←── cmd/xxx (diff指令) ───────│
```

### 6.3 频率与延迟

| 场景 | 遥测频率 | 指令频率 | 端到端延迟 |
|------|---------|---------|-----------|
| 手动控制 | 5s | 按需 | <100ms |
| 自动巡航 | 2~5s | 0.5~1s | <200ms |
| 紧急避障 | 5s | 即时 | <100ms |

> **建议**：巡航模式下将 UNO 遥测周期从 5s 缩短至 2s，提升闭环控制精度。

---

## 七、数据库变更

### 7.1 telemetry_points 表新增字段

```sql
ALTER TABLE telemetry_points
  ADD COLUMN imu_ax FLOAT COMMENT '加速度X(g)' AFTER imu_gyro_z,
  ADD COLUMN imu_ay FLOAT COMMENT '加速度Y(g)' AFTER imu_ax,
  ADD COLUMN imu_az FLOAT COMMENT '加速度Z(g)' AFTER imu_ay,
  ADD COLUMN imu_gx FLOAT COMMENT '角速度X(°/s)' AFTER imu_az,
  ADD COLUMN imu_gy FLOAT COMMENT '角速度Y(°/s)' AFTER imu_gx,
  ADD COLUMN bat_mv INT COMMENT '电池电压(mV)' AFTER satellites,
  ADD COLUMN seq INT COMMENT '消息序号' AFTER bat_mv;
```

### 7.2 TelemetryPoint 模型更新

在 `src/models/telemetry.py` 中新增对应字段和 `to_dict()` 输出。

---

## 八、配置参数

在 `config.py` 中新增导航引擎参数：

```python
# ── 导航引擎参数 ──────────────────────────────────────────
NAV_PID_KP = float(os.getenv('NAV_PID_KP', '2.5'))
NAV_PID_KI = float(os.getenv('NAV_PID_KI', '0.02'))
NAV_PID_KD = float(os.getenv('NAV_PID_KD', '0.8'))
NAV_ARRIVAL_RADIUS_M = float(os.getenv('NAV_ARRIVAL_RADIUS_M', '3.0'))
NAV_BASE_CRUISE_SPEED = int(os.getenv('NAV_BASE_CRUISE_SPEED', '160'))
NAV_COMMAND_INTERVAL_S = float(os.getenv('NAV_COMMAND_INTERVAL_S', '1.0'))
NAV_SAFE_DISTANCE_CM = float(os.getenv('NAV_SAFE_DISTANCE_CM', '50.0'))
NAV_CRITICAL_DISTANCE_CM = float(os.getenv('NAV_CRITICAL_DISTANCE_CM', '20.0'))
NAV_LOW_BATTERY_MV = int(os.getenv('NAV_LOW_BATTERY_MV', '10500'))
NAV_CRITICAL_BATTERY_MV = int(os.getenv('NAV_CRITICAL_BATTERY_MV', '10000'))
```

---

## 九、优势与风险

### 9.1 优势

| 维度 | 说明 |
|------|------|
| **固件简化** | UNO 代码量减少 ~50%，内存释放，稳定性提升 |
| **算法迭代** | 后端热更新，无需刷固件，支持在线调参 |
| **多设备一致** | 所有设备共享同一套算法，行为统一 |
| **可观测性** | 完整决策日志，便于调试和审计 |
| **Web 调参** | PID 参数、安全距离等可通过前端实时调整 |
| **安全增强** | 所有决策经过后端安全校验，防篡改 |

### 9.2 风险与对策

| 风险 | 影响 | 对策 |
|------|------|------|
| 网络延迟 | PID 控制精度下降 | 局域网 MQTT <50ms；巡航时缩短遥测周期至 2s |
| 断连失控 | 小车失控 | UNO 心跳超时 15s 自动停车 |
| 遥测频率不足 | 闭环控制慢 | 巡航模式加速上报至 2s |
| 后端单点故障 | 所有设备失控 | 后端高可用部署 + UNO 安全停车保护 |

---

## 十、实施步骤

| 阶段 | 任务 | 涉及文件 | 优先级 |
|------|------|---------|--------|
| **P1** | 新增后端导航引擎4个模块 | `services/navigation_engine.py`, `pid_controller.py`, `imu_fusion.py`, `obstacle_avoidance.py` | 高 |
| **P2** | 修改 mqtt_client.py 接入导航引擎 | `utils/mqtt_client.py` | 高 |
| **P3** | 新增巡航控制 API | `routes/vehicle.py` | 高 |
| **P4** | 更新 TelemetryPoint 模型 | `models/telemetry.py` + SQL migration | 中 |
| **P5** | UNO 固件：传感器 JSON 增加原始 IMU + 电池 | `Firmware/smartrover_uno.ino` | 中 |
| **P6** | UNO 固件：电机支持 diff 差速指令 | `Firmware/smartrover_uno.ino` | 中 |
| **P7** | UNO 固件：心跳超时自动停车 | `Firmware/smartrover_uno.ino` | 中 |
| **P8** | UNO 固件：移除 PID/导航/避障代码 | `Firmware/smartrover_uno.ino` | 低 |
| **P9** | 联调测试：手动差速 → 自动巡航闭环 | 全链路 | 高 |
| **P10** | 前端：巡航控制面板 + PID 调参界面 | `Frontend/src/` | 低 |

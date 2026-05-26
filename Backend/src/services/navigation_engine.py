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
BASE_CRUISE_SPEED = 200
LOW_BATTERY_MV = 10500
CRITICAL_BATTERY_MV = 10000
COMMAND_INTERVAL_S = 0.3


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
    cruise_speed: int = BASE_CRUISE_SPEED


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
                     waypoints: List[Dict],
                     speed_pwm: int = 0) -> dict:
        """
        启动巡航。

        Args:
            device_id: 设备ID
            waypoints: [{"lat": 39.9, "lng": 116.4}, ...]  可选，为空则自由巡航
            speed_pwm: 巡航速度 PWM，0 则使用默认值

        Returns:
            {"success": bool, "message": str}
        """
        ctx = self._get_context(device_id)
        ctx.pid.reset()
        ctx.cruise_start_time = time.time()
        ctx.state = NavState.CRUISING

        if speed_pwm and 50 <= speed_pwm <= 255:
            ctx.cruise_speed = speed_pwm
        else:
            ctx.cruise_speed = BASE_CRUISE_SPEED

        if waypoints and len(waypoints) >= 2:
            ctx.waypoints = [Waypoint(w['lat'], w['lng']) for w in waypoints]
            ctx.current_wp_index = 0
            ctx.last_command_time = time.time()
            logger.info(
                f'[NAV] 航点巡航启动: device={device_id} '
                f'waypoints={len(ctx.waypoints)}')
            return {"success": True,
                    "message": f"巡航启动，{len(ctx.waypoints)}个航点"}
        else:
            ctx.waypoints = []
            ctx.current_wp_index = 0
            ctx.last_command_time = time.time()
            logger.info(
                f'[NAV] 自由巡航启动: device={device_id} (无航点，仅避障)')
            return {"success": True,
                    "message": "自由巡航启动（仅避障模式）"}

    def stop_cruise(self, device_id: str) -> dict:
        """中止巡航"""
        ctx = self._get_context(device_id)
        ctx.state = NavState.IDLE
        ctx.pid.reset()
        ctx.waypoints = []
        ctx.current_wp_index = 0

        # 立即下发停止指令
        self._publish(device_id, {"cmd": "stop"})

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
        遥测数据回调。

        巡逻巡航模式下, ESP32 本地自主完成避障和巡航,
        后端只更新 IMU 状态和巡航状态, 不下发任何控制指令。
        当巡航状态变化时记录导航事件。
        """
        ctx = self._get_context(device_id)

        heading = self._update_imu(ctx, data)

        cruise_active = data.get('cruise_active', False)
        cruise_state = data.get('cruise_state', 'idle')
        avoid_state = data.get('avoid_state', 0)

        prev_state = ctx.state

        if cruise_active and cruise_state != 'idle':
            ctx.state = NavState.OBSTACLE_AVOID if cruise_state == 'avoiding' else NavState.CRUISING
        elif cruise_active:
            ctx.state = NavState.CRUISING
        elif prev_state in (NavState.CRUISING, NavState.OBSTACLE_AVOID):
            if ctx.cruise_start_time and (time.time() - ctx.cruise_start_time < 8):
                pass
            else:
                ctx.state = NavState.IDLE

        if ctx.state != prev_state:
            self._record_event(device_id, ctx.state, prev_state, data)

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

        return ctx.imu.update(ax, ay, az, gx, gy, gz,
                              gps_speed_kmh=gps_speed,
                              gps_course=gps_course)

    def _check_obstacle(self, ctx: DeviceNavContext,
                        data: dict) -> Optional[dict]:
        """避障检查"""
        us_cm = data.get('ultrasonic_cm')
        # 兼容新旧格式: ir_l/ir_r (新) 或 ir_obstacle (旧)
        ir_l = data.get('ir_l', 0)
        ir_r = data.get('ir_r', 0)
        if not ir_l and not ir_r:
            ir_obstacle = data.get('ir_obstacle')
            if ir_obstacle is True or ir_obstacle == 'true':
                ir_l = 1
                ir_r = 1

        return ctx.avoider.get_avoid_command(us_cm, ir_l, ir_r)

    def _cruise_step(self, device_id: str, ctx: DeviceNavContext,
                     data: dict, heading: float):
        """单步巡航决策"""
        now = time.time()

        # 指令下发频率限制
        if now - ctx.last_command_time < COMMAND_INTERVAL_S:
            return

        # 自由巡航模式（无航点）：直行 + 避障
        if not ctx.waypoints:
            cmd = {"cmd": "diff", "pwm_l": ctx.cruise_speed,
                   "pwm_r": ctx.cruise_speed, "dur_ms": 0}
            self._publish(device_id, cmd)
            ctx.last_command_time = now
            return

        # 航点巡航模式
        if ctx.current_wp_index >= len(ctx.waypoints):
            ctx.state = NavState.ARRIVED
            self._publish(device_id, {"cmd": "stop"})
            logger.info(f'[NAV] 到达终点: device={device_id}')
            return

        # 当前位置
        lat = data.get('latitude')
        lng = data.get('longitude')
        if lat is None or lng is None:
            return

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
                self._publish(device_id, {"cmd": "stop"})
                return
            target_wp = ctx.waypoints[ctx.current_wp_index]
            _, bearing = self._calc_distance_bearing(
                lat, lng, target_wp.lat, target_wp.lng)

        # PID 计算
        steering = ctx.pid.compute(bearing, heading, dt=0.5)

        # 差速输出
        pwm_l = int(max(0, min(255, ctx.cruise_speed - steering)))
        pwm_r = int(max(0, min(255, ctx.cruise_speed + steering)))

        cmd = {
            "cmd": "diff",
            "pwm_l": pwm_l,
            "pwm_r": pwm_r,
        }

        self._publish(device_id, cmd)
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

    @staticmethod
    def _publish(device_id: str, payload: dict):
        """发布指令到设备"""
        publish_command(device_id, payload)

    @staticmethod
    def _record_event(device_id: str, new_state: str,
                      prev_state: str, data: dict):
        """根据巡航状态变化记录导航事件"""
        from src.extensions import db
        from src.models.navigation_event import NavigationEvent

        event_type = None
        detail = None

        if prev_state == NavState.IDLE and new_state == NavState.CRUISING:
            event_type = 'CRUISE_STARTED'
            detail = '巡逻巡航启动'
        elif prev_state == NavState.CRUISING and new_state == NavState.OBSTACLE_AVOID:
            event_type = 'OBSTACLE_DETECTED'
            us_cm = data.get('ultrasonic_cm')
            ir_l = data.get('ir_l', 0)
            ir_r = data.get('ir_r', 0)
            detail = f'检测到障碍 us={us_cm}cm ir_l={ir_l} ir_r={ir_r}'
        elif prev_state == NavState.OBSTACLE_AVOID and new_state == NavState.CRUISING:
            event_type = 'OBSTACLE_CLEARED'
            detail = '障碍已清除，恢复巡航'
        elif new_state == NavState.IDLE and prev_state in (NavState.CRUISING, NavState.OBSTACLE_AVOID):
            event_type = 'ABORTED'
            detail = '巡航已停止'

        if not event_type:
            return

        lat = data.get('latitude')
        lng = data.get('longitude')

        evt = NavigationEvent(
            device_id=device_id,
            event_type=event_type,
            detail=detail,
            lat=lat,
            lng=lng,
            state=new_state,
        )
        db.session.add(evt)
        try:
            db.session.commit()
            logger.info(f'[NAV_EVENT] {event_type}: device={device_id} {detail}')
        except Exception as e:
            db.session.rollback()
            logger.warning(f'[NAV_EVENT] Failed to save: {e}')


# ── 全局单例 ──────────────────────────────────────────────────

nav_engine = NavigationEngine()

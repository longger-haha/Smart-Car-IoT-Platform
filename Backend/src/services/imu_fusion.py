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

from src.services import normalize_angle


class IMUState:
    """单个设备的 IMU 融合状态"""

    COMP_ALPHA = 0.98  # 互补滤波系数

    def __init__(self):
        self.heading = 0.0       # 当前航向角 (0~360°)
        self.pitch = 0.0
        self.roll = 0.0
        self.gyro_z_dps = 0.0   # Z轴角速度 (°/s)
        self.last_update = None  # 上次更新时间戳
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
            dt = 0.05
        self.last_update = now

        # 1. 陀螺仪积分
        self.gyro_z_dps = gz
        gyro_heading_delta = gz * dt

        # 2. 加速度计姿态
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
            diff = normalize_angle(gps_course - self.heading)
            self.heading += blend * diff * 0.1

        self.heading %= 360.0
        if self.heading < 0:
            self.heading += 360.0

        return self.heading

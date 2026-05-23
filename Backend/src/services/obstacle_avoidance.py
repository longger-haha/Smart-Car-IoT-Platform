"""
obstacle_avoidance.py — 后端避障决策引擎

替代 ESP32/UNO 端的避障逻辑。
根据超声波和红外数据生成避障指令。
"""

import logging
import time
from typing import Optional

logger = logging.getLogger(__name__)


class ObstacleAvoidance:
    """避障决策引擎"""

    SAFE_DISTANCE_CM = 50.0
    CRITICAL_DISTANCE_CM = 20.0
    AVOID_COOLDOWN_S = 3.0

    def __init__(self):
        self._last_avoid_time = 0.0

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
            return None

        now = time.time()

        # 紧急停车
        if status == "critical":
            logger.warning(
                f'[AVOID] 紧急停车! us={ultrasonic_cm}cm '
                f'ir_l={ir_l} ir_r={ir_r}')
            return {"cmd": "stop"}

        # 避障绕行
        if now - self._last_avoid_time < self.AVOID_COOLDOWN_S:
            return None

        self._last_avoid_time = now

        if ir_l and not ir_r:
            return {"cmd": "diff", "pwm_l": 180, "pwm_r": 80}
        elif ir_r and not ir_l:
            return {"cmd": "diff", "pwm_l": 80, "pwm_r": 180}
        else:
            return {"cmd": "diff", "pwm_l": -120, "pwm_r": -120}

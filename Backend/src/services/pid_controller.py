"""
pid_controller.py — 后端 PID 航向控制器

替代 ESP32 端的 computePIDSteering()。
输入目标方位角和当前航向，输出差速转向量。
"""

from src.services import normalize_angle


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
        error = normalize_angle(target_bearing - current_heading)

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

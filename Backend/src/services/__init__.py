# Backend/src/services/__init__.py


def normalize_angle(angle: float) -> float:
    """将角度归一化到 [-180, 180) 范围"""
    while angle > 180:
        angle -= 360
    while angle < -180:
        angle += 360
    return angle

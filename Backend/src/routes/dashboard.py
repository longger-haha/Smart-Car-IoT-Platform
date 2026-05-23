"""
dashboard.py — 平台统计概览路由蓝图

GET /api/dashboard/stats  → 平台总览统计数据（设备数/在线数/告警数/最新遥测）

为前端 Dashboard 首页提供一站式聚合数据接口。
"""

from flask import Blueprint, jsonify
from flask_jwt_extended import jwt_required, get_jwt, get_jwt_identity
from sqlalchemy import func

from src.extensions import db
from src.models.device import Device
from src.models.user import User
from src.models.telemetry import TelemetryPoint
from src.models.audit_log import SecurityAuditLog

dashboard_bp = Blueprint('dashboard', __name__)

# 超声波安全距离 (cm) — 与 devices.py 保持一致
ULTRASONIC_SAFE_DISTANCE_CM = 50


@dashboard_bp.get('/stats')
@jwt_required()
def get_stats():
    """
    获取平台统计概览

    Response 200:
        {
            "total_devices":       5,
            "online_devices":      2,
            "offline_devices":     3,
            "total_audit_logs":    128,
            "critical_alerts":     3,
            "collision_warnings":  1,
            "safe_distance_cm":    50,
            "recent_alerts": [
                { "id": 128, "event_type": "replay", ... },
                ...
            ]
        }
    """
    claims = get_jwt()
    is_admin = claims.get('role') == 'admin'
    username = get_jwt_identity()
    user = User.query.filter_by(username=username).first()

    if not user:
        return jsonify({'error': 'User not found'}), 404

    # Determine base queries for devices and audit logs
    device_query = Device.query
    audit_query = SecurityAuditLog.query

    if not is_admin:
        device_query = device_query.filter_by(user_id=user.id)
        # Find device IDs owned by this user
        owned_devices = [d.device_id for d in device_query.all()]
        audit_query = audit_query.filter(SecurityAuditLog.target_device_id.in_(owned_devices))

    total_devices  = device_query.count()
    online_devices = device_query.filter_by(status='online').count()

    total_audit = audit_query.count()

    # 严重告警数 (replay + ddos)
    critical_count = audit_query.filter(SecurityAuditLog.event_type.in_(['replay', 'ddos'])).count()

    # 安全指数：基于每台设备实际超声波距离的连续评分
    # 距离 → 安全分映射: 5cm=0分, 50cm=31分, 150cm=100分
    CRITICAL_DIST_CM = 5.0    # 视为碰撞 (0分)
    SAFE_DIST_CM     = 150.0  # 完全安全 (100分)
    DANGER_DIST_CM   = 30.0   # 危险阈值 (红色警告)

    online_device_ids = [d.device_id for d in device_query.filter_by(status='online').all()]
    device_scores = []
    ultrasonic_values = []
    collision_count = 0
    danger_count = 0
    min_distance = None
    min_distance_device = None

    for did in online_device_ids:
        latest = (
            TelemetryPoint.query
            .filter_by(device_id=did)
            .order_by(TelemetryPoint.recorded_at.desc())
            .first()
        )
        if latest and latest.ultrasonic_cm is not None:
            d = latest.ultrasonic_cm
            ultrasonic_values.append(d)
            if d < ULTRASONIC_SAFE_DISTANCE_CM:
                collision_count += 1
            if d < DANGER_DIST_CM:
                danger_count += 1
            if min_distance is None or d < min_distance:
                min_distance = d
                min_distance_device = did
            # 连续评分: 距离在 CRITICAL~SAFE 之间线性映射到 0~100
            score = max(0.0, min(100.0, (d - CRITICAL_DIST_CM) / (SAFE_DIST_CM - CRITICAL_DIST_CM) * 100.0))
            device_scores.append(score)

    # 总体安全指数 = 所有在线设备安全分的平均值 (没有设备则 100)
    safety_index = round(sum(device_scores) / len(device_scores)) if device_scores else 100
    avg_ultrasonic = round(sum(ultrasonic_values) / len(ultrasonic_values), 1) if ultrasonic_values else None

    # 最近 5 条告警日志
    recent_alerts = (
        audit_query
        .order_by(SecurityAuditLog.occurred_at.desc())
        .limit(5)
        .all()
    )

    return jsonify({
        'total_devices':        total_devices,
        'online_devices':       online_devices,
        'offline_devices':      total_devices - online_devices,
        'total_audit_logs':     total_audit,
        'critical_alerts':      critical_count,
        'collision_warnings':   collision_count,
        'safety_index':         safety_index,
        'avg_ultrasonic_cm':    avg_ultrasonic,
        'min_ultrasonic_cm':    round(min_distance, 1) if min_distance is not None else None,
        'min_distance_device':  min_distance_device,
        'danger_count':         danger_count,
        'safe_distance_cm':     ULTRASONIC_SAFE_DISTANCE_CM,
        'recent_alerts':        [a.to_dict() for a in recent_alerts],
    }), 200

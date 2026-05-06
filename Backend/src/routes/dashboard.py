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

    # 碰撞预警：检查所有在线设备的最新超声波数据
    collision_count = 0
    online_device_ids = [d.device_id for d in device_query.filter_by(status='online').all()]
    for did in online_device_ids:
        latest = (
            TelemetryPoint.query
            .filter_by(device_id=did)
            .order_by(TelemetryPoint.recorded_at.desc())
            .first()
        )
        if latest and latest.ultrasonic_cm is not None:
            if latest.ultrasonic_cm < ULTRASONIC_SAFE_DISTANCE_CM:
                collision_count += 1

    # 最近 5 条告警日志
    recent_alerts = (
        audit_query
        .order_by(SecurityAuditLog.occurred_at.desc())
        .limit(5)
        .all()
    )

    return jsonify({
        'total_devices':       total_devices,
        'online_devices':      online_devices,
        'offline_devices':     total_devices - online_devices,
        'total_audit_logs':    total_audit,
        'critical_alerts':     critical_count,
        'collision_warnings':  collision_count,
        'safe_distance_cm':    ULTRASONIC_SAFE_DISTANCE_CM,
        'recent_alerts':       [a.to_dict() for a in recent_alerts],
    }), 200

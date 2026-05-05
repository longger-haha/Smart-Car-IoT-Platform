"""
telemetry.py — 遥测数据路由蓝图

GET /api/telemetry/latest/<device_id>              → 最新一条遥测数据
GET /api/telemetry/history/<device_id>?limit=50   → 历史遥测数据

T024 [US2]: 遥测查询接口
"""

from flask import Blueprint, request, jsonify
from flask_jwt_extended import jwt_required

from src.models.device import Device
from src.models.telemetry import TelemetryPoint
from src.utils.auth_interceptor import require_device_ownership

telemetry_bp = Blueprint('telemetry', __name__)

DEFAULT_HISTORY_LIMIT = 50
MAX_HISTORY_LIMIT     = 200


@telemetry_bp.get('/latest/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_latest(device_id: str):
    """
    获取指定设备的最新一条遥测数据

    Response 200:
        {
            "device_id":    "aa:bb:cc:dd:ee:ff",
            "latitude":     39.9042,
            "longitude":    116.4074,
            "temperature":  26.5,
            "humidity":     65.0,
            "ultrasonic_cm": 120.3,
            "speed_pwm":    200,
            "raw_ciphertext": "...",
            "recorded_at":  "2026-05-04T10:00:00"
        }

    Response 404:
        { "error": "设备不存在或暂无数据" }
    """
    # 确认设备存在
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': f'设备 {device_id!r} 不存在'}), 404

    point = (
        TelemetryPoint.query
        .filter_by(device_id=device_id)
        .order_by(TelemetryPoint.recorded_at.desc())
        .first()
    )
    if not point:
        return jsonify({'error': f'设备 {device_id!r} 暂无遥测数据'}), 404

    return jsonify(point.to_dict()), 200


@telemetry_bp.get('/history/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_history(device_id: str):
    """
    获取指定设备的历史遥测数据（最新的在前）

    Query Params:
        limit (int): 返回条数，默认 50，最大 200

    Response 200:
        {
            "device_id": "aa:bb:cc:dd:ee:ff",
            "total": 50,
            "records": [ {...}, ... ]
        }
    """
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': f'设备 {device_id!r} 不存在'}), 404

    try:
        limit = min(int(request.args.get('limit', DEFAULT_HISTORY_LIMIT)), MAX_HISTORY_LIMIT)
    except (ValueError, TypeError):
        limit = DEFAULT_HISTORY_LIMIT

    points = (
        TelemetryPoint.query
        .filter_by(device_id=device_id)
        .order_by(TelemetryPoint.recorded_at.desc())
        .limit(limit)
        .all()
    )

    return jsonify({
        'device_id': device_id,
        'total':     len(points),
        'records':   [p.to_dict() for p in points],
    }), 200

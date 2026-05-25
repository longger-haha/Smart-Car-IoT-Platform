"""
vehicle.py — 远程指令反控 + 后端导航引擎 + 轨迹查询

POST /api/vehicle/command             → 下发控制指令 (含 diff 差速)
POST /api/vehicle/cruise/start        → 启动后端自动巡航
POST /api/vehicle/cruise/stop         → 停止自动巡航
GET  /api/vehicle/cruise/status/<id>  → 导航引擎状态 (含 IMU 融合)
GET  /api/vehicle/position/<device_id>→ 查询设备最新GPS位置
GET  /api/vehicle/nav-events/<id>     → 查询导航事件时间线
GET  /api/vehicle/trajectory/<id>     → 查询历史轨迹坐标点
GET  /api/vehicle/risk/<device_id>    → 设备安全风险评分
"""

import time
import logging
from datetime import datetime, timedelta, timezone

from flask import Blueprint, request, jsonify
from sqlalchemy import desc, func

from src.extensions import db
from src.models.navigation_event import NavigationEvent
from src.models.telemetry import TelemetryPoint
from src.utils.auth_interceptor import require_device_ownership
from src.utils.mqtt_client import publish_command
from src.utils.cruise_security import get_device_risk_score
from src.services.navigation_engine import nav_engine
from flask_jwt_extended import jwt_required

vehicle_bp = Blueprint('vehicle', __name__)

VALID_COMMANDS = {'forward', 'backward', 'left', 'right', 'stop', 'diff', 'config'}
MAX_WAYPOINTS = 50
MIN_WAYPOINTS = 2


@vehicle_bp.post('/command')
@jwt_required()
@require_device_ownership
def send_command():
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    command   = data.get('command',   '').strip().lower()

    log = logging.getLogger(__name__)

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400
    if command not in VALID_COMMANDS:
        return jsonify({
            'error': f'非法指令 {command!r}，合法指令: {sorted(VALID_COMMANDS)}',
        }), 400

    # diff 差速指令: 由前端或导航引擎下发
    if command == 'diff':
        payload = {
            'cmd':   'diff',
            'pwm_l': data.get('pwm_l', 0),
            'pwm_r': data.get('pwm_r', 0),
        }
    elif command == 'config':
        params = data.get('params', {})
        if not params:
            return jsonify({'error': 'config 指令必须包含 params 字段'}), 400
        allowed_keys = {
            'speed_pwm', 'telemetry_ms', 'critical_cm', 'warn_cm', 'safe_cm',
            'backward_timeout_ms', 'turn_timeout_ms', 'max_probe_retries',
            'avoid_enabled', 'wifi_scan_max_aps',
        }
        filtered = {k: v for k, v in params.items() if k in allowed_keys}
        if not filtered:
            return jsonify({'error': f'params 中无合法参数，合法键: {sorted(allowed_keys)}'}), 400
        payload = {
            'cmd':    'config',
            'params': filtered,
        }
    else:
        payload = {
            'command':   command,
            'speed_pwm': data.get('speed_pwm', 150),
        }

    success = publish_command(device_id, payload)

    if not success:
        return jsonify({'error': 'MQTT 发布失败'}), 503

    return jsonify({
        'message':   '指令已下发',
        'device_id': device_id,
        'command':   command,
        'speed_pwm': payload.get('speed_pwm'),
    }), 200


# ══════════ 实时位置跟踪 ══════════


@vehicle_bp.get('/position/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_latest_position(device_id: str):
    """
    获取设备最新一条遥测数据（含GPS位置、传感器数据、导航状态）

    Response 200:
        {
            "lat": 39.9042, "lng": 116.4074,
            "altitude": 50.5, "speed_kmh": 2.3,
            "course": 180.5, "satellites": 10,
            "temperature": 26.5, "humidity": 65.0,
            "ultrasonic_cm": 120.3, "speed_pwm": 160,
            "nav_state": "cruising", "wp_index": 2, "wp_total": 5,
            "distance_to_wp": 15.6, "target_bearing": 90.2,
            "current_course": 88.5, "pid_output": -1.7,
            "recorded_at": "2026-05-06T10:00:00"
        }
    Response 404:
        { "error": "暂无位置数据" }
    """
    point = (
        TelemetryPoint.query
        .filter_by(device_id=device_id)
        .order_by(TelemetryPoint.recorded_at.desc())
        .first()
    )
    if not point:
        return jsonify({'error': f'设备 {device_id!r} 暂无位置数据'}), 404

    result = point.to_dict()

    # 前端使用 lat/lng 短名称, 添加别名兼容
    if result.get('latitude') is not None:
        result['lat'] = float(result['latitude'])
    if result.get('longitude') is not None:
        result['lng'] = float(result['longitude'])

    # 附加设备在线状态
    from src.models.device import Device
    device = Device.query.filter_by(device_id=device_id).first()
    result['device_online'] = device.status == 'online' if device else False
    result['last_seen_at'] = device.last_seen_at.isoformat() if device and device.last_seen_at else None

    # 附加导航引擎状态
    nav_status = nav_engine.get_status(device_id)
    result['nav_state']     = nav_status.get('state', 'idle')
    result['wp_index']      = nav_status.get('current_wp_index', 0)
    result['wp_total']      = nav_status.get('total_waypoints', 0)
    result['heading']       = nav_status.get('heading')
    result['gyro_z']        = nav_status.get('gyro_z')

    return jsonify(result), 200


# ══════════ 导航事件时间线 ══════════


@vehicle_bp.get('/nav-events/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_nav_events(device_id: str):
    """
    获取设备导航事件时间线（用于前端展示巡航过程）

    Query Params:
        event_type (str): 可选，筛选特定类型的事件
        limit (int): 返回条数，默认 50，最大 200

    Response 200:
        {
            "total": 25,
            "events": [
                {
                    "id": 1, "event_type": "CRUISE_STARTED",
                    "detail": "收到路线: 5个航点",
                    "lat": 39.9042, "lng": 116.4074,
                    "wp_index": 0, "wp_total": 5,
                    "state": "cruising",
                    "occurred_at": "2026-05-06T10:00:00"
                },
                ...
            ]
        }
    """
    event_type = request.args.get('event_type')
    try:
        limit = min(int(request.args.get('limit', 50)), 200)
    except (ValueError, TypeError):
        limit = 50

    query = NavigationEvent.query.filter_by(device_id=device_id)
    if event_type:
        query = query.filter_by(event_type=event_type.upper())

    events = (
        query
        .order_by(desc(NavigationEvent.occurred_at))
        .limit(limit)
        .all()
    )

    total = query.count()

    return jsonify({
        'total':  total,
        'events': [e.to_dict() for e in reversed(events)],
    }), 200


# ══════════ 历史轨迹查询 ══════════


@vehicle_bp.get('/trajectory/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_trajectory(device_id: str):
    """
    获取设备历史GPS轨迹坐标点（用于地图回放轨迹线）

    Query Params:
        hours (int): 查询最近N小时的数据，默认 1，最大 24
        limit (int): 最大返回点数，默认 500，最大 2000

    Response 200:
        {
            "device_id": "...",
            "total_points": 120,
            "bounds": { "min_lat": ..., max_lat, min_lng, max_lng },
            "points": [
                {"lat": 39.9042, "lng": 116.4074, "ts": "..."},
                ...
            ]
        }
    """
    try:
        hours = min(int(request.args.get('hours', 1)), 24)
        limit = min(int(request.args.get('limit', 500)), 2000)
    except (ValueError, TypeError):
        hours = 1
        limit = 500

    since = datetime.now(timezone.utc) - timedelta(hours=hours)

    points = (
        TelemetryPoint.query
        .filter(TelemetryPoint.device_id == device_id)
        .filter(TelemetryPoint.recorded_at >= since)
        .filter(TelemetryPoint.latitude.isnot(None))
        .filter(TelemetryPoint.longitude.isnot(None))
        .order_by(TelemetryPoint.recorded_at.asc())
        .limit(limit)
        .all()
    )

    if not points:
        return jsonify({
            'device_id':    device_id,
            'total_points': 0,
            'points':       [],
            'bounds':       None,
        }), 200

    lats = [float(p.latitude) for p in points]
    lngs = [float(p.longitude) for p in points]

    return jsonify({
        'device_id':    device_id,
        'total_points': len(points),
        'bounds': {
            'min_lat': min(lats), 'max_lat': max(lats),
            'min_lng': min(lngs), 'max_lng': max(lngs),
        },
        'points': [
            {
                'lat': float(p.latitude),
                'lng': float(p.longitude),
                'ts':  p.recorded_at.isoformat() if p.recorded_at else None,
                'speed_pwm': p.speed_pwm,
                'ultrasonic_cm': float(p.ultrasonic_cm) if p.ultrasonic_cm else None,
            }
            for p in points
        ],
    }), 200


# ══════════ 安全评估 ══════════


@vehicle_bp.get('/risk/<string:device_id>')
@jwt_required()
def get_device_risk(device_id: str):
    """
    获取设备安全风险评分

    Response 200:
        {
            "device_id": "...",
            "risk_score": 15,
            "risk_level": "低风险",
            "recent_event_count": 2,
            "factors": ["警告事件 x2"]
        }
    """
    risk = get_device_risk_score(device_id)
    return jsonify({
        'device_id': device_id,
        **risk,
    }), 200


# ══════════ 后端导航引擎控制 ══════════


@vehicle_bp.post('/cruise/start')
@jwt_required()
@require_device_ownership
def start_cruise():
    """
    启动后端自动巡航 (导航引擎接管)

    Request JSON:
        {
            "device_id": "...",
            "waypoints": [{"lat": 39.9, "lng": 116.4}, ...]  // 可选
        }

    Response 200:
        {"success": true, "message": "巡航启动，5个航点"}
    """
    data = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    waypoints = data.get('waypoints', [])
    speed_pwm = data.get('speed_pwm', 150)

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400

    # 航点可选：有航点则按航点巡航，无航点则自由巡航（仅避障）
    if waypoints:
        if not isinstance(waypoints, list) or len(waypoints) < MIN_WAYPOINTS:
            return jsonify({'error': f'航点数量不足，至少需要 {MIN_WAYPOINTS} 个'}), 400
        if len(waypoints) > MAX_WAYPOINTS:
            return jsonify({'error': f'航点超限，最多 {MAX_WAYPOINTS} 个'}), 400

        for i, wp in enumerate(waypoints):
            if not isinstance(wp, dict):
                return jsonify({'error': f'第 {i+1} 个航点格式错误'}), 400
            if 'lat' not in wp or 'lng' not in wp:
                return jsonify({'error': f'第 {i+1} 个航点缺少 lat/lng'}), 400

    result = nav_engine.start_cruise(device_id, waypoints, speed_pwm=speed_pwm)

    if result['success']:
        return jsonify(result), 200
    else:
        return jsonify({'error': result['message']}), 400


@vehicle_bp.post('/cruise/stop')
@jwt_required()
@require_device_ownership
def stop_cruise():
    """
    停止后端自动巡航

    Request JSON:
        {"device_id": "..."}

    Response 200:
        {"success": true, "message": "巡航已中止"}
    """
    data = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400

    result = nav_engine.stop_cruise(device_id)

    return jsonify(result), 200


@vehicle_bp.get('/cruise/status/<string:device_id>')
@jwt_required()
def get_cruise_status_v2(device_id: str):
    """
    获取导航引擎状态 (含 IMU 融合结果)

    Response 200:
        {
            "state": "cruising",
            "current_wp_index": 2,
            "total_waypoints": 5,
            "heading": 180.5,
            "gyro_z": 3.8
        }
    """
    status = nav_engine.get_status(device_id)
    return jsonify(status), 200

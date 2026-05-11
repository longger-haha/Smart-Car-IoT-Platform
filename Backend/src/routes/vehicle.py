"""
vehicle.py — 远程指令反控 + 路线规划 + 自动驾驶跟踪

POST /api/vehicle/command             → 下发控制指令
POST /api/vehicle/route               → 下发巡航路线
GET  /api/vehicle/route/<device_id>   → 查询最近下发的路线
GET  /api/vehicle/position/<device_id>→ 查询设备最新GPS位置
GET  /api/vehicle/nav-events/<device_id> → 查询导航事件时间线
GET  /api/vehicle/status/<device_id>  → 查询设备巡航状态
GET  /api/vehicle/trajectory/<device_id> → 查询历史轨迹坐标点

T029 [US3]: 指令下发接口，RBAC 拦截演示
T051 [US3]: 路线规划下发接口
T060 [US3]: 实时位置跟踪与自动驾驶状态监控
"""

import time
import json
from datetime import datetime, timedelta
from math import cos

from flask import Blueprint, request, jsonify
from sqlalchemy import desc, func

from src.extensions import db
from src.models.audit_log import SecurityAuditLog
from src.models.navigation_event import NavigationEvent
from src.models.telemetry import TelemetryPoint
from src.utils.auth_interceptor import jwt_required_with_rbac, require_device_ownership
from src.utils.mqtt_client import publish_command
from src.utils.cruise_security import (
    validate_cruise_command,
    generate_signature,
    record_abnormal_event,
    get_device_risk_score,
)
from flask_jwt_extended import jwt_required

vehicle_bp = Blueprint('vehicle', __name__)

VALID_COMMANDS = {'forward', 'backward', 'left', 'right', 'stop'}
MAX_WAYPOINTS = 50
MIN_WAYPOINTS = 2

_last_routes: dict = {}

_nav_status_cache: dict = {}


@vehicle_bp.post('/command')
@jwt_required()
@require_device_ownership
def send_command():
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    command   = data.get('command',   '').strip().lower()

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400
    if command not in VALID_COMMANDS:
        return jsonify({
            'error': f'非法指令 {command!r}，合法指令: {sorted(VALID_COMMANDS)}',
        }), 400

    payload = {
        'command':   command,
        'device_id': device_id,
        'timestamp': int(time.time()),
        'speed_pwm': data.get('speed_pwm', 150),
    }

    success = publish_command(device_id, payload)
    if not success:
        return jsonify({'error': 'MQTT 发布失败'}), 503

    return jsonify({
        'message':   '指令已下发',
        'device_id': device_id,
        'command':   command,
        'speed_pwm': payload['speed_pwm'],
    }), 200


# ══════════ 路线规划 ══════════


@vehicle_bp.post('/route')
@jwt_required()
@require_device_ownership
def dispatch_route():
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    waypoints = data.get('waypoints', [])

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400
    if not isinstance(waypoints, list) or len(waypoints) < MIN_WAYPOINTS:
        return jsonify({'error': f'航点数量不足，至少需要 {MIN_WAYPOINTS} 个'}), 400
    if len(waypoints) > MAX_WAYPOINTS:
        return jsonify({'error': f'航点超限，最多 {MAX_WAYPOINTS} 个'}), 400

    for i, wp in enumerate(waypoints):
        if not isinstance(wp, dict):
            return jsonify({'error': f'第 {i+1} 个航点格式错误'}), 400
        if 'lat' not in wp or 'lng' not in wp:
            return jsonify({'error': f'第 {i+1} 个航点缺少 lat/lng'}), 400
        try:
            float(wp['lat']); float(wp['lng'])
        except (ValueError, TypeError):
            return jsonify({'error': f'第 {i+1} 个航点 lat/lng 非数字'}), 400

    from src.models.device import Device
    device = Device.query.filter_by(device_id=device_id).first()
    device_online = device.status == 'online' if device else False

    dispatched_at = int(time.time())
    payload_for_check = {
        'command':   'route',
        'device_id': device_id,
        'timestamp': dispatched_at,
        'waypoints': [{'lat': float(wp['lat']), 'lng': float(wp['lng'])} for wp in waypoints],
    }

    passed, security_messages = validate_cruise_command(payload_for_check, device_online=device_online)

    errors_only = [m for m in security_messages if m.startswith('[') and ('失败' in m or '超过' in m or '超出' in m)]
    if not passed:
        record_abnormal_event('CRUISE_SECURITY_FAIL', device_id, '; '.join(errors_only), 'critical')
        return jsonify({
            'error': f'安全验证未通过: {" | ".join(errors_only)}',
            'security_warnings': [m for m in security_messages],
        }), 403

    payload_for_check['signature'] = generate_signature(payload_for_check)
    payload = payload_for_check

    success = publish_command(device_id, payload)
    if not success:
        return jsonify({'error': 'MQTT 发布失败'}), 503

    _last_routes[device_id] = {
        'device_id':      device_id,
        'waypoints':      payload['waypoints'],
        'waypoint_count': len(payload['waypoints']),
        'dispatched_at':  dispatched_at,
    }

    _nav_status_cache[device_id] = {
        'state':         'dispatched',
        'wp_index':      0,
        'wp_total':      len(payload['waypoints']),
        'started_at':    dispatched_at,
    }

    return jsonify({
        'message':        '巡航路线已下发',
        'device_id':      device_id,
        'waypoint_count': len(waypoints),
        'dispatched_at':  dispatched_at,
    }), 200


@vehicle_bp.get('/route/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_last_route(device_id: str):
    route = _last_routes.get(device_id)
    if not route:
        return jsonify({'error': f'设备 {device_id!r} 暂无路线记录'}), 404
    return jsonify(route), 200


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

    nav_info = _nav_status_cache.get(device_id)
    if nav_info:
        result['nav_state']     = nav_info.get('state', 'unknown')
        result['wp_index']      = nav_info.get('wp_index', 0)
        result['wp_total']      = nav_info.get('wp_total', 0)

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


# ══════════ 巡航状态总览 ══════════


@vehicle_bp.get('/status/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_cruise_status(device_id: str):
    """
    获取设备当前巡航状态总览（聚合最新信息）

    Response 200:
        {
            "device_id": "AA:BB:CC:DD:EE:FF",
            "nav_state": "cruising",
            "route": { ... },              // 当前路线信息
            "position": { ... },           // 最新位置
            "latest_event": { ... },       // 最近导航事件
            "stats": {                     // 统计
                "total_waypoints_reached": 3,
                "obstacle_avoid_count": 1,
                "cruise_duration_s": 245,
                "distance_traveled_m": 156.8
            }
        }
    """
    from src.models.device import Device

    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': f'设备 {device_id!r} 不存在'}), 404

    nav_info = _nav_status_cache.get(device_id, {})
    route_info = _last_routes.get(device_id)

    latest_point = (
        TelemetryPoint.query
        .filter_by(device_id=device_id)
        .order_by(TelemetryPoint.recorded_at.desc())
        .first()
    )

    latest_event = (
        NavigationEvent.query
        .filter_by(device_id=device_id)
        .order_by(NavigationEvent.occurred_at.desc())
        .first()
    )

    stats = {}
    if latest_event and latest_event.event_type == 'CRUISE_STARTED':
        started = latest_event.occurred_at
        cruise_duration = int((datetime.now() - started).total_seconds()) if started else 0
        reached_count = (
            NavigationEvent.query
            .filter_by(device_id=device_id, event_type='WAYPOINT_REACHED')
            .count()
        )
        avoid_count = (
            NavigationEvent.query
            .filter(
                NavigationEvent.device_id == device_id,
                NavigationEvent.event_type.in_(['OBSTACLE_DETECTED', 'EMERGENCY_STOP'])
            )
            .count()
        )

        trajectory_points = (
            TelemetryPoint.query
            .filter(
                TelemetryPoint.device_id == device_id,
                TelemetryPoint.recorded_at >= started
            )
            .filter(TelemetryPoint.latitude.isnot(None))
            .filter(TelemetryPoint.longitude.isnot(None))
            .order_by(TelemetryPoint.recorded_at.asc())
            .all()
        )

        total_distance = 0.0
        for i in range(1, len(trajectory_points)):
            prev = trajectory_points[i - 1]
            curr = trajectory_points[i]
            dlat = abs(float(curr.latitude) - float(prev.latitude)) * 111320
            dlng = abs(float(curr.longitude) - float(prev.longitude)) * \
                  111320 * cos(float(prev.latitude) * 3.14159 / 180)
            total_distance += (dlat ** 2 + dlng ** 2) ** 0.5

        stats = {
            'cruise_duration_s':      cruise_duration,
            'total_waypoints_reached': reached_count,
            'obstacle_avoid_count':    avoid_count,
            'distance_traveled_m':     round(total_distance, 1),
        }
    else:
        all_events = (
            NavigationEvent.query
            .filter_by(device_id=device_id)
            .filter(NavigationEvent.event_type.in_([
                'CRUISE_STARTED', 'ARRIVED', 'ABORTED'
            ]))
            .order_by(NavigationEvent.occurred_at.desc())
            .limit(5)
            .all()
        )
        completed_cruises = sum(1 for e in all_events if e.event_type in ('ARRIVED', 'ABORTED'))
        stats = {
            'completed_cruises': completed_cruises,
        }

    return jsonify({
        'device_id':     device_id,
        'device_name':   device.name,
        'online':        device.status == 'online',
        'nav_state':     nav_info.get('state', 'idle'),
        'route':         route_info,
        'position':      latest_point.to_dict() if latest_point else None,
        'latest_event':  latest_event.to_dict() if latest_event else None,
        'stats':         stats,
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

    since = datetime.now() - timedelta(hours=hours)

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

"""
vehicle.py — 远程指令反控 + 路线规划路由蓝图

POST /api/vehicle/command             → 向指定设备下发控制指令（仅 Admin）
POST /api/vehicle/route               → 向指定设备下发巡航路线（仅 Admin）
GET  /api/vehicle/route/<device_id>   → 查询设备最近一次下发的路线

T029 [US3]: 指令下发接口，RBAC 拦截演示
T051 [US3]: 路线规划下发接口
"""

import time
import json

from flask import Blueprint, request, jsonify

from src.extensions import db
from src.models.audit_log import SecurityAuditLog
from src.utils.auth_interceptor import jwt_required_with_rbac, require_device_ownership
from src.utils.mqtt_client import publish_command
from flask_jwt_extended import jwt_required

vehicle_bp = Blueprint('vehicle', __name__)

# 合法指令集
VALID_COMMANDS = {'forward', 'backward', 'left', 'right', 'stop'}

# 路线规划约束
MAX_WAYPOINTS = 50          # 单次最多下发的航点数
MIN_WAYPOINTS = 2           # 至少需要 2 个点才能构成路线

# 内存缓存：最近一次下发的路线（生产环境应持久化到数据库）
_last_routes: dict = {}     # { device_id: { waypoints, dispatched_at, ... } }


@vehicle_bp.post('/command')
@jwt_required()
@require_device_ownership
def send_command():
    """
    向设备下发控制指令（仅限设备 Owner 或 Admin）。

    Request JSON:
        {
            "device_id": "aa:bb:cc:dd:ee:ff",
            "command":   "forward"     // forward|backward|left|right|stop
        }

    Response 200:
        { "message": "指令已下发", "device_id": "...", "command": "forward" }

    Response 400:
        { "error": "非法指令" }

    Response 403 (Guest 用户触发):
        { "error": "无权限：鉴权受阻", "detail": "..." }
        同时写入 security_audit_logs(event_type='rbac_deny')
    """
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    command   = data.get('command',   '').strip().lower()

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400

    if command not in VALID_COMMANDS:
        return jsonify({
            'error': f'非法指令 {command!r}，合法指令: {sorted(VALID_COMMANDS)}',
        }), 400

    # 构造下行 payload（含时间戳，供 Arduino 做防重放）
    payload = {
        'command':   command,
        'device_id': device_id,
        'timestamp': int(time.time()),
    }

    success = publish_command(device_id, payload)
    if not success:
        return jsonify({'error': 'MQTT 发布失败，请检查 Broker 连接'}), 503

    return jsonify({
        'message':   '指令已下发',
        'device_id': device_id,
        'command':   command,
    }), 200


# ══════════ 路线规划 ══════════


@vehicle_bp.post('/route')
@jwt_required()
@require_device_ownership
def dispatch_route():
    """
    向指定设备下发巡航路线（Waypoints 坐标数组，仅限设备 Owner 或 Admin）。

    前端在高德地图上打点生成路线后，调用此接口将坐标数组
    通过 MQTT 推送至 cmd/<device_id>，小车端接收后按顺序
    驶向各航点（PID 算法驱动）。

    Request JSON:
        {
            "device_id": "aa:bb:cc:dd:ee:ff",
            "waypoints": [
                { "lat": 39.9042, "lng": 116.4074 },
                { "lat": 39.9050, "lng": 116.4100 },
                { "lat": 39.9060, "lng": 116.4130 }
            ]
        }

    Response 200:
        {
            "message":        "巡航路线已下发",
            "device_id":      "aa:bb:cc:dd:ee:ff",
            "waypoint_count": 3,
            "dispatched_at":  1714567890
        }

    Response 400:
        { "error": "航点数量不足" }

    Response 403 (Guest 用户触发):
        { "error": "无权限：鉴权受阻" }
    """
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    waypoints = data.get('waypoints', [])

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400

    # ── 校验航点格式 ─────────────────────────────────────────────
    if not isinstance(waypoints, list) or len(waypoints) < MIN_WAYPOINTS:
        return jsonify({
            'error': f'航点数量不足，至少需要 {MIN_WAYPOINTS} 个坐标点',
        }), 400

    if len(waypoints) > MAX_WAYPOINTS:
        return jsonify({
            'error': f'航点数量超限，最多 {MAX_WAYPOINTS} 个',
        }), 400

    # 校验每个航点都包含 lat/lng
    for i, wp in enumerate(waypoints):
        if not isinstance(wp, dict):
            return jsonify({'error': f'第 {i+1} 个航点格式错误，需要 {{lat, lng}}'}), 400
        if 'lat' not in wp or 'lng' not in wp:
            return jsonify({'error': f'第 {i+1} 个航点缺少 lat 或 lng 字段'}), 400
        try:
            float(wp['lat'])
            float(wp['lng'])
        except (ValueError, TypeError):
            return jsonify({'error': f'第 {i+1} 个航点的 lat/lng 必须是数字'}), 400

    # ── 构造 MQTT 下行 payload ───────────────────────────────────
    dispatched_at = int(time.time())
    payload = {
        'command':    'route',
        'device_id':  device_id,
        'timestamp':  dispatched_at,
        'waypoints':  [{'lat': float(wp['lat']), 'lng': float(wp['lng'])} for wp in waypoints],
    }

    success = publish_command(device_id, payload)
    if not success:
        return jsonify({'error': 'MQTT 发布失败，请检查 Broker 连接'}), 503

    # 缓存最近一次下发的路线（供查询接口使用）
    _last_routes[device_id] = {
        'device_id':      device_id,
        'waypoints':      payload['waypoints'],
        'waypoint_count': len(payload['waypoints']),
        'dispatched_at':  dispatched_at,
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
    """
    查询指定设备最近一次下发的巡航路线

    Response 200:
        {
            "device_id":      "aa:bb:cc:dd:ee:ff",
            "waypoints":      [{ "lat": 39.90, "lng": 116.40 }, ...],
            "waypoint_count": 3,
            "dispatched_at":  1714567890
        }

    Response 404:
        { "error": "该设备暂无路线记录" }
    """
    route = _last_routes.get(device_id)
    if not route:
        return jsonify({'error': f'设备 {device_id!r} 暂无路线记录'}), 404

    return jsonify(route), 200


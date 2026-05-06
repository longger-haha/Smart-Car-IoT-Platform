"""
devices.py — 设备管理路由蓝图

GET  /api/devices                → 获取设备列表（需登录）
GET  /api/devices/<device_id>    → 获取单个设备详情 + 最新遥测（需登录）
POST /api/devices                → 注册新设备（仅 Admin）→ 生成 Device_Secret
DELETE /api/devices/<device_id>  → 删除设备（仅 Admin）

T020 [US1]: 设备注册与列表接口
T021 [US1]: MQTT 白名单检查逻辑（在 mqtt_client.py 中实现，此处提供设备增删查 API）
"""

import secrets
import string

from flask import Blueprint, request, jsonify
from flask_jwt_extended import jwt_required, get_jwt_identity, get_jwt

from app import db
from src.models.device import Device
from src.models.user import User
from src.models.telemetry import TelemetryPoint
from src.utils.auth_interceptor import jwt_required_with_rbac, require_device_ownership

devices_bp = Blueprint('devices', __name__)

# Device_Secret 长度（字节）
SECRET_LENGTH = 32

# ── 安全阈值 ────────────────────────────────────────────────────
# 超声波距离安全阈值 (cm)：低于此值视为碰撞危险
ULTRASONIC_SAFE_DISTANCE_CM = 50


def _generate_device_secret() -> str:
    """生成强随机 Device_Secret（64 位十六进制字符串）"""
    return secrets.token_hex(SECRET_LENGTH)


@devices_bp.get('/')
@jwt_required()
def list_devices():
    """
    获取所有设备列表（已登录用户均可查看）

    Response 200:
        {
            "devices": [
                {
                    "id": 1,
                    "device_id": "aa:bb:cc:dd:ee:ff",
                    "name": "01号车",
                    "status": "online",
                    "last_seen_at": "2026-05-04T10:00:00",
                    "registered_at": "2026-04-01T08:00:00"
                },
                ...
            ],
            "total": 1
        }
    """
    """
    claims = get_jwt()
    if claims.get('role') == 'admin':
        devices = Device.query.order_by(Device.registered_at.desc()).all()
    else:
        username = get_jwt_identity()
        user = User.query.filter_by(username=username).first()
        if not user:
            return jsonify({'error': 'User not found'}), 404
        devices = Device.query.filter_by(user_id=user.id).order_by(Device.registered_at.desc()).all()
        
    return jsonify({
        'devices': [d.to_dict() for d in devices],
        'total':   len(devices),
    }), 200


@devices_bp.get('/<string:device_id>')
@jwt_required()
@require_device_ownership
def get_device_detail(device_id: str):
    """
    获取单个设备详情 + 最新遥测数据 + 碰撞预警状态

    Response 200:
        {
            "device": { ... },
            "latest_telemetry": { ... } | null,
            "collision_warning": true/false,
            "safe_distance_cm": 50
        }

    Response 404:
        { "error": "设备不存在" }
    """
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': f'设备 {device_id!r} 不存在'}), 404

    # 最新一条遥测
    latest = (
        TelemetryPoint.query
        .filter_by(device_id=device_id)
        .order_by(TelemetryPoint.recorded_at.desc())
        .first()
    )

    # 碰撞预警检测：超声波距离 < 50cm
    collision_warning = False
    if latest and latest.ultrasonic_cm is not None:
        collision_warning = latest.ultrasonic_cm < ULTRASONIC_SAFE_DISTANCE_CM

    return jsonify({
        'device':             device.to_dict(),
        'latest_telemetry':   latest.to_dict() if latest else None,
        'collision_warning':  collision_warning,
        'safe_distance_cm':   ULTRASONIC_SAFE_DISTANCE_CM,
    }), 200


@devices_bp.post('/')
@jwt_required()
def register_device():
    """
    注册新设备（挂载至当前租户），系统自动生成 Device_Secret。
    注册新设备（挂载至当前租户），系统自动生成 Device_Secret。

    Request JSON:
        {
            "device_id": "aa:bb:cc:dd:ee:ff",   // 必填，设备唯一标识
            "name":      "01号车"                 // 可选，设备别名
        }

    Response 201:
        {
            "message":       "设备注册成功",
            "device_id":     "aa:bb:cc:dd:ee:ff",
            "device_secret": "<64位十六进制密钥>"   // 一次性展示，请妥善保存
        }

    Response 409:
        { "error": "设备 ID 已存在" }
    """
    data      = request.get_json(silent=True) or {}
    device_id = data.get('device_id', '').strip()
    name      = data.get('name', '').strip() or None

    if not device_id:
        return jsonify({'error': 'device_id 不能为空'}), 400

    # 检查是否已存在
    if Device.query.filter_by(device_id=device_id).first():
        return jsonify({'error': f'设备 ID {device_id!r} 已存在，请勿重复注册'}), 409

    username = get_jwt_identity()
    user = User.query.filter_by(username=username).first()
    if not user:
        return jsonify({'error': 'User not found'}), 404

    secret = _generate_device_secret()
    device = Device(device_id=device_id, device_secret=secret, name=name, user_id=user.id)
    db.session.add(device)
    db.session.commit()

    return jsonify({
        'message':       '设备注册成功',
        'device_id':     device.device_id,
        'device_secret': secret,   # 一次性展示
    }), 201


@devices_bp.delete('/<string:device_id>')
@jwt_required()
@require_device_ownership
def delete_device(device_id: str):
    """
    删除设备（仅 Owner 或 Admin）

    Response 200:
        { "message": "设备已删除" }

    Response 404:
        { "error": "设备不存在" }
    """
    device = Device.query.filter_by(device_id=device_id).first()
    if not device:
        return jsonify({'error': f'设备 {device_id!r} 不存在'}), 404

    db.session.delete(device)
    db.session.commit()
    return jsonify({'message': f'设备 {device_id!r} 已删除'}), 200


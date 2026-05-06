"""
auth_interceptor.py — JWT 鉴权 + RBAC 角色检查 + 时间戳防重放 装饰器

提供两个核心装饰器:
    @jwt_required_with_rbac(roles=['admin'])
        → 验证 JWT Token 有效性 + 用户角色是否在允许列表内
        → 不通过则写 audit_log(rbac_deny) 并返回 403

    @replay_protected
        → 验证 JSON body 中 `timestamp` 字段的时间差
        → 超过 REPLAY_WINDOW_SECONDS 则写 audit_log(replay) 并返回 403

用法示例:
    from src.utils.auth_interceptor import jwt_required_with_rbac, replay_protected

    @vehicle_bp.post('/command')
    @jwt_required_with_rbac(roles=['admin'])
    def send_command():
        ...

    @telemetry_bp.post('/ingest')
    @replay_protected
    def ingest_telemetry():
        ...
"""

import time
from functools import wraps

from flask import request, jsonify, current_app
from flask_jwt_extended import verify_jwt_in_request, get_jwt


def _write_audit(event_type: str, detail: str = None, target_device_id: str = None):
    """安全地将一条审计日志写入数据库，失败时静默忽略（避免审计本身引发 500）。"""
    try:
        from src.extensions import db
        from src.models.audit_log import SecurityAuditLog

        source_ip = request.remote_addr
        SecurityAuditLog.record(
            event_type=event_type,
            source_ip=source_ip,
            target_device_id=target_device_id,
            detail=detail,
        )
        db.session.commit()
    except Exception as exc:
        current_app.logger.warning(f'[audit] Failed to write audit log: {exc}')


def jwt_required_with_rbac(roles: list):
    """
    装饰器工厂：验证 JWT Token 并执行 RBAC 角色检查。

    Args:
        roles: 允许访问的角色列表，如 ['admin'] 或 ['admin', 'guest']

    用法:
        @jwt_required_with_rbac(roles=['admin'])
        def protected_view():
            ...
    """
    def decorator(fn):
        @wraps(fn)
        def wrapper(*args, **kwargs):
            # 1. 验证 JWT 有效性
            try:
                verify_jwt_in_request()
            except Exception as e:
                _write_audit(
                    event_type='auth_fail',
                    detail=f'JWT verification failed: {e}',
                )
                return jsonify({'error': 'Authentication required', 'detail': str(e)}), 401

            # 2. RBAC 角色检查
            claims      = get_jwt()
            user_role   = claims.get('role', 'guest')
            username    = claims.get('sub', 'unknown')

            if user_role not in roles:
                _write_audit(
                    event_type='rbac_deny',
                    detail=(
                        f'User {username!r} with role {user_role!r} '
                        f'attempted to access endpoint requiring {roles}'
                    ),
                )
                return jsonify({
                    'error': '无权限：鉴权受阻',
                    'detail': f'Role {user_role!r} is not authorized. Required: {roles}',
                }), 403

            return fn(*args, **kwargs)
        return wrapper
    return decorator


def replay_protected(fn):
    """
    装饰器：校验请求体 JSON 中的 `timestamp` 字段，
    超过 REPLAY_WINDOW_SECONDS 则视为重放攻击并拒绝。

    请求体格式要求:
        {
            "timestamp": 1714567890,   // Unix 时间戳 (秒)
            "device_id": "...",        // 可选，用于审计
            ...
        }
    """
    @wraps(fn)
    def wrapper(*args, **kwargs):
        data = request.get_json(silent=True) or {}
        ts   = data.get('timestamp')

        # timestamp 缺失视为异常
        if ts is None:
            _write_audit(
                event_type='replay',
                detail='Missing timestamp field in request body',
            )
            return jsonify({'error': '缺少时间戳字段，请求被拒绝'}), 403

        # 时间差检查
        window  = current_app.config.get('REPLAY_WINDOW_SECONDS', 30)
        now_ts  = time.time()
        delta   = abs(now_ts - float(ts))

        if delta > window:
            device_id = data.get('device_id')
            _write_audit(
                event_type='replay',
                detail=(
                    f'Timestamp delta={delta:.1f}s exceeds window={window}s. '
                    f'Received ts={ts}, server ts={now_ts:.0f}'
                ),
                target_device_id=device_id,
            )
            return jsonify({
                'error':  '防重放校验失败：时间戳过期',
                'detail': f'时间差 {delta:.1f}s 超过容忍窗口 {window}s',
            }), 403

        return fn(*args, **kwargs)
    return wrapper


def require_device_ownership(fn):
    """
    装饰器：验证当前登录用户是否拥有目标设备。
    对于 Admin 用户，放行所有设备。
    对于 User 租户，若设备非己有，则记录跨租户越权攻击日志并阻断。

    要求：被装饰的路由函数参数列表中有 `device_id`，
    或者请求体 JSON 中包含 `device_id`。
    """
    @wraps(fn)
    def wrapper(*args, **kwargs):
        # 获取身份信息
        try:
            verify_jwt_in_request()
        except Exception:
            return jsonify({'error': 'Authentication required'}), 401
            
        claims    = get_jwt()
        user_role = claims.get('role', 'user')
        username  = claims.get('sub', 'unknown')

        # 尝试从 kwargs (URL path) 获取，若没有则从 json 获取
        device_id = kwargs.get('device_id')
        if not device_id:
            data = request.get_json(silent=True) or {}
            device_id = data.get('device_id')

        if not device_id:
            return jsonify({'error': '未提供设备ID'}), 400

        # Admin 用户放行
        if user_role == 'admin':
            return fn(*args, **kwargs)

        # 校验设备归属
        from src.models.device import Device
        from src.models.user import User

        target_device = Device.query.filter_by(device_id=device_id).first()
        if not target_device:
            return jsonify({'error': '设备不存在'}), 404

        current_user = User.query.filter_by(username=username).first()
        
        if not current_user or target_device.user_id != current_user.id:
            _write_audit(
                event_type='rbac_deny',
                detail=f'Cross-tenant horizontal privilege escalation attempt! User {username!r} tried to access device {device_id!r} owned by user_id {target_device.user_id}.',
                target_device_id=device_id
            )
            return jsonify({
                'error': '无权限：该设备不属于您',
                'detail': '越权拦截，已记录审计日志'
            }), 403

        return fn(*args, **kwargs)
    return wrapper

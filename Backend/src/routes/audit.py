"""
audit.py — 安全审计日志路由蓝图

GET /api/audit/logs → 查询安全攻防日志，支持 event_type 过滤和分页
  - 管理员：可查看全部日志，含关联用户名
  - 普通用户：仅查看与自己相关的日志（user_id 匹配或 target_device_id 属于自己设备）
"""

from flask import Blueprint, request, jsonify
from flask_jwt_extended import jwt_required, get_jwt

from src.models.audit_log import SecurityAuditLog, AUDIT_EVENT_TYPES
from src.models.device import Device
from src.models.user import User

audit_bp = Blueprint('audit', __name__)

DEFAULT_PAGE_SIZE = 20
MAX_PAGE_SIZE     = 100


@audit_bp.get('/logs')
@jwt_required()
def get_logs():
    claims = get_jwt()
    user_role = claims.get('role', 'user')
    username  = claims.get('sub', 'unknown')

    event_type = request.args.get('event_type', '').strip().lower() or None
    filter_user_id = request.args.get('user_id', type=int)

    # 分页参数
    page_size = min(int(request.args.get('page_size', DEFAULT_PAGE_SIZE)), MAX_PAGE_SIZE)
    page = max(int(request.args.get('page', 1)), 1)

    # 校验 event_type
    if event_type and event_type not in AUDIT_EVENT_TYPES:
        return jsonify({
            'error': f'无效的 event_type: {event_type!r}',
            'valid_types': list(AUDIT_EVENT_TYPES),
        }), 400

    query = SecurityAuditLog.query

    # event_type 过滤
    if event_type:
        query = query.filter_by(event_type=event_type)

    # 普通用户：只返回自己相关的日志
    if user_role != 'admin':
        user = User.query.filter_by(username=username).first()
        if not user:
            return jsonify({'error': '用户不存在'}), 404

        # 获取用户拥有的设备 ID 列表
        owned_device_ids = [
            d.device_id for d in Device.query.filter_by(user_id=user.id).all()
        ]

        from sqlalchemy import or_
        conditions = [SecurityAuditLog.user_id == user.id]
        if owned_device_ids:
            conditions.append(SecurityAuditLog.target_device_id.in_(owned_device_ids))
        query = query.filter(or_(*conditions))
    else:
        # 管理员可按用户过滤
        if filter_user_id:
            query = query.filter_by(user_id=filter_user_id)

    # 总数（分页前）
    total = query.count()

    # 分页
    logs = (
        query
        .order_by(SecurityAuditLog.occurred_at.desc())
        .offset((page - 1) * page_size)
        .limit(page_size)
        .all()
    )

    return jsonify({
        'total':              total,
        'page':               page,
        'page_size':          page_size,
        'event_type_filter':  event_type,
        'logs':               [log.to_dict() for log in logs],
    }), 200

"""
audit.py — 安全审计日志路由蓝图

GET /api/audit/logs → 查询安全攻防日志，支持 event_type 过滤和 limit 分页

T034 [US4]: 审计日志查询接口
"""

from flask import Blueprint, request, jsonify
from flask_jwt_extended import jwt_required

from src.models.audit_log import SecurityAuditLog, AUDIT_EVENT_TYPES

audit_bp = Blueprint('audit', __name__)

DEFAULT_LIMIT = 50
MAX_LIMIT     = 500


@audit_bp.get('/logs')
@jwt_required()
def get_logs():
    """
    获取安全审计日志（最新的在前）

    Query Params:
        event_type (str): 过滤事件类型 replay|ddos|auth_fail|rbac_deny|sig_invalid
        limit      (int): 返回条数，默认 50，最大 500

    Response 200:
        {
            "total": 25,
            "event_type_filter": "replay",
            "logs": [
                {
                    "id": 1,
                    "event_type": "replay",
                    "source_ip": "192.168.1.100",
                    "target_device_id": "aa:bb:cc:dd:ee:ff",
                    "detail": "Timestamp delta=360s exceeds window=30s",
                    "is_blocked": true,
                    "occurred_at": "2026-05-04T10:00:00"
                },
                ...
            ]
        }
    """
    event_type = request.args.get('event_type', '').strip().lower() or None

    # 校验 event_type
    if event_type and event_type not in AUDIT_EVENT_TYPES:
        return jsonify({
            'error': f'无效的 event_type: {event_type!r}',
            'valid_types': list(AUDIT_EVENT_TYPES),
        }), 400

    try:
        limit = min(int(request.args.get('limit', DEFAULT_LIMIT)), MAX_LIMIT)
    except (ValueError, TypeError):
        limit = DEFAULT_LIMIT

    query = SecurityAuditLog.query

    if event_type:
        query = query.filter_by(event_type=event_type)

    logs = query.order_by(SecurityAuditLog.occurred_at.desc()).limit(limit).all()

    return jsonify({
        'total':              len(logs),
        'event_type_filter':  event_type,
        'logs':               [log.to_dict() for log in logs],
    }), 200

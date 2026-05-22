"""
SecurityAuditLog SQLAlchemy 模型 — 对应 `security_audit_logs` 表
"""
from datetime import datetime
from src.extensions import db

# 允许的事件类型
AUDIT_EVENT_TYPES = ('replay', 'ddos', 'auth_fail', 'rbac_deny', 'sig_invalid')


class SecurityAuditLog(db.Model):
    """安全攻防审计日志，记录所有拦截事件"""

    __tablename__ = 'security_audit_logs'

    id               = db.Column(db.Integer, primary_key=True, autoincrement=True)
    event_type       = db.Column(
        db.Enum('replay', 'ddos', 'auth_fail', 'rbac_deny', 'sig_invalid'),
        nullable=False, comment='攻击类型'
    )
    source_ip        = db.Column(db.String(64),  nullable=True, comment='攻击来源IP')
    target_device_id = db.Column(db.String(64),  nullable=True, comment='被针对的设备ID')
    user_id          = db.Column(db.Integer, db.ForeignKey('users.id'), nullable=True, comment='关联用户ID')
    detail           = db.Column(db.Text,         nullable=True, comment='详细描述')
    is_blocked       = db.Column(db.Boolean,      nullable=False, default=True,
                                 comment='是否成功拦截')
    occurred_at      = db.Column(db.DateTime,     nullable=False, default=datetime.utcnow,
                                 index=True, comment='事件发生时间')

    __table_args__ = (
        db.Index('idx_audit_event_device', 'event_type', 'target_device_id'),
        db.Index('idx_audit_user_id', 'user_id'),
    )

    @classmethod
    def record(cls, event_type: str, source_ip: str = None,
               target_device_id: str = None, detail: str = None,
               is_blocked: bool = True, user_id: int = None):
        """
        快捷工厂方法：创建并写入一条审计记录。
        调用方负责 db.session.commit()。
        """
        if event_type not in AUDIT_EVENT_TYPES:
            raise ValueError(f'Invalid event_type: {event_type!r}')
        log = cls(
            event_type=event_type,
            source_ip=source_ip,
            target_device_id=target_device_id,
            detail=detail,
            is_blocked=is_blocked,
            user_id=user_id,
        )
        db.session.add(log)
        return log

    def to_dict(self):
        username = None
        if self.user_id:
            from src.models.user import User
            u = db.session.get(User, self.user_id)
            username = u.username if u else None
        return {
            'id':               self.id,
            'event_type':       self.event_type,
            'source_ip':        self.source_ip,
            'target_device_id': self.target_device_id,
            'user_id':          self.user_id,
            'username':         username,
            'detail':           self.detail,
            'is_blocked':       self.is_blocked,
            'occurred_at':      self.occurred_at.isoformat() if self.occurred_at else None,
        }

    def __repr__(self):
        return (f'<SecurityAuditLog id={self.id} event_type={self.event_type!r} '
                f'occurred_at={self.occurred_at!r}>')

"""
Device SQLAlchemy 模型 — 对应 `devices` 表（设备白名单）
"""
from datetime import datetime, timezone
from src.extensions import db


class Device(db.Model):
    """设备白名单模型，保存设备凭证和在线状态"""

    __tablename__ = 'devices'

    id             = db.Column(db.Integer,     primary_key=True, autoincrement=True)
    user_id        = db.Column(db.Integer,     db.ForeignKey('users.id', ondelete='CASCADE'), nullable=False, comment='所属租户ID')
    device_id      = db.Column(db.String(64),  nullable=False, unique=True,
                               comment='设备唯一标识 (如MAC地址)')
    device_secret  = db.Column(db.String(128), nullable=False,
                               comment='HMAC签名派生密钥 (强随机)')
    name           = db.Column(db.String(128), nullable=True,  comment='设备别名')
    status         = db.Column(db.Enum('online', 'offline'), nullable=False, default='offline')
    last_seen_at   = db.Column(db.DateTime,    nullable=True,  comment='最近心跳时间')
    registered_at  = db.Column(db.DateTime,    nullable=False, default=lambda: datetime.now(timezone.utc))

    # ── 关联 ──────────────────────────────────────────────────────────
    user             = db.relationship('User', backref=db.backref('devices', lazy=True, cascade='all, delete-orphan'))
    telemetry_points = db.relationship('TelemetryPoint', backref='device',
                                       lazy='dynamic', cascade='all, delete-orphan')

    def touch(self):
        """更新心跳时间并标记在线"""
        self.last_seen_at = datetime.now(timezone.utc)
        self.status = 'online'

    def to_dict(self, include_secret=False):
        """序列化为字典；默认不暴露 device_secret"""
        data = {
            'id':            self.id,
            'user_id':       self.user_id,
            'username':      self.user.username if self.user else None,
            'device_id':     self.device_id,
            'name':          self.name,
            'status':        self.status,
            'last_seen_at':  self.last_seen_at.isoformat() if self.last_seen_at else None,
            'registered_at': self.registered_at.isoformat() if self.registered_at else None,
        }
        if include_secret:
            data['device_secret'] = self.device_secret
        return data

    def __repr__(self):
        return f'<Device id={self.id} device_id={self.device_id!r} status={self.status!r}>'

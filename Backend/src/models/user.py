"""
User SQLAlchemy 模型 — 对应 `users` 表
"""
from datetime import datetime, timezone
from src.extensions import db


class User(db.Model):
    """用户账户模型，支持 RBAC 角色 (admin / user)"""

    __tablename__ = 'users'

    id             = db.Column(db.Integer,      primary_key=True, autoincrement=True)
    username       = db.Column(db.String(64),   nullable=False, unique=True)
    password_hash  = db.Column(db.String(256),  nullable=False)
    role           = db.Column(db.Enum('admin', 'user'), nullable=False, default='user')
    created_at     = db.Column(db.DateTime,     nullable=False, default=lambda: datetime.now(timezone.utc))
    last_login_at  = db.Column(db.DateTime,     nullable=True)

    def to_dict(self):
        """序列化为 JSON 安全的字典（不含密码哈希）"""
        return {
            'id':            self.id,
            'username':      self.username,
            'role':          self.role,
            'created_at':    self.created_at.isoformat() if self.created_at else None,
            'last_login_at': self.last_login_at.isoformat() if self.last_login_at else None,
        }

    def __repr__(self):
        return f'<User id={self.id} username={self.username!r} role={self.role!r}>'

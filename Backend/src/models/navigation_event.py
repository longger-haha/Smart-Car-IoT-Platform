"""
NavigationEvent SQLAlchemy 模型 — 对应 `navigation_events` 表

记录设备自动驾驶巡航过程中的关键事件:
  - 巡航开始/到达/中止
  - 航点到达
  - 障碍物检测与绕行
  - 紧急停车
"""

from datetime import datetime, timezone, timedelta
from src.extensions import db

CST = timezone(timedelta(hours=8))


class NavigationEvent(db.Model):
    """自动驾驶导航事件记录"""

    __tablename__ = 'navigation_events'

    id               = db.Column(db.Integer,      primary_key=True, autoincrement=True)
    device_id        = db.Column(db.String(64),   nullable=False, index=True, comment='设备ID')
    event_type       = db.Column(db.String(32),   nullable=False,
                                 comment='CRUISE_STARTED|WAYPOINT_REACHED|ARRIVED|'
                                         'OBSTACLE_DETECTED|OBSTACLE_CLEARED|EMERGENCY_STOP|'
                                         'ABORTED|ONLINE')
    detail           = db.Column(db.String(256),   nullable=True, comment='事件详情')
    lat              = db.Column(db.Numeric(10,7), nullable=True, comment='事件发生时纬度')
    lng              = db.Column(db.Numeric(10,7), nullable=True, comment='事件发生时经度')
    wp_index         = db.Column(db.Integer,       nullable=True, comment='当前航点序号(从1开始)')
    wp_total         = db.Column(db.Integer,       nullable=True, comment='航点总数')
    state            = db.Column(db.String(16),    nullable=True, comment='导航状态: idle/cruising/avoiding/arrived/aborted')
    occurred_at      = db.Column(db.DateTime,      nullable=False, default=lambda: datetime.now(timezone.utc),
                                 index=True, comment='事件时间')

    __table_args__ = (
        db.Index('idx_nav_event_device_time', 'device_id', 'occurred_at'),
        db.Index('idx_nav_event_type', 'event_type'),
    )

    def to_dict(self):
        return {
            'id':          self.id,
            'device_id':   self.device_id,
            'event_type':  self.event_type,
            'detail':      self.detail,
            'lat':         float(self.lat) if self.lat is not None else None,
            'lng':         float(self.lng) if self.lng is not None else None,
            'wp_index':    self.wp_index,
            'wp_total':    self.wp_total,
            'state':       self.state,
            'occurred_at': self.occurred_at.astimezone(CST).isoformat() if self.occurred_at else None,
        }

    def __repr__(self):
        return (f'<NavigationEvent id={self.id} type={self.event_type!r} '
                f'device={self.device_id!r}>')

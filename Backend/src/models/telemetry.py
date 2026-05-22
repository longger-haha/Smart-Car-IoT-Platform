"""
TelemetryPoint SQLAlchemy 模型 — 对应 `telemetry_points` 表
"""
from datetime import datetime
from src.extensions import db


class TelemetryPoint(db.Model):
    """传感器遥测数据记录"""

    __tablename__ = 'telemetry_points'

    id             = db.Column(db.Integer,      primary_key=True, autoincrement=True)
    device_id      = db.Column(db.String(64),     db.ForeignKey('devices.device_id',
                                                   ondelete='CASCADE', onupdate='CASCADE'),
                               nullable=False, index=True)
    latitude       = db.Column(db.Numeric(10, 7), nullable=True,  comment='GPS纬度')
    longitude      = db.Column(db.Numeric(10, 7), nullable=True,  comment='GPS经度')
    temperature    = db.Column(db.Float,          nullable=True,  comment='温度(°C)')
    humidity       = db.Column(db.Float,          nullable=True,  comment='湿度(%)')
    ultrasonic_cm  = db.Column(db.Float,          nullable=True,  comment='超声波距离(cm)')
    ir_obstacle    = db.Column(db.Boolean,         nullable=True,  comment='红外避障(True=检测到)')
    imu_heading    = db.Column(db.Float,          nullable=True,  comment='IMU航向角(°)')
    imu_gyro_z     = db.Column(db.Float,          nullable=True,  comment='IMU Z轴角速度(°/s)')
    speed_pwm      = db.Column(db.SmallInteger,   nullable=True,  comment='电机PWM值')
    altitude       = db.Column(db.Float,          nullable=True,  comment='GPS海拔(m)')
    speed_kmh      = db.Column(db.Float,          nullable=True,  comment='GPS速度(km/h)')
    satellites     = db.Column(db.SmallInteger,   nullable=True,  comment='GPS卫星数')
    raw_ciphertext = db.Column(db.Text,           nullable=True,  comment='原始接收密文')
    recorded_at    = db.Column(db.DateTime,       nullable=False, default=datetime.utcnow,
                               index=True, comment='数据时间戳')

    __table_args__ = (
        db.Index('idx_telemetry_device_time', 'device_id', 'recorded_at'),
    )

    def to_dict(self):
        """序列化为 JSON 字典"""
        return {
            'id':             self.id,
            'device_id':      self.device_id,
            'latitude':       float(self.latitude)  if self.latitude  is not None else None,
            'longitude':      float(self.longitude) if self.longitude is not None else None,
            'temperature':    self.temperature,
            'humidity':       self.humidity,
            'ultrasonic_cm':  self.ultrasonic_cm,
            'ir_obstacle':    self.ir_obstacle,
            'imu_heading':    self.imu_heading,
            'imu_gyro_z':     self.imu_gyro_z,
            'speed_pwm':      self.speed_pwm,
            'altitude':       self.altitude,
            'speed_kmh':      self.speed_kmh,
            'satellites':     self.satellites,
            'raw_ciphertext': self.raw_ciphertext,
            'recorded_at':    self.recorded_at.isoformat() if self.recorded_at else None,
        }

    def __repr__(self):
        return (f'<TelemetryPoint id={self.id} device_id={self.device_id!r} '
                f'recorded_at={self.recorded_at!r}>')

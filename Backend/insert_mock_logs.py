from dotenv import load_dotenv
import os
load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

from app import create_app
from src.extensions import db
from src.models.audit_log import SecurityAuditLog
from src.models.user import User
import random

app = create_app()
with app.app_context():
    # 获取任意一个用户作为测试用户
    user = User.query.first()
    user_id = user.id if user else None

    logs = [
        ("ddos", "192.168.1.100", "dev_01", "检测到高频请求 (1000qps)，已拉黑 IP"),
        ("replay", "10.0.0.5", "dev_02", "指令时间戳过期 30s，疑似重放攻击"),
        ("auth_fail", "114.114.114.114", "dev_01", "连续 5 次设备认证密码错误"),
        ("rbac_deny", "192.168.1.50", "dev_03", "越权访问设备控制接口，无操作权限"),
        ("sig_invalid", "10.10.10.10", "dev_02", "API 签名 (HMAC) 校验失败"),
        ("auth_fail", "8.8.8.8", None, "尝试爆破平台管理员账户"),
        ("ddos", "223.5.5.5", "dev_01", "SYN Flood 攻击拦截"),
        ("replay", "192.168.1.101", "dev_03", "相同的 Nonce 被多次使用，拒绝服务"),
    ]

    for event_type, ip, dev_id, detail in logs:
        SecurityAuditLog.record(
            event_type=event_type,
            source_ip=ip,
            target_device_id=dev_id,
            detail=detail,
            is_blocked=True,
            user_id=user_id if random.random() > 0.2 else None
        )
    
    db.session.commit()
    print("Mock audit logs inserted successfully!")

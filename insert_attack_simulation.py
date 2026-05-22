"""
安全审计日志攻击模拟脚本

模拟各类攻击事件，生成测试用的审计日志数据。

事件类型:
  - replay      : 重放攻击
  - ddos        : DDoS 攻击
  - auth_fail   : 认证失败
  - rbac_deny   : 权限拒绝 / 越权访问
  - sig_invalid : 签名校验失败

运行方式:
  python insert_attack_simulation.py
"""

from dotenv import load_dotenv
import os, random, time
from datetime import datetime, timedelta

load_dotenv(os.path.join(os.path.dirname(__file__), '..', '.env'))

from app import create_app
from src.extensions import db
from src.models.audit_log import SecurityAuditLog, AUDIT_EVENT_TYPES
from src.models.user import User
from src.models.device import Device

# 模拟攻击源 IP 池
MALICIOUS_IPS = [
    '192.168.1.100', '192.168.1.101', '192.168.1.102',
    '10.0.0.5', '10.0.0.8', '10.10.10.10',
    '114.114.114.114', '8.8.8.8', '223.5.5.5',
    '185.220.101.1', '45.33.32.156', '23.129.64.1',
    '1.2.3.4', '5.6.7.8', '99.88.77.66',
]

# 设备 ID 池
DEVICE_IDS = ['dev_01', 'dev_02', 'dev_03', 'dev_04', 'dev_05']

# 各事件类型模拟数据
ATTACK_SCENARIOS = {
    'ddos': [
        ("{ip}", "{device}", "检测到高频请求 (1200qps)，SYN Flood 攻击，已拉黑 IP"),
        ("{ip}", "{device}", "HTTP Flood 攻击，QPS 超过阈值 500，已触发限流"),
        ("{ip}", "{device}", "UDP Flood 攻击，流量峰值 800Mbps，已拦截"),
        ("{ip}", "{device}", "ICMP Flood，来源 IP {ip}，已加入黑名单"),
        ("{ip}", "{device}", "多向量 DDoS，混合协议攻击，已触发防御机制"),
        ("{ip}", None, "平台级 DDoS 攻击，目标 /api/vehicle，QPS 达 5000"),
        ("{ip}", "{device}", "CC 攻击，访问 /dashboard/stats 频繁，已限流"),
    ],
    'replay': [
        ("{ip}", "{device}", "指令时间戳过期 60s，疑似重放攻击，已拒绝"),
        ("{ip}", "{device}", "相同的 Nonce 被多次使用，拒绝执行"),
        ("{ip}", "{device}", "JWT Token 被重放，有效期内重复请求，已拦截"),
        ("{ip}", "{device}", "历史指令序列被重放，时间戳异常，已拒绝"),
        ("{ip}", "{device}", "MQTT 重放攻击，ClientID 复用，已阻断连接"),
        ("{ip}", None, "API 请求重放，Authorization Header 重复使用"),
    ],
    'auth_fail': [
        ("{ip}", "{device}", "设备认证密码错误，连续失败 3 次"),
        ("{ip}", "{device}", "JWT 签名无效，Token 伪造检测"),
        ("{ip}", None, "用户 {user} 登录密码错误 5 次，账号已锁定"),
        ("{ip}", None, "管理员账户 {user} 爆破尝试，目标密码强度过低"),
        ("{ip}", "{device}", "设备证书校验失败，证书已过期"),
        ("{ip}", None, "Anonymous 匿名访问被拒绝，需登录"),
        ("{ip}", "{device}", "OAuth2 Token 失效，已过期"),
        ("{ip}", None, "API Key 无效或已撤销"),
    ],
    'rbac_deny': [
        ("{ip}", "{device}", "普通用户尝试访问管理员设备控制接口，已拒绝"),
        ("{ip}", "{device}", "跨租户访问，其他用户设备 {device} 无权限操作"),
        ("{ip}", "{device}", "普通用户 role=user 尝试执行管理员操作，已拦截"),
        ("{ip}", "{device}", "设备所有权验证失败，当前用户无操控权限"),
        ("{ip}", "{device}", "CSRF Token 缺失，跨站请求伪造检测"),
        ("{ip}", None, "未授权访问 /admin 路径，已重定向至 /dashboard"),
    ],
    'sig_invalid': [
        ("{ip}", "{device}", "HMAC 签名校验失败，消息被篡改"),
        ("{ip}", "{device}", "RSA 签名验证失败，证书链不完整"),
        ("{ip}", "{device}", "指令签名缺失或格式错误，已拒绝"),
        ("{ip}", "{device}", "时间戳签名不匹配，中间人攻击检测"),
        ("{ip}", "{device}", "重放攻击检测到，Nonce 已使用过"),
        ("{ip}", None, "WebSocket 握手签名校验失败"),
    ],
}


def generate_logs(count_per_type: int = 3) -> None:
    """生成各类型攻击日志"""
    app = create_app()
    with app.app_context():
        users = User.query.all()
        devices = Device.query.all()
        device_ids = [d.device_id for d in devices] if devices else DEVICE_IDS

        user_names = [u.username for u in users]
        user_ids = [u.id for u in users]

        logs = []

        for event_type in AUDIT_EVENT_TYPES:
            scenarios = ATTACK_SCENARIOS.get(event_type, [])
            for i in range(count_per_type):
                scenario = random.choice(scenarios)
                ip = random.choice(MALICIOUS_IPS)
                device = random.choice(device_ids)
                user_name = random.choice(user_names) if user_names else 'unknown'

                detail = scenario[2].format(
                    ip=ip, device=device, user=user_name
                )

                # 随机时间（最近 7 天内）
                random_days = random.uniform(0, 7)
                occurred_at = datetime.utcnow() - timedelta(days=random_days)

                # 关联用户（部分记录关联，部分不关联）
                user_id = random.choice(user_ids) if user_ids and random.random() > 0.3 else None

                # 是否被拦截
                is_blocked = random.random() > 0.15  # 85% 被拦截

                log = SecurityAuditLog(
                    event_type=event_type,
                    source_ip=ip,
                    target_device_id=device if random.random() > 0.2 else None,
                    detail=detail,
                    is_blocked=is_blocked,
                    user_id=user_id,
                    occurred_at=occurred_at,
                )
                logs.append(log)

        db.session.add_all(logs)
        db.session.commit()

        print(f"[+] 成功写入 {len(logs)} 条审计日志:")
        for event_type in AUDIT_EVENT_TYPES:
            cnt = len([l for l in logs if l.event_type == event_type])
            print(f"    {event_type:<12} : {cnt} 条")


if __name__ == '__main__':
    import sys
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 3
    print(f"开始模拟生成审计日志（每类型 {count} 条）...")
    generate_logs(count)
    print("完成。")

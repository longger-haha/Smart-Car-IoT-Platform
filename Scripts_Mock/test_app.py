"""测试 Flask app 工厂可以正常创建（不连接真实数据库）"""
import sys
sys.path.insert(0, '.')

import os
# 使用 SQLite 内存数据库测试（避免依赖 MySQL）
os.environ['MYSQL_HOST'] = 'localhost'
os.environ['MYSQL_DATABASE'] = 'test_db'

# 覆盖 SQLAlchemy URI 为 SQLite
import config as cfg_module

original_uri = cfg_module.Config.SQLALCHEMY_DATABASE_URI

# Monkey-patch 为 SQLite
cfg_module.Config.SQLALCHEMY_DATABASE_URI = 'sqlite:///:memory:'

from app import create_app, db
from src.models import User, Device, TelemetryPoint, SecurityAuditLog

app = create_app()

with app.app_context():
    # 创建所有表
    db.create_all()
    print('Tables created: OK')

    # 测试 User 模型创建
    import bcrypt
    hashed = bcrypt.hashpw(b'admin123', bcrypt.gensalt()).decode('utf-8')
    admin  = User(username='admin', password_hash=hashed, role='admin')
    db.session.add(admin)
    db.session.commit()
    print(f'User created: {admin}')

    # 测试 Device 模型
    dev = Device(device_id='aa:bb:cc:dd:ee:ff', device_secret='testsecret', name='TestCar')
    db.session.add(dev)
    db.session.commit()
    print(f'Device created: {dev}')
    dev.touch()
    db.session.commit()
    print(f'Device status: {dev.status}, last_seen_at: {dev.last_seen_at}')

    # 测试 SecurityAuditLog 快捷方法
    log = SecurityAuditLog.record(event_type='auth_fail', source_ip='127.0.0.1',
                                   detail='Test log entry')
    db.session.commit()
    print(f'AuditLog created: {log}')

    # 测试 health 端点
    client   = app.test_client()
    response = client.get('/api/health')
    assert response.status_code == 200
    data = response.get_json()
    assert data['status'] == 'ok'
    print(f'Health endpoint: {data}')

    # 测试 auth login
    resp = client.post('/api/auth/login',
                       json={'username': 'admin', 'password': 'admin123'})
    assert resp.status_code == 200, f'Login failed: {resp.get_json()}'
    token_data = resp.get_json()
    assert 'access_token' in token_data
    assert token_data['role'] == 'admin'
    print(f'Login: OK, role={token_data["role"]}')

    # 测试设备列表（需要 JWT）
    token = token_data['access_token']
    resp  = client.get('/api/devices/',
                       headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 200
    devs_data = resp.get_json()
    assert devs_data['total'] == 1
    print(f'Devices list: total={devs_data["total"]} OK')

    # 测试审计日志
    resp = client.get('/api/audit/logs',
                      headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 200
    logs_data = resp.get_json()
    print(f'Audit logs: total={logs_data["total"]} OK')

    print()
    print('ALL FLASK APP TESTS PASSED')

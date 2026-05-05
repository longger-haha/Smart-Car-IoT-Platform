"""测试新增 API: 设备详情、Dashboard 统计、用户注册"""
import sys, os
sys.path.insert(0, '.')
os.environ['MYSQL_HOST'] = 'localhost'
os.environ['MYSQL_DATABASE'] = 'test_db'

import config as cfg_module
cfg_module.Config.SQLALCHEMY_DATABASE_URI = 'sqlite:///:memory:'

from app import create_app, db
from src.models import User, Device, TelemetryPoint, SecurityAuditLog
import bcrypt
from datetime import datetime

app = create_app()

with app.app_context():
    db.create_all()

    # 创建测试数据
    hashed = bcrypt.hashpw(b'admin123', bcrypt.gensalt()).decode('utf-8')
    admin  = User(username='admin', password_hash=hashed, role='admin')
    db.session.add(admin)

    dev = Device(device_id='test-car-01', device_secret='secret123', name='TestCar')
    dev.touch()
    db.session.add(dev)

    # 写入一条遥测（超声波 30cm < 50cm 阈值）
    point = TelemetryPoint(
        device_id='test-car-01', latitude=39.9, longitude=116.4,
        temperature=25, humidity=60, ultrasonic_cm=30, speed_pwm=150,
        recorded_at=datetime.utcnow()
    )
    db.session.add(point)
    db.session.commit()

    client = app.test_client()

    # 1. 用户注册测试
    resp = client.post('/api/auth/register', json={'username': 'testuser', 'password': 'test123456'})
    assert resp.status_code == 201, f'Register failed: {resp.get_json()}'
    print(f'Register: {resp.get_json()}')

    # 注册重复用户应 409
    resp2 = client.post('/api/auth/register', json={'username': 'testuser', 'password': 'test123456'})
    assert resp2.status_code == 409
    print(f'Duplicate register: 409 OK')

    # 密码太短应 400
    resp3 = client.post('/api/auth/register', json={'username': 'short', 'password': '123'})
    assert resp3.status_code == 400
    print(f'Short password: 400 OK')

    # 2. 登录获取 token
    resp = client.post('/api/auth/login', json={'username': 'admin', 'password': 'admin123'})
    token = resp.get_json()['access_token']

    # 3. 设备详情 + 碰撞预警
    resp = client.get('/api/devices/test-car-01', headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 200
    data = resp.get_json()
    assert data['collision_warning'] == True, f'Should have collision warning: ultrasonic=30 < 50'
    assert data['safe_distance_cm'] == 50
    print(f'Device detail: collision_warning={data["collision_warning"]}, safe_distance={data["safe_distance_cm"]}cm OK')

    # 4. 不存在的设备应 404
    resp = client.get('/api/devices/nonexistent', headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 404
    print(f'Device 404: OK')

    # 5. Dashboard 统计
    resp = client.get('/api/dashboard/stats', headers={'Authorization': f'Bearer {token}'})
    assert resp.status_code == 200
    stats = resp.get_json()
    assert stats['total_devices'] == 1
    assert stats['online_devices'] == 1
    assert stats['collision_warnings'] == 1
    assert stats['safe_distance_cm'] == 50
    print(f'Dashboard stats: devices={stats["total_devices"]}, online={stats["online_devices"]}, '
          f'collision_warnings={stats["collision_warnings"]} OK')

    # 6. 安全距离内的遥测不应触发预警
    point2 = TelemetryPoint(
        device_id='test-car-01', latitude=39.91, longitude=116.41,
        temperature=25, humidity=60, ultrasonic_cm=120, speed_pwm=150,
        recorded_at=datetime.utcnow()
    )
    db.session.add(point2)
    db.session.commit()

    resp = client.get('/api/devices/test-car-01', headers={'Authorization': f'Bearer {token}'})
    data = resp.get_json()
    assert data['collision_warning'] == False, f'Should NOT have warning: ultrasonic=120 > 50'
    print(f'No collision at 120cm: OK')

    print()
    print('ALL NEW API TESTS PASSED')

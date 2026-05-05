"""测试路线规划 API"""
import sys, os
sys.path.insert(0, '.')
os.environ['MYSQL_HOST'] = 'localhost'
os.environ['MYSQL_DATABASE'] = 'test_db'

import config as cfg_module
cfg_module.Config.SQLALCHEMY_DATABASE_URI = 'sqlite:///:memory:'

from app import create_app, db
from src.models import User
import bcrypt

app = create_app()

with app.app_context():
    db.create_all()

    # 创建 admin 和 guest
    hashed = bcrypt.hashpw(b'admin123', bcrypt.gensalt()).decode('utf-8')
    db.session.add(User(username='admin', password_hash=hashed, role='admin'))
    hashed2 = bcrypt.hashpw(b'guest123', bcrypt.gensalt()).decode('utf-8')
    db.session.add(User(username='guest', password_hash=hashed2, role='guest'))
    db.session.commit()

    client = app.test_client()

    # 获取 admin token
    resp = client.post('/api/auth/login', json={'username': 'admin', 'password': 'admin123'})
    admin_token = resp.get_json()['access_token']

    # 获取 guest token
    resp = client.post('/api/auth/login', json={'username': 'guest', 'password': 'guest123'})
    guest_token = resp.get_json()['access_token']

    # 1. Admin 下发路线 — 应成功
    route_data = {
        'device_id': 'test-car-01',
        'waypoints': [
            {'lat': 39.9042, 'lng': 116.4074},
            {'lat': 39.9050, 'lng': 116.4100},
            {'lat': 39.9060, 'lng': 116.4130},
        ]
    }
    resp = client.post('/api/vehicle/route',
                       json=route_data,
                       headers={'Authorization': f'Bearer {admin_token}'})
    # MQTT 未连接会返回 503，但路由逻辑应正确
    assert resp.status_code in (200, 503), f'Unexpected: {resp.status_code} {resp.get_json()}'
    if resp.status_code == 200:
        data = resp.get_json()
        assert data['waypoint_count'] == 3
        print(f'Route dispatch: waypoints={data["waypoint_count"]} OK')
    else:
        print(f'Route dispatch: MQTT not connected (expected in test), status=503 OK')

    # 2. Guest 下发路线 — 应被 403 拦截
    resp = client.post('/api/vehicle/route',
                       json=route_data,
                       headers={'Authorization': f'Bearer {guest_token}'})
    assert resp.status_code == 403, f'Guest should be 403, got {resp.status_code}'
    print(f'Guest route dispatch: 403 RBAC deny OK')

    # 3. 航点不足 — 应 400
    resp = client.post('/api/vehicle/route',
                       json={'device_id': 'x', 'waypoints': [{'lat': 1, 'lng': 2}]},
                       headers={'Authorization': f'Bearer {admin_token}'})
    assert resp.status_code == 400
    print(f'Too few waypoints: 400 OK')

    # 4. 航点格式错误 — 应 400
    resp = client.post('/api/vehicle/route',
                       json={'device_id': 'x', 'waypoints': [{'lat': 1}, {'lng': 2}]},
                       headers={'Authorization': f'Bearer {admin_token}'})
    assert resp.status_code == 400
    print(f'Bad waypoint format: 400 OK')

    # 5. 查询路线（无连接时路线不会被缓存）
    resp = client.get('/api/vehicle/route/nonexistent',
                      headers={'Authorization': f'Bearer {admin_token}'})
    assert resp.status_code == 404
    print(f'No route record: 404 OK')

    print()
    print('ALL ROUTE PLANNING TESTS PASSED')

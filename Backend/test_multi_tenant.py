import requests

BASE_URL = 'http://127.0.0.1:5000/api'

def test_horizontal_privilege_escalation():
    print("=== 开始测试多租户与水平越权隔离 ===")
    
    # 1. 注册 User A 和 User B
    requests.post(f'{BASE_URL}/auth/register', json={'username': 'tenant_a', 'password': 'password123'})
    requests.post(f'{BASE_URL}/auth/register', json={'username': 'tenant_b', 'password': 'password123'})
    print("[+] User A 和 User B 注册成功")

    # 2. 登录获取 Token
    res_a = requests.post(f'{BASE_URL}/auth/login', json={'username': 'tenant_a', 'password': 'password123'})
    token_a = res_a.json()['access_token']
    
    res_b = requests.post(f'{BASE_URL}/auth/login', json={'username': 'tenant_b', 'password': 'password123'})
    token_b = res_b.json()['access_token']
    
    headers_a = {'Authorization': f'Bearer {token_a}'}
    headers_b = {'Authorization': f'Bearer {token_b}'}
    print("[+] 登录成功，获取 JWT Token")

    # 3. User A 创建自己的设备
    device_id_a = 'CAR_A_001'
    res = requests.post(f'{BASE_URL}/devices/', json={'device_id': device_id_a, 'name': 'A的车'}, headers=headers_a)
    assert res.status_code == 201, "User A 创建设备失败"
    print(f"[+] User A 成功创建设备: {device_id_a}")

    # 4. 越权测试：User B 试图获取 User A 的设备详情
    res = requests.get(f'{BASE_URL}/devices/{device_id_a}', headers=headers_b)
    assert res.status_code == 403, f"预期 403，实际返回 {res.status_code}"
    print(f"[+] 越权防御成功 (GET)：User B 无法读取 User A 的设备信息")

    # 5. 越权测试：User B 试图控制 User A 的设备 (发送前进指令)
    res = requests.post(f'{BASE_URL}/vehicle/command', json={'device_id': device_id_a, 'command': 'forward'}, headers=headers_b)
    assert res.status_code == 403, f"预期 403，实际返回 {res.status_code}"
    print(f"[+] 越权防御成功 (POST)：User B 无法遥控 User A 的设备")
    
    print("=== 测试通过！多租户隔离与水平越权拦截已生效 ===")

if __name__ == '__main__':
    try:
        test_horizontal_privilege_escalation()
    except Exception as e:
        print(f"测试失败请检查服务是否启动或数据库结构: {e}")

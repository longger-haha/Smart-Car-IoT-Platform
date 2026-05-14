import requests
import time
import hashlib
import hmac
import json
import uuid
import sys

BASE_URL = "http://localhost:5000/api"
HMAC_SECRET = b'SmartRover_Cruise_Secret_2026!'

# Define colors for output
RED = '\033[91m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
RESET = '\033[0m'

def print_test_header(title):
    print(f"\n{YELLOW}=== {title} ==={RESET}")

def print_result(success, message):
    if success:
        print(f"[{GREEN}PASS{RESET}] {message}")
    else:
        print(f"[{RED}FAIL{RESET}] {message}")

def generate_signature(payload_dict):
    sorted_str = '&'.join(
        f'{k}={v}' for k, v in sorted(payload_dict.items())
        if k != 'signature'
    )
    return hmac.new(HMAC_SECRET, sorted_str.encode('utf-8'), hashlib.sha256).hexdigest()

def test_ddos():
    print_test_header("Test 1: DDoS Rate Limiting")
    # Using a non-existent endpoint or a public one to test global rate limit quickly
    # We will spam the health endpoint
    url = f"{BASE_URL}/health"
    
    # Send 250 requests (limit is 200/min)
    hit_429 = False
    print("Sending 250 requests to /api/health...")
    for i in range(250):
        try:
            r = requests.get(url, timeout=1)
            if r.status_code == 429:
                hit_429 = True
                print(f"  Hit rate limit at request {i+1}")
                break
        except requests.exceptions.RequestException:
            pass
            
    print_result(hit_429, "DDoS protection (Global 429 Rate Limit)")
    time.sleep(1) # short pause

def test_auth_brute_force():
    print_test_header("Test 2: Auth Brute Force")
    url = f"{BASE_URL}/auth/login"
    
    # We attempt to login with wrong password 6 times
    # We expect a 429 error before reaching the 6th attempt if rate limit is implemented
    hit_429 = False
    print("Sending 6 wrong password login attempts...")
    for i in range(6):
        data = {"username": "admin", "password": f"wrongpassword{i}"}
        r = requests.post(url, json=data)
        print(f"  Attempt {i+1}: Status {r.status_code}")
        if r.status_code == 429:
            hit_429 = True
            break
            
    print_result(hit_429, "Auth brute force protection (Specific 429 Rate Limit)")

def test_replay_attack():
    print_test_header("Test 3: Replay Attack (Nonce Reuse)")
    
    # First we need to login to get a token
    login_res = requests.post(f"{BASE_URL}/auth/login", json={"username": "admin", "password": "admin123"})
    if login_res.status_code != 200:
        print(f"{RED}Failed to login, skipping test.{RESET}")
        return
        
    token = login_res.json().get('access_token')
    headers = {"Authorization": f"Bearer {token}"}
    
    # Device ID for test
    device_id = "test_dev_01"
    
    # We use the route command because it requires signature and is a good target
    url = f"{BASE_URL}/vehicle/route"
    
    ts = int(time.time())
    nonce = str(uuid.uuid4())
    
    payload = {
        'command': 'route',
        'device_id': device_id,
        'timestamp': ts,
        'nonce': nonce,
        'waypoints': [
            {'lat': 39.9, 'lng': 116.4},
            {'lat': 39.901, 'lng': 116.401}
        ]
    }
    # Add signature
    # Wait, the signature generation in the backend expects the exact payload dict.
    # The current validation in `cruise_security.py` might fail if we add extra fields?
    # Actually, the signature generation in `cruise_security.py` sorts all items.
    payload['signature'] = generate_signature(payload)

    print("Sending initial valid request...")
    # It might fail with 404/403 if device doesn't exist, but we care if it hits Replay check.
    r1 = requests.post(url, json=payload, headers=headers)
    print(f"  First request status: {r1.status_code}")
    
    print("Replaying the exact same request...")
    r2 = requests.post(url, json=payload, headers=headers)
    print(f"  Second request status: {r2.status_code} - {r2.text}")
    
    # If the replay protection works against NONCE reuse, it should return 403 with specific error
    is_blocked = (r2.status_code == 403 and 'nonce' in r2.text.lower()) or (r2.status_code == 403 and 'replay' in r2.text.lower() and r1.status_code != 403)
    
    if r1.status_code == r2.status_code and r1.status_code != 403:
         print_result(False, f"Replay attack successful (Both returned {r1.status_code})")
    elif r1.status_code == r2.status_code == 403 and 'nonce' not in r2.text.lower() and 'replay' not in r2.text.lower():
         print_result(False, "Could not properly test replay due to other 403 error (e.g. Device not found/Offline)")
    else:
         print_result(is_blocked, "Replay attack protection (Nonce reuse blocked)")


def test_rbac_deny():
    print_test_header("Test 4: RBAC Deny (Cross-tenant)")
    
    # First, login as admin to create a device
    admin_login = requests.post(f"{BASE_URL}/auth/login", json={"username": "admin", "password": "admin123"})
    if admin_login.status_code == 200:
        admin_token = admin_login.json().get('access_token')
        requests.post(f"{BASE_URL}/devices/", json={"device_id": "admin_device_01", "name": "Admin Rover", "device_type": "rover"}, headers={"Authorization": f"Bearer {admin_token}"})

    # Try to register a new user
    user_id = str(uuid.uuid4())[:8]
    requests.post(f"{BASE_URL}/auth/register", json={"username": f"user_{user_id}", "password": "password123"})
    
    login_res = requests.post(f"{BASE_URL}/auth/login", json={"username": f"user_{user_id}", "password": "password123"})
    if login_res.status_code != 200:
        print(f"{RED}Failed to login as user, skipping test.{RESET}")
        return
        
    token = login_res.json().get('access_token')
    headers = {"Authorization": f"Bearer {token}"}
    
    # Try to send a command to a device we don't own (admin's device or non-existent)
    url = f"{BASE_URL}/vehicle/command"
    payload = {
        'command': 'forward',
        'device_id': 'admin_device_01',
        'timestamp': int(time.time()),
        'speed_pwm': 150
    }
    
    print("Attempting to control unowned device...")
    r = requests.post(url, json=payload, headers=headers)
    print(f"  Status: {r.status_code} - {r.text}")
    
    print_result(r.status_code == 403, "RBAC protection (Cross-tenant blocked)")

def test_signature_invalid():
    print_test_header("Test 5: HMAC Signature Invalid")
    
    login_res = requests.post(f"{BASE_URL}/auth/login", json={"username": "admin", "password": "admin123"})
    if login_res.status_code != 200:
        print(f"{RED}Failed to login, skipping test.{RESET}")
        return
        
    token = login_res.json().get('access_token')
    headers = {"Authorization": f"Bearer {token}"}
    
    url = f"{BASE_URL}/vehicle/route"
    
    ts = int(time.time())
    
    payload = {
        'command': 'route',
        'device_id': 'test_dev_01',
        'timestamp': ts,
        'waypoints': [
            {'lat': 39.9, 'lng': 116.4},
            {'lat': 39.901, 'lng': 116.401}
        ]
    }
    # Add valid signature
    payload['signature'] = generate_signature(payload)
    
    # TAMPER with the payload
    print("Tampering with payload after signature generation...")
    payload['waypoints'][0]['lat'] = 40.0
    
    r = requests.post(url, json=payload, headers=headers)
    print(f"  Status: {r.status_code} - {r.text}")
    
    try:
        response_json = r.json()
        error_msg = response_json.get('error', '')
    except ValueError:
        error_msg = r.text

    print_result(r.status_code == 403 and '安全验证未通过' in error_msg and '签名' in error_msg, "Signature protection (Tampered payload blocked)")

if __name__ == "__main__":
    print(f"{YELLOW}Starting Security Tests against {BASE_URL}{RESET}")
    print("Waiting for backend to be ready...")
    
    # Wait for backend
    ready = False
    for i in range(10):
        try:
            if requests.get(f"{BASE_URL}/health").status_code == 200:
                ready = True
                break
        except Exception:
            pass
        time.sleep(1)
        
    if not ready:
        print(f"{RED}Backend not ready. Ensure it's running.{RESET}")
        sys.exit(1)
        
    test_ddos()
    test_replay_attack()
    test_rbac_deny()
    test_signature_invalid()
    test_auth_brute_force()
    
    print(f"\n{YELLOW}Testing Complete.{RESET}")

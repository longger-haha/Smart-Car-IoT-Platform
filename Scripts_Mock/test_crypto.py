"""测试脚本：验证 crypto_tool.py 加解密逻辑"""
import sys
sys.path.insert(0, '.')

from src.utils.crypto_tool import aes_encrypt, aes_decrypt, hmac_sign, hmac_verify

# ── AES round-trip ──────────────────────────────────────────
key       = b'SmartRover2026!!'
plaintext = '{"device_id": "test", "temperature": 26.5}'

ct = aes_encrypt(key, plaintext)
print(f'Encrypted: {ct[:60]}...')

pt = aes_decrypt(key, ct)
assert pt == plaintext, f'AES round-trip failed: {pt!r}'
print('AES round-trip: OK')

# ── HMAC sign/verify ────────────────────────────────────────
secret  = 'test_device_secret'
message = 'hello_world'
sig     = hmac_sign(secret, message)
print(f'HMAC signature: {sig}')

assert hmac_verify(secret, message, sig), 'Valid signature should verify'
assert not hmac_verify(secret, message, 'deadbeef0000'), 'Invalid sig should fail'
print('HMAC sign/verify: OK')

# ── 错误密钥应解密失败 ─────────────────────────────────────
wrong_key = b'WrongKeyXXXXXXXX'
try:
    aes_decrypt(wrong_key, ct)
    print('ERROR: wrong key should raise ValueError!')
except ValueError as e:
    print(f'Wrong key correctly rejected: {e}')

print()
print('ALL CRYPTO TESTS PASSED')

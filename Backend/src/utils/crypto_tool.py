"""
crypto_tool.py — AES-128 CBC 加密/解密 + HMAC-SHA256 签名/验签

用法示例:
    from src.utils.crypto_tool import aes_encrypt, aes_decrypt, hmac_sign, hmac_verify

    key = b'SmartRover2026!!'          # 16 字节 AES 密钥
    ciphertext_b64 = aes_encrypt(key, '{"msg":"hello"}')
    plaintext      = aes_decrypt(key, ciphertext_b64)

    secret  = 'device_secret_abc'
    sig     = hmac_sign(secret, '{"ts":1234567890}')
    is_ok   = hmac_verify(secret, '{"ts":1234567890}', sig)
"""

import base64
import hmac
import hashlib

from Crypto.Cipher import AES
from Crypto.Util.Padding import pad, unpad
from Crypto.Random import get_random_bytes


# ── AES-128 CBC ──────────────────────────────────────────────────────────────

def aes_encrypt(key: bytes, plaintext: str) -> str:
    """
    使用 AES-128 CBC 模式加密明文字符串。

    Args:
        key:       16 字节 AES 密钥
        plaintext: 待加密的 UTF-8 字符串

    Returns:
        Base64 编码的 "<iv_hex>:<ciphertext_b64>" 格式字符串
        （IV 随机生成，跟随密文一起返回）
    """
    if len(key) != 16:
        raise ValueError(f'AES key must be exactly 16 bytes, got {len(key)}')
    iv     = get_random_bytes(16)
    cipher = AES.new(key, AES.MODE_CBC, iv)
    ct     = cipher.encrypt(pad(plaintext.encode('utf-8'), AES.block_size))
    # 格式: base64(iv) + ":" + base64(ciphertext)
    result = base64.b64encode(iv).decode() + ':' + base64.b64encode(ct).decode()
    return result


def aes_decrypt(key: bytes, token: str) -> str:
    """
    解密 aes_encrypt() 输出的密文。

    Args:
        key:   16 字节 AES 密钥
        token: "<base64_iv>:<base64_ciphertext>" 格式字符串

    Returns:
        解密后的 UTF-8 明文字符串

    Raises:
        ValueError: 格式错误或解密失败
    """
    if len(key) != 16:
        raise ValueError(f'AES key must be exactly 16 bytes, got {len(key)}')
    try:
        iv_b64, ct_b64 = token.split(':', 1)
        iv     = base64.b64decode(iv_b64)
        ct     = base64.b64decode(ct_b64)
        cipher = AES.new(key, AES.MODE_CBC, iv)
        plain  = unpad(cipher.decrypt(ct), AES.block_size)
        return plain.decode('utf-8')
    except Exception as e:
        raise ValueError(f'AES decryption failed: {e}') from e


# ── HMAC-SHA256 签名 ─────────────────────────────────────────────────────────

def hmac_sign(secret: str, message: str) -> str:
    """
    对 message 做 HMAC-SHA256 签名。

    Args:
        secret:  设备共享密钥字符串
        message: 待签名的字符串（通常为 JSON payload）

    Returns:
        十六进制小写签名字符串
    """
    sig = hmac.new(
        secret.encode('utf-8'),
        message.encode('utf-8'),
        hashlib.sha256
    ).hexdigest()
    return sig


def hmac_verify(secret: str, message: str, signature: str) -> bool:
    """
    验证 HMAC-SHA256 签名（恒时比较，防时序攻击）。

    Args:
        secret:    设备共享密钥
        message:   原始消息字符串
        signature: 待验证的签名（十六进制）

    Returns:
        True 表示签名合法，False 表示非法
    """
    expected = hmac_sign(secret, message)
    return hmac.compare_digest(expected, signature.lower())


# ── XOR 轻量化加密/解密 (UNO 兼容) ───────────────────────────────────────────

def xor_decrypt(key: bytes, token: str) -> str:
    """
    解密 XOR 加密的遥测数据（Arduino UNO 端使用）。

    数据格式: "XOR:" + base64(xor_encrypt(plaintext, key))

    Args:
        key:   XOR 密钥字节（与 UNO 端 XOR_KEY 一致）
        token: "XOR:<base64_data>" 格式字符串

    Returns:
        解密后的 UTF-8 明文字符串

    Raises:
        ValueError: 格式错误或解密失败
    """
    if not token.startswith('XOR:'):
        raise ValueError('Not an XOR-encrypted token (missing XOR: prefix)')

    try:
        b64_data = token[4:]  # 去掉 "XOR:" 前缀
        xored = base64.b64decode(b64_data)

        # XOR 解密 (与加密相同操作)
        key_len = len(key)
        plain_bytes = bytes(xored[i] ^ key[i % key_len] for i in range(len(xored)))

        return plain_bytes.decode('utf-8')
    except Exception as e:
        raise ValueError(f'XOR decryption failed: {e}') from e


def xor_verify_checksum(secret: str, message: str, signature: str) -> bool:
    """
    验证 UNO 端生成的简易 XOR 校验和签名。

    UNO 端签名算法: 将 JSON 内容与 DEVICE_SECRET 做 XOR, 取前 4 字节转十六进制

    Args:
        secret:    设备共享密钥
        message:   原始消息字符串（去掉 signature 字段后的 JSON）
        signature: 8 字符十六进制签名

    Returns:
        True 表示签名合法
    """
    try:
        sig_buf = [0, 0, 0, 0]
        s_len = len(secret)
        for i, ch in enumerate(message[:200]):
            sig_buf[i % 4] ^= ord(ch) ^ ord(secret[i % s_len])

        expected = ''.join(f'{b:02X}' for b in sig_buf)
        return hmac.compare_digest(expected, signature.upper())
    except Exception:
        return False

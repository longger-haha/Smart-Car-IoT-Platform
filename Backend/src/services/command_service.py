"""
command_service.py — 下行指令服务层

封装向设备发送控制指令的打签名与 MQTT 发布逻辑。
vehicle 路由调用此服务，无需关心 MQTT 细节。

T030 [US3]: 指令服务层
"""

import json
import time

from flask import current_app

from src.utils.crypto_tool import hmac_sign
from src.utils.mqtt_client import publish_command


def send_vehicle_command(device_id: str, command: str,
                         device_secret: str = None) -> dict:
    """
    构建、签名并通过 MQTT 发布下行指令。

    Args:
        device_id:     目标设备 ID
        command:       指令字符串（forward/backward/left/right/stop）
        device_secret: 设备密钥（用于 HMAC 签名）；为 None 时跳过签名

    Returns:
        dict: { 'success': bool, 'payload': dict, 'error': str|None }
    """
    payload = {
        'command':   command,
        'device_id': device_id,
        'timestamp': int(time.time()),
    }

    # 如果提供了 device_secret，对 payload 做 HMAC 签名
    if device_secret:
        msg_body         = json.dumps(payload, sort_keys=True)
        payload['sig']   = hmac_sign(device_secret, msg_body)

    success = publish_command(device_id, payload)

    if success:
        return {'success': True, 'payload': payload, 'error': None}
    else:
        return {
            'success': False,
            'payload': payload,
            'error':   'MQTT publish failed',
        }

"""
cruise_security.py — 自动驾驶巡航指令安全模块

功能:
  1. HMAC-SHA256 签名验证: 防止巡航指令被篡改
  2. 时间戳防重放攻击: 检测过期/重放指令
  3. 异常行为检测:
     - 短时间内大量路线下发 (DDoS/滥用检测)
     - 航点坐标异常跳变检测
     - 设备离线状态下下发指令检测
  4. 安全审计日志记录
"""

import time
import hashlib
import hmac
import logging
from collections import defaultdict, deque
from datetime import datetime, timedelta
from typing import Optional, Tuple, Dict, Any

logger = logging.getLogger(__name__)

HMAC_SECRET = b'SmartRover_Cruise_Secret_2026!'

REPLAY_WINDOW_SECONDS = 300
MAX_ROUTE_DISPATCH_PER_MINUTE = 10
MAX_WAYPOINTS = 50
MAX_COORDINATE_JUMP_DEG = 0.01

_dispatch_history: Dict[str, deque] = defaultdict(lambda: deque(maxlen=100))
_abnormal_events: Dict[str, list] = defaultdict(list)


def generate_signature(payload_dict: dict) -> str:
    """生成巡航指令的 HMAC-SHA256 签名（供前端调用）"""
    sorted_str = '&'.join(
        f'{k}={v}' for k, v in sorted(payload_dict.items())
        if k != 'signature'
    )
    sig = hmac.new(HMAC_SECRET, sorted_str.encode('utf-8'), hashlib.sha256).hexdigest()
    return sig


def verify_signature(payload_dict: dict) -> Tuple[bool, str]:
    """
    验证巡航指令签名

    Args:
        payload_dict: 包含 signature 字段的完整 payload

    Returns:
        (is_valid, reason) 元组
    """
    received_sig = payload_dict.get('signature', '')
    if not received_sig:
        return False, '缺少签名字段'

    expected_sig = generate_signature(payload_dict)

    if not hmac.compare_digest(received_sig, expected_sig):
        return False, '签名不匹配，数据可能被篡改'

    return True, ''


def check_replay_attack(timestamp: int) -> Tuple[bool, str]:
    """
    检测重放攻击

    Args:
        timestamp: 指令中的 Unix 时间戳

    Returns:
        (is_safe, reason) 元组
    """
    now = int(time.time())
    diff = abs(now - timestamp)

    if diff > REPLAY_WINDOW_SECONDS:
        return False, f'时间戳偏差 {diff}s > {REPLAY_WINDOW_SECONDS}s 阈值，可能为重放攻击'

    if timestamp > now + 60:
        return False, '时间戳在未来超过60s，可能为时钟攻击'

    return True, ''


def check_rate_limit(device_id: str) -> Tuple[bool, str]:
    """
    检测设备级别的路线下发频率限制

    Args:
        device_id: 目标设备 ID

    Returns:
        (is_safe, reason) 元组
    """
    now = time.time()
    cutoff = now - 60

    history = _dispatch_history[device_id]
    while history and history[0] < cutoff:
        history.popleft()

    if len(history) >= MAX_ROUTE_DISPATCH_PER_MINUTE:
        return False, f'该设备1分钟内已下发 {len(history)} 次，超过 {MAX_ROUTE_DISPATCH_PER_MINUTE} 次/分钟 限制'

    history.append(now)
    return True, ''


def detect_coordinate_anomaly(waypoints: list) -> Tuple[bool, str]:
    """
    检测航点坐标异常跳变

    Args:
        waypoints: [{lat, lng}, ...] 坐标数组

    Returns:
        (is_normal, reason) 元组
    """
    if not waypoints or len(waypoints) < 2:
        return True, ''

    for i in range(1, len(waypoints)):
        prev = waypoints[i - 1]
        curr = waypoints[i]

        lat_diff = abs(curr['lat'] - prev['lat'])
        lng_diff = abs(curr['lng'] - prev['lng'])

        if lat_diff > MAX_COORDINATE_JUMP_DEG or lng_diff > MAX_COORDINATE_JUMP_DEG:
            return (
                False,
                f'航点 {i}→{i+1} 坐标跳变过大 '
                f'(Δlat={lat_diff:.6f}, Δlng={lng_diff:.6f})',
            )

    for wp in waypoints:
        lat, lng = wp.get('lat'), wp.get('lng')
        if lat is None or lng is None:
            continue
        if not (-90 <= lat <= 90):
            return False, f'纬度值 {lat} 超出有效范围 [-90, 90]'
        if not (-180 <= lng <= 180):
            return False, f'经度值 {lng} 超出有效范围 [-180, 180]'

    return True, ''


def validate_cruise_command(data: dict, device_online: bool = True) -> Tuple[bool, list]:
    """
    综合验证巡航指令安全性（入口函数）

    Args:
        data: 完整的请求 payload
        device_online: 目标设备是否在线

    Returns:
        (passed, warnings_list) 元组
        passed=True 表示通过所有检查
        warnings 包含安全提示信息（非阻塞）
    """
    errors = []
    warnings = []

    device_id = data.get('device_id', '')
    timestamp = data.get('timestamp')
    waypoints = data.get('waypoints', [])
    signature = data.get('signature')

    if signature:
        valid, reason = verify_signature(data)
        if not valid:
            errors.append(f'[签名验证失败] {reason}')
        else:
            warnings.append('[签名验证] ✅ 通过')
    else:
        warnings.append('[签名验证] ⚠️ 未提供签名（建议启用）')

    if timestamp:
        safe, reason = check_replay_attack(timestamp)
        if not safe:
            errors.append(f'[重放检测] {reason}')
        else:
            warnings.append('[时间戳校验] ✅ 正常')
    else:
        warnings.append('[时间戳校验] ⚠️ 无时间戳')

    if device_id:
        rate_ok, reason = check_rate_limit(device_id)
        if not rate_ok:
            errors.append(f'[频率限制] {reason}')

        if not device_online:
            warnings.append(f'[设备状态] ⚠️ 设备 {device_id} 当前离线，指令可能无法送达')

    if waypoints:
        coord_ok, reason = detect_coordinate_anomaly(waypoints)
        if not coord_ok:
            errors.append(f'[坐标异常] {reason}')

    return len(errors) == 0, errors + warnings


def record_abnormal_event(event_type: str, device_id: str, detail: str, severity: str = 'warning'):
    """记录异常事件用于后续分析"""
    event = {
        'event_type': event_type,
        'device_id': device_id,
        'detail': detail,
        'severity': severity,
        'timestamp': datetime.now().isoformat(),
    }
    _abnormal_events[device_id].append(event)

    if len(_abnormal_events[device_id]) > 50:
        _abnormal_events[device_id] = _abnormal_events[device_id][-50:]

    logger.warning(
        f'[CRUISE_SECURITY] 异常事件: type={event_type} device={device_id!r} '
        f'detail={detail!r} severity={severity}'
    )


def get_device_risk_score(device_id: str) -> dict:
    """
    计算设备风险评分（0-100，越高越危险）

    基于:
      - 近期异常事件数量
      - 路线下发频率
      - 签名验证失败次数
    """
    events = _abnormal_events.get(device_id, [])
    recent = [e for e in events if e['timestamp'] >= (datetime.now() - timedelta(hours=1)).isoformat()]

    score = 0
    factors = []

    critical_count = sum(1 for e in recent if e['severity'] == 'critical')
    warning_count = sum(1 for e in recent if e['severity'] == 'warning')

    score += min(critical_count * 25, 50)
    factors.append(f'严重异常 x{critical_count}')

    score += min(warning_count * 5, 30)
    factors.append(f'警告事件 x{warning_count}')

    dispatches = len(_dispatch_history.get(device_id, []))
    if dispatches > MAX_ROUTE_DISPATCH_PER_MINUTE:
        score += 20
        factors.append(f'高频下发 ({dispatches}/min)')

    score = min(score, 100)

    level = '低风险' if score < 30 else ('中风险' if score < 70 else '高风险')

    return {
        'score': score,
        'level': level,
        'recent_event_count': len(recent),
        'factors': factors,
    }

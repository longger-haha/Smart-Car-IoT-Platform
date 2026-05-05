"""
heartbeat_checker.py — 设备心跳超时检测

后台守护线程，每 30 秒扫描一次所有 status='online' 的设备：
- 若 last_seen_at 距当前超过 60 秒，标记为 offline
- 写入日志用于调试

启动方式:
    from src.utils.heartbeat_checker import start_heartbeat_checker
    start_heartbeat_checker(app)
"""

import threading
import logging
import time
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

# 心跳超时阈值 (秒)：超过此时间未收到消息则标记为 offline
HEARTBEAT_TIMEOUT_SECONDS = 60

# 检查间隔 (秒)
CHECK_INTERVAL_SECONDS = 30


def _check_heartbeats(app):
    """在 Flask 上下文中检查设备心跳"""
    with app.app_context():
        from app import db
        from src.models.device import Device

        cutoff = datetime.utcnow() - timedelta(seconds=HEARTBEAT_TIMEOUT_SECONDS)

        stale_devices = (
            Device.query
            .filter_by(status='online')
            .filter(
                (Device.last_seen_at < cutoff) | (Device.last_seen_at.is_(None))
            )
            .all()
        )

        if stale_devices:
            for dev in stale_devices:
                dev.status = 'offline'
                logger.info(
                    f'[Heartbeat] Device {dev.device_id!r} marked offline '
                    f'(last_seen_at={dev.last_seen_at})'
                )
            db.session.commit()


def start_heartbeat_checker(app):
    """启动后台心跳检测线程"""

    def _run():
        logger.info(
            f'[Heartbeat] Checker started (timeout={HEARTBEAT_TIMEOUT_SECONDS}s, '
            f'interval={CHECK_INTERVAL_SECONDS}s)'
        )
        while True:
            try:
                _check_heartbeats(app)
            except Exception as e:
                logger.error(f'[Heartbeat] Check failed: {e}', exc_info=True)
            time.sleep(CHECK_INTERVAL_SECONDS)

    thread = threading.Thread(target=_run, daemon=True, name='heartbeat-checker')
    thread.start()

import threading
import logging
import time
from datetime import datetime, timedelta

logger = logging.getLogger(__name__)

HEARTBEAT_TIMEOUT_SECONDS = 60
CHECK_INTERVAL_SECONDS = 30

_db = None
_Device = None


def _check_heartbeats(app):
    global _db, _Device
    if _db is None:
        with app.app_context():
            from src.extensions import db as _db_ref
            from src.models.device import Device as _Device_ref
            _db = _db_ref
            _Device = _Device_ref

    with app.app_context():
        cutoff = datetime.utcnow() - timedelta(seconds=HEARTBEAT_TIMEOUT_SECONDS)
        stale_devices = (
            _Device.query
            .filter_by(status='online')
            .filter(
                (_Device.last_seen_at < cutoff) | (_Device.last_seen_at.is_(None))
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
            _db.session.commit()


def start_heartbeat_checker(app):
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

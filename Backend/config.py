import os
from dotenv import load_dotenv

load_dotenv()

class Config:
    # ── Flask ──────────────────────────────────────────────────────
    SECRET_KEY = os.getenv('FLASK_SECRET_KEY', 'dev-secret-change-in-production')
    DEBUG = os.getenv('FLASK_DEBUG', 'false').lower() == 'true'

    # ── JWT ────────────────────────────────────────────────────────
    JWT_SECRET_KEY = os.getenv('JWT_SECRET_KEY', 'jwt-secret-change-in-production')
    JWT_ACCESS_TOKEN_EXPIRES = 86400  # 24 小时，单位秒

    # ── MySQL (via SQLAlchemy) ───────────────────────────────────
    MYSQL_HOST     = os.getenv('MYSQL_HOST', '127.0.0.1')
    MYSQL_PORT     = int(os.getenv('MYSQL_PORT', '3306'))
    MYSQL_USER     = os.getenv('MYSQL_USER', 'sruser')
    MYSQL_PASSWORD = os.getenv('MYSQL_PASSWORD', 'srpass123')
    MYSQL_DATABASE = os.getenv('MYSQL_DATABASE', 'smartrover_db')

    SQLALCHEMY_DATABASE_URI = (
        f"mysql+pymysql://{MYSQL_USER}:{MYSQL_PASSWORD}"
        f"@{MYSQL_HOST}:{MYSQL_PORT}/{MYSQL_DATABASE}?charset=utf8mb4"
    )
    SQLALCHEMY_TRACK_MODIFICATIONS = False

    # ── MQTT ──────────────────────────────────────────────────────
    MQTT_BROKER_HOST = os.getenv('MQTT_BROKER_HOST', 'localhost')
    MQTT_BROKER_PORT = int(os.getenv('MQTT_BROKER_PORT', '1883'))
    MQTT_USERNAME    = os.getenv('MQTT_USERNAME', '')
    MQTT_PASSWORD    = os.getenv('MQTT_PASSWORD', '')

    # ── 安全参数 ────────────────────────────────────────────────
    # 防重放时间容忍窗口（秒），超出则视为重放攻击
    REPLAY_WINDOW_SECONDS = int(os.getenv('REPLAY_WINDOW_SECONDS', '30'))
    # AES 加密密钥（16 字节）
    AES_KEY = os.getenv('AES_KEY', 'SmartRover2026!!').encode('utf-8')[:16]

    # ── WiFi 定位 ──────────────────────────────────────────────
    # Google Geolocation API Key (ESP32 无 GPS 时使用 WiFi AP 定位)
    GOOGLE_GEOLOCATION_API_KEY = os.getenv('GOOGLE_GEOLOCATION_API_KEY', '')

    # ── 导航引擎参数 ────────────────────────────────────────────
    NAV_PID_KP           = float(os.getenv('NAV_PID_KP', '2.5'))
    NAV_PID_KI           = float(os.getenv('NAV_PID_KI', '0.02'))
    NAV_PID_KD           = float(os.getenv('NAV_PID_KD', '0.8'))
    NAV_PID_OUTPUT_LIMIT = float(os.getenv('NAV_PID_OUTPUT_LIMIT', '120.0'))
    NAV_BASE_CRUISE_PWM  = int(os.getenv('NAV_BASE_CRUISE_PWM', '160'))
    NAV_ARRIVAL_RADIUS_M = float(os.getenv('NAV_ARRIVAL_RADIUS_M', '3.0'))
    NAV_COMMAND_INTERVAL = float(os.getenv('NAV_COMMAND_INTERVAL', '1.0'))
    NAV_LOW_BATTERY_MV   = int(os.getenv('NAV_LOW_BATTERY_MV', '10500'))
    NAV_CRIT_BATTERY_MV  = int(os.getenv('NAV_CRIT_BATTERY_MV', '10000'))
    NAV_SAFE_DISTANCE_CM = float(os.getenv('NAV_SAFE_DISTANCE_CM', '50.0'))
    NAV_CRIT_DISTANCE_CM = float(os.getenv('NAV_CRIT_DISTANCE_CM', '20.0'))

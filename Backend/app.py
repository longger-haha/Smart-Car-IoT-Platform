from flask import Flask
from flask_sqlalchemy import SQLAlchemy
from flask_jwt_extended import JWTManager
from flask_cors import CORS
from flask_limiter import Limiter
from flask_limiter.util import get_remote_address
from config import Config

# 全局扩展实例（在蓝图中导入使用）
db      = SQLAlchemy()
jwt     = JWTManager()
limiter = Limiter(key_func=get_remote_address, default_limits=["200 per minute"])


def create_app(config_class=Config):
    """Flask Application Factory"""
    app = Flask(__name__)
    app.config.from_object(config_class)

    # ── 初始化扩展 ──────────────────────────────────────────────
    db.init_app(app)
    jwt.init_app(app)
    limiter.init_app(app)
    CORS(app, resources={r"/api/*": {"origins": "*"}})

    # ── 注册蓝图 ────────────────────────────────────────────────
    from src.routes.auth    import auth_bp
    from src.routes.devices import devices_bp
    from src.routes.telemetry import telemetry_bp
    from src.routes.vehicle import vehicle_bp
    from src.routes.audit   import audit_bp

    app.register_blueprint(auth_bp,      url_prefix='/api/auth')
    app.register_blueprint(devices_bp,   url_prefix='/api/devices')
    app.register_blueprint(telemetry_bp, url_prefix='/api/telemetry')
    app.register_blueprint(vehicle_bp,   url_prefix='/api/vehicle')
    app.register_blueprint(audit_bp,     url_prefix='/api/audit')

    # ── 健康检查 ────────────────────────────────────────────────
    @app.get('/api/health')
    def health():
        return {'status': 'ok', 'service': 'SmartRover Backend'}

    return app


if __name__ == '__main__':
    app = create_app()

    # 启动时同时连接 MQTT（长驻线程）
    from src.utils.mqtt_client import start_mqtt
    start_mqtt(app)

    app.run(host='0.0.0.0', port=5000, debug=app.config['DEBUG'])

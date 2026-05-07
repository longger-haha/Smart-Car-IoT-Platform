from flask import Flask
from flask_cors import CORS
from config import Config
from src.extensions import db, jwt, limiter


def create_app(config_class=Config):
    app = Flask(__name__)
    app.config.from_object(config_class)

    db.init_app(app)
    jwt.init_app(app)
    limiter.init_app(app)
    CORS(app, resources={r"/api/*": {"origins": "*"}})

    import src.models.user
    import src.models.device
    import src.models.telemetry
    import src.models.audit_log
    import src.models.navigation_event

    with app.app_context():
        db.create_all()

    from src.routes.auth      import auth_bp
    from src.routes.devices   import devices_bp
    from src.routes.telemetry import telemetry_bp
    from src.routes.vehicle   import vehicle_bp
    from src.routes.audit     import audit_bp
    from src.routes.dashboard import dashboard_bp

    app.register_blueprint(auth_bp,      url_prefix='/api/auth')
    app.register_blueprint(devices_bp,   url_prefix='/api/devices')
    app.register_blueprint(telemetry_bp, url_prefix='/api/telemetry')
    app.register_blueprint(vehicle_bp,   url_prefix='/api/vehicle')
    app.register_blueprint(audit_bp,     url_prefix='/api/audit')
    app.register_blueprint(dashboard_bp, url_prefix='/api/dashboard')

    @app.get('/api/health')
    def health():
        return {'status': 'ok', 'service': 'SmartRover Backend'}

    return app


if __name__ == '__main__':
    app = create_app()

    from src.utils.mqtt_client import start_mqtt
    start_mqtt(app)

    from src.utils.heartbeat_checker import start_heartbeat_checker
    start_heartbeat_checker(app)

    app.run(host='0.0.0.0', port=5000, debug=app.config['DEBUG'])

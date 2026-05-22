from app import create_app
from src.utils.mqtt_client import start_mqtt
from src.utils.heartbeat_checker import start_heartbeat_checker

application = create_app()
start_mqtt(application)
start_heartbeat_checker(application)

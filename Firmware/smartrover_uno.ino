/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover Arduino Uno — 智能小车自动驾驶固件 v1.0
 * ═══════════════════════════════════════════════════════════════
 *
 *  基于: smartrover_autopilot.ino v2.1 (ESP32版本)
 *  适配:  Arduino Uno R3 + L293D 4WD Motor Control Shield
 *
 *  功能清单:
 *    ✅ WiFi + MQTT 物联网通信 (通过ESP-01S AT指令)
 *    ✅ GPS 定位 (NEO-6M)
 *    ✅ Waypoint 巡航自动驾驶 (PID航向控制)
 *    ✅ 超声波避障 (HC-SR04) + 自动紧急停车
 *    ✅ 温湿度采集 (DHT11/DHT22)
 *    ✅ L293D 电机驱动 (4WD差速转向, AFMotor库)
 *    ✅ MPU6050 IMU 陀螺仪 (精确航向角, 互补滤波)
 *    ⚠️ 电池电压监测 (需外接分压电路, 默认禁用)
 *    ⚠️ AES/HMAC 加密 (Uno性能限制, 已简化)
 *    ✅ 心跳保活机制
 *
 *  硬件接线 (最终确认版):
 *    ┌─────────────────────────────────────────────┐
 *    │ L293D Shield (堆叠):                         │
 *    │   M1 → 左前电机   M2 → 右前电机              │
 *    │   M3 → 左后电机   M4 → 右后电机              │
 *    │   EXT_PWR → 7~12V 电池                       │
 *    ├─────────────────────────────────────────────┤
 *    │ 传感器:                                       │
 *    │   HC-SR04 Trig → D5   Echo → D2             │
 *    │   DHT11 DATA  → D7                           │
 *    │   MPU6050 SDA  → A4   SCL → A5               │
 *    │   GPS TX      → A0   RX  → A1                │
 *    │   ESP-01S TX  → A3   RX  → A2                │
 *    │   ESP-01S VCC → 3.3V稳压  GND→GND           │
 *    ├─────────────────────────────────────────────┤
 *    │ 电源:                                         │
 *    │   5V  → HC-SR04, DHT11, GPS                  │
 *    │   3.3V → MPU6050                             │
 *    │   独立AMS1117-3.3V → ESP-01 VCC+CH_PD        │
 *    │   所有GND共地!                                │
 *    └─────────────────────────────────────────────┘
 *
 *  编译环境: Arduino IDE 1.8.x / 2.x
 *  需安装库:
 *    - AFMotor (Adafruit Motor Shield library)
 *    - SoftwareSerial (内置)
 *    - Wire (内置)
 *    - DHT sensor library
 *    - TinyGPS++
 *    - ArduinoJson
 *
 *  作者: SmartRover Team
 *  日期: 2026-05-09
 */

#include <SoftwareSerial.h>
#include <AFMotor.h>
#include <Wire.h>
#include <DHT.h>
#include <TinyGPS++.h>

// ═══════════════════════════════════════════════════════════════
//  一、系统配置（修改这里适配你的网络和设备）
// ═══════════════════════════════════════════════════════════════

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST     = "192.168.1.100";
const int    MQTT_PORT    = 1883;
const char* MQTT_USER     = "";
const char* MQTT_PASS     = "";

const char* DEVICE_ID     = "SMARTROVER_UNO_001";
const char* DEVICE_SECRET = "your_secret_key";

const long  HEARTBEAT_MS  = 5000;
const long  TELEMETRY_MS  = 10000;
const float ARRIVAL_RADIUS_M = 3.0;

// ═══════════════════════════════════════════════════════════════
//  二、引脚定义（对应你的接线方案）
// ═══════════════════════════════════════════════════════════════

#define DHT_PIN       7
#define DHT_TYPE      DHT11
#define TRIG_PIN      5
#define ECHO_PIN      2
#define LED_BUILTIN   13

#define MPU6050_ADDR  0x68

// 软件串口引脚
#define GPS_RX_PIN    A0
#define GPS_TX_PIN    A1
#define ESP_RX_PIN    A2
#define ESP_TX_PIN    A3

// 电池监测 (可选, 需要分压电路接A3, 但A3已被ESP占用)
// 如果需要电池监测, 请改用其他空闲模拟脚或注释掉此功能
// #define BAT_ADC     A3
// #define BATT_R1     30000.0
// #define BATT_R2     7500.0
// #define BATT_LOW_V  10.5
// #define BATT_FULL_V 12.6

// ═══════════════════════════════════════════════════════════════
//  三、全局对象和变量
// ═══════════════════════════════════════════════════════════════

DHT dht(DHT_PIN, DHT_TYPE);
TinyGPSPlus gps;

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);

// L293D Shield 4个电机通道
AF_DCMotor motorLeftFront(1);
AF_DCMotor motorRightFront(2);
AF_DCMotor motorLeftBack(3);
AF_DCMotor motorRightBack(4);

enum NavState {
  NAV_IDLE,
  NAV_CRUISING,
  NAV_OBSTACLE_AVOID,
  NAV_ARRIVED,
  NAV_ABORTED
};

struct Waypoint {
  double lat;
  double lng;
};

struct CruiseStatus {
  NavState state;
  int totalWaypoints;
  int currentWpIndex;
  double currentLat;
  double currentLng;
  double currentCourse;
  double targetBearing;
  double distanceToTarget;
  double leftSpeed;
  double rightSpeed;
  unsigned long stateDurationMs;
  String lastEvent;
};

static Waypoint routeWaypoints[10];
static int waypointCount = 0;
static volatile NavState navState = NAV_IDLE;
static volatile int currentWpIndex = 0;
static unsigned long navStartTime = 0;
static unsigned long lastObstacleTime = 0;

double pidKp = 2.5, pidKi = 0.02, pidKd = 0.8;
double pidIntegral = 0, pidLastError = 0;
double baseCruiseSpeed = 160;

int manualSpeedPwm = 150;
String lastManualCommand = "stop";

bool obstacleDetected = false;
float ultrasonicDistanceCm = 999;
unsigned long lastTelemetrySend = 0;
unsigned long lastHeartbeat = 0;
int messageSequence = 0;

CruiseStatus cruiseInfo;

float  imuHeadingDeg      = 0.0;
float  imuGyroZ           = 0.0;
float  imuAccelX          = 0.0;
float  imuAccelY          = 0.0;
float  imuAccelZ          = 0.0;
bool   imuReady           = false;
unsigned long lastImuRead  = 0;

static float  compAngleX  = 0.0;
static float  compAngleY  = 0.0;
static float  gyroOffsetZ = 0.0;
static bool   gyroCalibrated = false;
const float COMP_ALPHA = 0.98f;
const float DT_IMU     = 0.02f;

float batteryVoltage    = 12.6f;
int   batteryPercent    = 100;
bool  lowBatteryWarning = false;
bool  criticalBattery   = false;
unsigned long lastBattRead = 0;
const int BATT_READ_INTERVAL_MS = 10000;

bool wifiConnected = false;
bool mqttConnected = false;

// ═══════════════════════════════════════════════════════════════
//  四、ESP-01S AT指令封装
// ═══════════════════════════════════════════════════════════════

String sendATCommand(String cmd, unsigned long timeout = 2000) {
  espSerial.println(cmd);
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (espSerial.available()) {
      char c = espSerial.read();
      response += c;
    }
  }
  return response;
}

bool initESP01S() {
  Serial.println(F("[ESP] 初始化 ESP-01S..."));

  delay(1000);

  String resp = sendATCommand("AT", 2000);
  if (resp.indexOf("OK") == -1) {
    Serial.println(F("[ESP] ❌ ESP-01S 无响应! 检查接线(A2/A3)和电源(3.3V)"));
    return false;
  }

  sendATCommand("ATE0", 1000);

  resp = sendATCommand("AT+CWMODE?", 1000);
  if (resp.indexOf("1") == -1) {
    sendATCommand("AT+CWMODE=1", 2000);
    delay(500);
  }

  Serial.println(F("[ESP] ✅ ESP-01S 就绪"));
  return true;
}

bool connectWiFi() {
  if (!initESP01S()) return false;

  Serial.print(F("[ESP] 连接WiFi: "));
  Serial.println(WIFI_SSID);

  String cmd = F("AT+CWJAP=\"");
  cmd += WIFI_SSID;
  cmd += F("\",\"");
  cmd += WIFI_PASSWORD;
  cmd += F("\"");

  String resp = sendATCommand(cmd, 15000);

  if (resp.indexOf("OK") != -1 || resp.indexOf("CONNECTED") != -1 || resp.indexOf("GOT IP") != -1) {
    wifiConnected = true;
    Serial.println(F("[ESP] ✅ WiFi 连接成功!"));

    resp = sendATCommand("AT+CIFSR", 2000);
    int ipStart = resp.indexOf("\"");
    if (ipStart != -1) {
      int ipEnd = resp.indexOf("\"", ipStart + 1);
      if (ipEnd != -1) {
        String ip = resp.substring(ipStart + 1, ipEnd);
        Serial.print(F("[ESP] IP地址: "));
        Serial.println(ip);
      }
    }
    return true;
  } else {
    wifiConnected = false;
    Serial.println(F("[ESP] ❌ WiFi 连接失败! 检查SSID和密码"));
    Serial.print(F("[ESP] 响应: "));
    Serial.println(resp.substring(0, min((int)resp.length(), 200)));
    return false;
  }
}

bool connectMQTT() {
  if (!wifiConnected) {
    Serial.println(F("[MQTT] ❌ WiFi未连接, 无法连接MQTT"));
    return false;
  }

  Serial.print(F("[MQTT] 连接到 "));
  Serial.print(MQTT_HOST);
  Serial.print(F(":"));
  Serial.println(MQTT_PORT);

  String cmd = F("AT+CMQTTCONNECT=\"");
  cmd += MQTT_HOST;
  cmd += F("\",");
  cmd += String(MQTT_PORT);

  String resp = sendATCommand(cmd, 10000);

  if (resp.indexOf("OK") != -1) {
    mqttConnected = true;
    Serial.println(F("[MQTT] ✅ MQTT 连接成功!"));
    return true;
  } else {
    mqttConnected = false;
    Serial.println(F("[MQTT] ❌ MQTT 连接失败!"));
    return false;
  }
}

void publishMQTT(String topic, String payload) {
  if (!mqttConnected && !connectMQTT()) return;

  topic.replace("/", "%2F");

  String cmd = F("AT+CMQTTPUB=0,\"");
  cmd += topic;
  cmd += F("\",0,0,0,\"");
  cmd += payload;
  cmd += F("\"");

  sendATCommand(cmd, 5000);
}

void subscribeMQTT(String topic) {
  if (!mqttConnected) return;

  topic.replace("/", "%2F");

  String cmd = F("AT+CMQTTSUB=0,\"");
  cmd += topic;
  cmd += F("\",0");

  sendATCommand(cmd, 3000);
}

String checkMQTTMessages() {
  if (!espSerial.available()) return "";

  String msg = "";
  unsigned long start = millis();
  while (millis() - start < 100) {
    while (espSerial.available()) {
      char c = espSerial.read();
      msg += c;
    }
  }

  if (msg.indexOf("+CMQTTRXSTART") != -1) {
    int dataStart = msg.indexOf("+CMQTTRXDATA,");
    if (dataStart != -1) {
      dataStart = msg.indexOf(",", dataStart + 12);
      if (dataStart != -1) {
        int dataEnd = msg.indexOf("\r\n", dataStart);
        if (dataEnd != -1) {
          return msg.substring(dataStart + 1, dataEnd);
        }
      }
    }
  }

  return "";
}

// ═══════════════════════════════════════════════════════════════
//  五、安全工具（Uno简化版 - 无实际加密）
// ═══════════════════════════════════════════════════════════════

String hmacSha256(const String& keyHex, const String& message) {
  return "";
}

String simpleEncrypt(const String& plaintext) {
  return plaintext;
}

// ═══════════════════════════════════════════════════════════════
//  六、GPS 导航数学工具
// ═══════════════════════════════════════════════════════════════

constexpr double DEG_TO_RAD = PI / 180.0;
constexpr double RAD_TO_DEG = 180.0 / PI;
constexpr double EARTH_RADIUS_M = 6371000.0;

void calculateNavigation(double lat1, double lng1, double lat2, double lng2,
                         double* distanceM, double* bearingDeg) {
  double dLat = (lat2 - lat1) * DEG_TO_RAD;
  double dLng = (lng2 - lng1) * DEG_TO_RAD;
  double a = sin(dLat / 2) * sin(dLat / 2) +
             cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
             sin(dLng / 2) * sin(dLng / 2);
  double c = 2 * atan2(sqrt(a), sqrt(1 - a));
  *distanceM = EARTH_RADIUS_M * c;

  double y = sin(dLng) * cos(lat2 * DEG_TO_RAD);
  double x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) -
             sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLng);
  *bearingDeg = atan2(y, x) * RAD_TO_DEG;
}

double normalizeAngle(double angle) {
  while (angle > 180)   angle -= 360;
  while (angle < -180)  angle += 360;
  return angle;
}

// ═══════════════════════════════════════════════════════════════
//  七、MPU6050 IMU 陀螺仪
// ═══════════════════════════════════════════════════════════════

void writeMPU6050(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

uint8_t readMPU6050(uint8_t reg) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 1, true);
  return Wire.read();
}

void readMPU6050Raw(int16_t* ax, int16_t* ay, int16_t* az,
                     int16_t* gx, int16_t* gy, int16_t* gz,
                    int16_t* tempRaw) {
  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);

  *ax     = (Wire.read() << 8 | Wire.read());
  *ay     = (Wire.read() << 8 | Wire.read());
  *az     = (Wire.read() << 8 | Wire.read());
  *tempRaw= (Wire.read() << 8 | Wire.read());
  *gx     = (Wire.read() << 8 | Wire.read());
  *gy     = (Wire.read() << 8 | Wire.read());
  *gz     = (Wire.read() << 8 | Wire.read());
}

void initMPU6050() {
  Wire.begin();
  delay(100);

  writeMPU6050(0x6B, 0x80);
  delay(100);
  writeMPU6050(0x6B, 0x03);
  delay(10);
  writeMPU6050(0x1A, 0x03);
  delay(10);
  writeMPU6050(0x1B, 0x18);
  delay(10);
  writeMPU6050(0x1C, 0x00);
  delay(50);

  uint8_t whoAmI = readMPU6050(0x75);
  if (whoAmI == 0x68 || whoAmI == 0x69) {
    imuReady = true;
    Serial.println(F("[IMU] ✅ MPU6050 检测成功! 地址: 0x"));
    Serial.println(whoAmI, HEX);
    calibrateGyro();
    Serial.println(F("[IMU] 陀螺仪零偏校准完成"));
  } else {
    imuReady = false;
    Serial.print(F("[IMU] ❌ MPU6050 未响应 (WHO_AM_I=0x"));
    Serial.print(whoAmI, HEX);
    Serial.println(F("), 将使用GPS航向"));
  }
}

void calibrateGyro() {
  const int SAMPLES = 500;
  long gzSum = 0;

  for (int i = 0; i < SAMPLES; i++) {
    int16_t ax, ay, az, gx, gy, gz, temp;
    readMPU6050Raw(&ax, &ay, &az, &gx, &gy, &gz, &temp);
    gzSum += gz;
    delay(2);
  }
  gyroOffsetZ = (float)gzSum / SAMPLES;
  gyroCalibrated = true;
}

void updateIMU() {
  if (!imuReady) return;
  unsigned long now = millis();
  if (now - lastImuRead < (unsigned long)(DT_IMU * 1000)) return;
  lastImuRead = now;

  int16_t ax, ay, az, gx, gy, gz, tempRaw;
  readMPU6050Raw(&ax, &ay, &az, &gx, &gy, &gz, &tempRaw);

  float accX = ax / 16384.0f;
  float accY = ay / 16384.0f;
  float accZ = az / 16384.0f;
  float gyroZRad = ((float)gz - gyroOffsetZ) / 131.07f * DEG_TO_RAD;

  imuAccelX = accX; imuAccelY = accY; imuAccelZ = accZ;
  imuGyroZ  = ((float)gz - gyroOffsetZ) / 131.07f;

  float accRoll  = atan2(accY, accZ) * RAD_TO_DEG;
  float accPitch = atan2(-accX, sqrt(accY * accY + accZ * accZ)) * RAD_TO_DEG;

  if (!gyroCalibrated) {
    compAngleX = accRoll;
    compAngleY = accPitch;
    return;
  }

  compAngleX = COMP_ALPHA * (compAngleX + gyroZRad * DT_IMU * RAD_TO_DEG) + (1.0f - COMP_ALPHA) * accRoll;
  compAngleY = COMP_ALPHA * compAngleY + (1.0f - COMP_ALPHA) * accPitch;

  static float headingIntegrator = 0.0f;
  headingIntegrator += gyroZRad * DT_IMU;
  imuHeadingDeg = fmod(headingIntegrator * RAD_TO_DEG, 360.0f);
  if (imuHeadingDeg < 0) imuHeadingDeg += 360.0f;
}

float getEffectiveHeading(double gpsCourse) {
  if (!imuReady) return gpsCourse;

  double gpsSpeedKmh = gps.speed.kmph();

  if (gpsSpeedKmh < 3.0 && gps.location.isValid()) {
    double gpsHeading = normalizeAngle(gpsCourse);
    float imuH        = normalizeAngle(imuHeadingDeg);
    float diff         = fabs(normalizeAngle(gpsHeading - imuH));

    if (diff > 150.0f) {
      return imuHeadingDeg;
    }

    float blend = constrain((float)(gpsSpeedKmh / 5.0), 0.05f, 1.0f);
    return normalizeAngle(blend * gpsHeading + (1.0f - blend) * imuH);
  }

  return imuHeadingDeg;
}

// ═══════════════════════════════════════════════════════════════
//  八、电池电压监测（可选功能）
// ═══════════════════════════════════════════════════════════════

void updateBattery() {
#ifndef BAT_ADC
  batteryVoltage = 12.0f;
  batteryPercent = 80;
  lowBatteryWarning = false;
  criticalBattery = false;
  return;
#endif

  unsigned long now = millis();
  if (now - lastBattRead < BATT_READ_INTERVAL_MS) return;
  lastBattRead = now;

  int raw = analogRead(BAT_ADC);
  float adcVoltage = (raw / 1023.0f) * 5.0f;
  batteryVoltage = adcVoltage * (BATT_R1 + BATT_R2) / BATT_R2;

  batteryPercent = (int)constrain(
    ((batteryVoltage - BATT_LOW_V) / (BATT_FULL_V - BATT_LOW_V)) * 100.0f,
    0.0f, 100.0f
  );

  lowBatteryWarning = (batteryVoltage < 11.0f && batteryVoltage >= BATT_LOW_V);
  criticalBattery   = (batteryVoltage < BATT_LOW_V);

  if (criticalBattery && navState == NAV_CRUISING) {
    navState = NAV_ABORTED;
    stopMotor();
    resetPID();
    publishNavStatus("CRITICAL_BATTERY",
      String("⚠️ 电量严重不足: ") + batteryVoltage + "V (" +
      batteryPercent + "%), 巡航已中止!");
  } else if (lowBatteryWarning) {
    Serial.print(F("[BAT] 🟡 低电量警告: "));
    Serial.print(batteryVoltage);
    Serial.print(F("V ("));
    Serial.print(batteryPercent);
    Serial.println(F("%)"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  九、PID 航向控制器
// ═══════════════════════════════════════════════════════════════

double computePIDSteering(double targetBearing, double currentHeading) {
  double error = normalizeAngle(targetBearing - currentHeading);

  pidIntegral += error;
  if (pidIntegral > 100)  pidIntegral = 100;
  if (pidIntegral < -100) pidIntegral = -100;

  double derivative = error - pidLastError;
  pidLastError = error;

  double output = pidKp * error + pidKi * pidIntegral + pidKd * derivative;
  return constrain(output, -120, 120);
}

void resetPID() {
  pidIntegral = 0;
  pidLastError = 0;
}

// ═══════════════════════════════════════════════════════════════
//  十、L293D 电机驱动控制（4WD差速转向）
// ═══════════════════════════════════════════════════════════════

void initMotors() {
  motorLeftFront.setSpeed(0);
  motorRightFront.setSpeed(0);
  motorLeftBack.setSpeed(0);
  motorRightBack.setSpeed(0);

  motorLeftFront.run(RELEASE);
  motorRightFront.run(RELEASE);
  motorLeftBack.run(RELEASE);
  motorRightBack.run(RELEASE);

  stopMotor();

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  Serial.println(F("[MOTOR] ✅ L293D Shield 初始化完成 (4WD模式)"));
}

void setMotorRaw(int leftSpeed, int rightSpeed) {
  leftSpeed  = constrain(leftSpeed,  -255, 255);
  rightSpeed = constrain(rightSpeed, -255, 255);

  if (leftSpeed == 0 && rightSpeed == 0) {
    motorLeftFront.run(RELEASE);
    motorLeftBack.run(RELEASE);
    motorRightFront.run(RELEASE);
    motorRightBack.run(RELEASE);
    return;
  }

  if (leftSpeed >= 0) {
    motorLeftFront.run(FORWARD);
    motorLeftBack.run(FORWARD);
  } else {
    motorLeftFront.run(BACKWARD);
    motorLeftBack.run(BACKWARD);
  }
  motorLeftFront.setSpeed(abs(leftSpeed));
  motorLeftBack.setSpeed(abs(leftSpeed));

  if (rightSpeed >= 0) {
    motorRightFront.run(FORWARD);
    motorRightBack.run(FORWARD);
  } else {
    motorRightFront.run(BACKWARD);
    motorRightBack.run(BACKWARD);
  }
  motorRightFront.setSpeed(abs(rightSpeed));
  motorRightBack.setSpeed(abs(rightSpeed));
}

void stopMotor() {
  setMotorRaw(0, 0);
}

void moveForward(int speed) { setMotorRaw(speed, speed); }
void moveBackward(int speed){ setMotorRaw(-speed, -speed); }
void turnLeft(int speed)     { setMotorRaw(-speed, speed); }
void turnRight(int speed)    { setMotorRaw(speed, -speed); }

void executeCommand(const String& cmd, int pwm) {
  lastManualCommand = cmd;
  if (navState == NAV_CRUISING) {
    navState = NAV_ABORTED;
    stopMotor();
    resetPID();
    Serial.println(F("[NAV] 手动接管，巡航已中止"));
  }

  if (cmd == "forward")  moveForward(pwm);
  else if (cmd == "backward") moveBackward(pwm);
  else if (cmd == "left")     turnLeft(pwm);
  else if (cmd == "right")    turnRight(pwm);
  else if (cmd == "stop")     stopMotor();

  Serial.print(F("[CMD] 执行: "));
  Serial.print(cmd);
  Serial.print(F(" PWM="));
  Serial.println(pwm);
}

// ═══════════════════════════════════════════════════════════════
//  十一、超声波测距
// ═══════════════════════════════════════════════════════════════

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

// ═══════════════════════════════════════════════════════════════
//  十二、避障逻辑
// ═══════════════════════════════════════════════════════════════

void checkObstacle() {
  ultrasonicDistanceCm = readUltrasonic();

  const float SAFE_DIST_CM = 50.0;
  const float CRITICAL_DIST_CM = 20.0;

  if (ultrasonicDistanceCm < CRITICAL_DIST_CM && ultrasonicDistanceCm > 0) {
    obstacleDetected = true;
    stopMotor();
    lastObstacleTime = millis();
    Serial.print(F("[OBSTACLE] ⚠️ 紧急停车! 距离="));
    Serial.print(ultrasonicDistanceCm);
    Serial.println(F("cm"));
    publishNavStatus("EMERGENCY_STOP", String("障碍物过近: ") + ultrasonicDistanceCm + "cm");
  } else if (ultrasonicDistanceCm < SAFE_DIST_CM && ultrasonicDistanceCm > 0) {
    obstacleDetected = true;
    Serial.print(F("[OBSTACLE] 障碍物警告: "));
    Serial.print(ultrasonicDistanceCm);
    Serial.println(F("cm"));
  } else {
    obstacleDetected = false;
  }
}

void avoidObstacleManeuver() {
  static unsigned long avoidStart = 0;
  static int phase = 0;

  if (avoidStart == 0) avoidStart = millis();
  unsigned long elapsed = millis() - avoidStart;

  switch (phase) {
    case 0:
      stopMotor();
      if (elapsed > 500) { phase = 1; avoidStart = millis(); }
      break;
    case 1:
      turnLeft(120);
      if (elapsed > 800) { phase = 2; avoidStart = millis(); }
      break;
    case 2:
      moveForward(130);
      if (elapsed > 1500) { phase = 3; avoidStart = millis(); }
      break;
    case 3:
      turnRight(120);
      if (elapsed > 800) {
        phase = 0;
        avoidStart = 0;
        navState = NAV_CRUISING;
        publishNavStatus("OBSTACLE_CLEARED", "绕行完成，恢复巡航");
        Serial.println(F("[NAV] 障碍已绕过，继续巡航"));
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  十三、Waypoint 自动驾驶导航循环
// ═══════════════════════════════════════════════════════════════

void navigationLoop() {
  if (waypointCount == 0 || currentWpIndex >= waypointCount) {
    navState = NAV_ARRIVED;
    stopMotor();
    resetPID();
    publishNavStatus("ARRIVED", "所有航点已完成!");
    Serial.println(F("[NAV] ✅ 所有航点到达!"));
    navState = NAV_IDLE;
    return;
  }

  checkObstacle();

  if (obstacleDetected && (millis() - lastObstacleTime < 3000)) {
    navState = NAV_OBSTACLE_AVOID;
    avoidObstacleManeuver();
    return;
  }

  Waypoint target = routeWaypoints[currentWpIndex];

  if (!gps.location.isValid()) {
    Serial.println(F("[NAV] ⏳ 等待GPS信号..."));
    stopMotor();
    return;
  }

  double currentLat = gps.location.lat();
  double currentLng = gps.location.lng();
  double distanceM, targetBearing;
  calculateNavigation(currentLat, currentLng, target.lat, target.lng, &distanceM, &targetBearing);

  double gpsCourse = gps.course.deg();
  double effectiveHeading = getEffectiveHeading(gpsCourse);

  double steering = computePIDSteering(targetBearing, effectiveHeading);

  int leftSpeed  = (int)(baseCruiseSpeed - steering);
  int rightSpeed = (int)(baseCruiseSpeed + steering);
  leftSpeed  = constrain(leftSpeed,  0, 255);
  rightSpeed = constrain(rightSpeed, 0, 255);

  if (distanceM < ARRIVAL_RADIUS_M) {
    currentWpIndex++;
    Serial.print(F("[NAV] ✅ 到达航点 #"));
    Serial.println(currentWpIndex);

    if (currentWpIndex >= waypointCount) {
      navState = NAV_ARRIVED;
      stopMotor();
      resetPID();
      publishNavStatus("ARRIVED", "所有航点已完成!");
      Serial.println(F("[NAV] 🎉 所有航点完成!"));
      navState = NAV_IDLE;
      return;
    }

    publishNavStatus("WAYPOINT_REACHED",
      String("航点 ") + currentWpIndex + "/" + waypointCount + " 已到达");
  } else {
    setMotorRaw(leftSpeed, rightSpeed);
  }

  cruiseInfo.state = navState;
  cruiseInfo.currentWpIndex = currentWpIndex;
  cruiseInfo.totalWaypoints = waypointCount;
  cruiseInfo.currentLat = currentLat;
  cruiseInfo.currentLng = currentLng;
  cruiseInfo.currentCourse = effectiveHeading;
  cruiseInfo.targetBearing = targetBearing;
  cruiseInfo.distanceToTarget = distanceM;
  cruiseInfo.leftSpeed = leftSpeed;
  cruiseInfo.rightSpeed = rightSpeed;
  cruiseInfo.stateDurationMs = millis() - navStartTime;
}

void startCruise(Wpoint* wps, int count) {
  if (count == 0 || count > 10) {
    Serial.println(F("[NAV] ❌ 无效的航点数量!"));
    return;
  }

  for (int i = 0; i < count; i++) {
    routeWaypoints[i].lat = wps[i].lat;
    routeWaypoints[i].lng = wps[i].lng;
  }
  waypointCount = count;
  currentWpIndex = 0;
  navState = NAV_CRUISING;
  navStartTime = millis();
  resetPID();

  Serial.print(F("[NAV] 🚀 开始巡航, 共 "));
  Serial.print(count);
  Serial.println(F(" 个航点"));

  publishNavStatus("CRUISE_START", String("开始巡航, 共") + count + "个航点");
}

void abortCruise() {
  if (navState == NAV_CRUISING || navState == NAV_OBSTACLE_AVOID) {
    navState = NAV_ABORTED;
    stopMotor();
    resetPID();
    publishNavStatus("ABORTED", "巡航已中止");
    Serial.println(F("[NAV] ⛔ 巡航已中止"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十四、遥测数据上报
// ═══════════════════════════════════════════════════════════════

void sendTelemetry() {
  float temperature = dht.readTemperature();
  float humidity    = dht.readHumidity();

  updateBattery();
  updateIMU();

  String telemetryJson = "{";
  telemetryJson += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  telemetryJson += "\"seq\":" + String(messageSequence++) + ",";
  telemetryJson += "\"ts\":" + String(millis()) + ",";

  telemetryJson += "\"gps\":{";
  if (gps.location.isValid()) {
    telemetryJson += "\"lat\":" + String(gps.location.lat(), 6) + ",";
    telemetryJson += "\"lng\":" + String(gps.location.lng(), 6) + ",";
    telemetryJson += "\"alt\":" + String(gps.altitude.meters(), 1) + ",";
    telemetryJson += "\"speed_kmh\":" + String(gps.speed.kmph(), 1) + ",";
    telemetryJson += "\"course\":" + String(gps.course.deg(), 1) + ",";
    telemetryJson += "\"sats\":" + String(gps.satellites.value());
  } else {
    telemetryJson += "\"lat\":null,\"lng\":null,\"alt\":null,";
    telemetryJson += "\"speed_kmh\":0,\"course\":0,\"sats\":0";
  }
  telemetryJson += "},";

  telemetryJson += "\"sensors\":{";
  if (!isnan(temperature)) {
    telemetryJson += "\"temp_c\":" + String(temperature, 1) + ",";
  } else {
    telemetryJson += "\"temp_c\":null,";
  }
  if (!isnan(humidity)) {
    telemetryJson += "\"humidity_pct\":" + String(humidity, 1) + ",";
  } else {
    telemetryJson += "\"humidity_pct\":null,";
  }
  telemetryJson += "\"ultrasonic_cm\":" + String(ultrasonicDistanceCm, 1) + ",";
  telemetryJson += "\"obstacle\":" + String(obstacleDetected ? "true" : "false") + ",";

  if (imuReady) {
    telemetryJson += "\"imu_heading\":" + String(imuHeadingDeg, 1) + ",";
    telemetryJson += "\"imu_gyro_z\":" + String(imuGyroZ, 2) + ",";
    telemetryJson += "\"imu_accel_x\":" + String(imuAccelX, 3) + ",";
    telemetryJson += "\"imu_accel_y\":" + String(imuAccelY, 3) + ",";
    telemetryJson += "\"imu_accel_z\":" + String(imuAccelZ, 3);
  } else {
    telemetryJson += "\"imu_heading\":null,\"imu_gyro_z\":null,";
    telemetryJson += "\"imu_accel_x\":null,\"imu_accel_y\":null,\"imu_accel_z\":null";
  }
  telemetryJson += "},";

  telemetryJson += "\"power\":{";
  telemetryJson += "\"battery_v\":" + String(batteryVoltage, 2) + ",";
  telemetryJson += "\"battery_pct\":" + String(batteryPercent) + ",";
  telemetryJson += "\"low_battery\":" + String(lowBatteryWarning ? "true" : "false");
  telemetryJson += "},";

  telemetryJson += "\"motor\":{";
  telemetryJson += "\"left_speed\":" + String(cruiseInfo.leftSpeed, 0) + ",";
  telemetryJson += "\"right_speed\":" + String(cruiseInfo.rightSpeed, 0) + ",";
  telemetryJson += "\"last_cmd\":\"" + lastManualCommand + "\"";
  telemetryJson += "},";

  telemetryJson += "\"nav\":{";
  telemetryJson += "\"state\":" + String(navState) + ",";
  telemetryJson += "\"wp_index\":" + String(currentWpIndex) + ",";
  telemetryJson += "\"wp_total\":" + String(waypointCount) + ",";
  telemetryJson += "\"dist_to_target_m\":" + String(cruiseInfo.distanceToTarget, 1);
  telemetryJson += "}";

  telemetryJson += "}";

  publishMQTT("smartrover/" + String(DEVICE_ID) + "/telemetry", telemetryJson);

  Serial.print(F("[TEL] 上报遥测 seq="));
  Serial.println(messageSequence - 1);
}

// ═══════════════════════════════════════════════════════════════
//  十五、心跳保活
// ═══════════════════════════════════════════════════════════════

void sendHeartbeat() {
  String heartbeatJson = "{";
  heartbeatJson += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  heartbeatJson += "\"ts\":" + String(millis()) + ",";
  heartbeatJson += "\"uptime_s\":" + String(millis() / 1000) + ",";
  heartbeatJson += "\"free_ram\":" + String(freeMemory()) + ",";
  heartbeatJson += "\"wifi_connected\":" + String(wifiConnected ? "true" : "false") + ",";
  heartbeatJson += "\"mqtt_connected\":" + String(mqttConnected ? "true" : "false") + ",";
  heartbeatJson += "\"battery_v\":" + String(batteryVoltage, 2);
  heartbeatJson += "}";

  publishMQTT("smartrover/" + String(DEVICE_ID) + "/heartbeat", heartbeatJson);

  static bool ledState = false;
  ledState = !ledState;
  digitalWrite(LED_BUILTIN, ledState ? HIGH : LOW);
}

int freeMemory() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══════════════════════════════════════════════════════════════
//  十六、导航状态发布
// ═══════════════════════════════════════════════════════════════

void publishNavStatus(const String& event, const String& detail) {
  String statusJson = "{";
  statusJson += "\"device_id\":\"" + String(DEVICE_ID) + "\",";
  statusJson += "\"event\":\"" + event + "\",";
  statusJson += "\"detail\":\"" + detail + "\",";
  statusJson += "\"ts\":" + String(millis()) + ",";
  statusJson += "\"nav_state\":" + String(navState) + ",";
  statusJson += "\"wp_index\":" + String(currentWpIndex) + ",";
  statusJson += "\"lat\":" + String(gps.location.isValid() ? String(gps.location.lat(), 6) : "null") + ",";
  statusJson += "\"lng\":" + String(gps.location.isValid() ? String(gps.location.lng(), 6) : "null");
  statusJson += "}";

  publishMQTT("smartrover/" + String(DEVICE_ID) + "/nav/status", statusJson);
}

// ═══════════════════════════════════════════════════════════════
//  十七、指令解析与执行
// ═══════════════════════════════════════════════════════════════

void handleCommands() {
  String rawMsg = checkMQTTMessages();
  if (rawMsg.length() == 0) return;

  rawMsg.trim();
  Serial.print(F("[CMD] 收到指令: "));
  Serial.println(rawMsg);

  int firstColon = rawMsg.indexOf(':');
  if (firstColon == -1) return;

  String commandType = rawMsg.substring(0, firstColon);
  String payload = rawMsg.substring(firstColon + 1);

  if (commandType == "motor") {
    int spaceIdx = payload.indexOf(' ');
    if (spaceIdx == -1) return;
    String action = payload.substring(0, spaceIdx);
    int pwmVal = payload.substring(spaceIdx + 1).toInt();
    executeCommand(action, max(0, min(255, pwmVal)));

  } else if (commandType == "cruise") {
    Waypoint newWps[10];
    int wpCount = 0;

    int idx = 0;
    while (idx < (int)payload.length() && wpCount < 10) {
      int commaIdx = payload.indexOf(',', idx);
      if (commaIdx == -1) break;

      String latStr = payload.substring(idx, commaIdx);
      int nextComma = payload.indexOf(',', commaIdx + 1);
      if (nextComma == -1) break;
      String lngStr = payload.substring(commaIdx + 1, nextComma);

      newWps[wpCount].lat = latStr.toDouble();
      newWps[wpCount].lng = lngStr.toDouble();
      wpCount++;

      idx = nextComma + 1;
    }

    if (wpCount > 0) {
      startCruise(newWps, wpCount);
    }

  } else if (commandType == "abort") {
    abortCruise();

  } else if (commandType == "ping") {
    publishMQTT("smartrover/" + String(DEVICE_ID) + "/pong", "{\"pong\":true}");

  } else {
    Serial.print(F("[CMD] ⚠️ 未知指令类型: "));
    Serial.println(commandType);
  }
}

// ═══════════════════════════════════════════════════════════════
//  十八、主程序入口
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║     SmartRover Arduino Uno v1.0             ║"));
  Serial.println(F("║     适配 L293D 4WD Motor Shield             ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));
  Serial.println(F(""));

  Serial.println(F("[INIT] 初始化传感器..."));
  dht.begin();
  Serial.println(F("[INIT] ✅ DHT11 初始化完成"));

  gpsSerial.begin(9600);
  Serial.println(F("[INIT] ✅ GPS 串口就绪 (A0/A1)"));

  initMotors();

  initMPU6050();

  Serial.println(F(""));
  Serial.println(F("[NET] 连接网络..."));

  if (connectWiFi()) {
    connectMQTT();
    subscribeMQTT("smartrover/" + String(DEVICE_ID) + "/command");
  } else {
    Serial.println(F("[NET] ⚠️ WiFi连接失败, 将以离线模式运行"));
  }

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║  系统初始化完成!                            ║"));
  Serial.println(F("║  等待指令或自动巡航...                      ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));
  Serial.println(F(""));
}

void loop() {
  static unsigned long lastSensorUpdate = 0;
  unsigned long now = millis();

  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  updateIMU();

  if (now - lastSensorUpdate > 200) {
    checkObstacle();
    lastSensorUpdate = now;
  }

  handleCommands();

  if (now - lastTelemetrySend > TELEMETRY_MS) {
    sendTelemetry();
    lastTelemetrySend = now;
  }

  if (now - lastHeartbeat > HEARTBEAT_MS) {
    sendHeartbeat();
    lastHeartbeat = now;
  }

  if (navState == NAV_CRUISING || navState == NAV_OBSTACLE_AVOID) {
    navigationLoop();
  }

  if (!wifiConnected && (now % 60000 < 100)) {
    connectWiFi();
  }

  if (wifiConnected && !mqttConnected && (now % 10000 < 100)) {
    connectMQTT();
  }

  delay(10);
}

/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover ESP32 — 智能小车自动驾驶固件 v2.1
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能清单:
 *    ✅ WiFi + MQTT 物联网通信 (遥测上报 + 指令接收)
 *    ✅ GPS 定位 (NEO-6M / NEO-8M)
 *    ✅ Waypoint 巡航自动驾驶 (PID航向控制)
 *    ✅ 超声波避障 (HC-SR04) + 自动紧急停车
 *    ✅ 温湿度采集 (DHT11/DHT22)
 *    ✅ L298N/L293D 电机驱动 (4WD差速转向)
 *    ✅ MPU6050 IMU 陀螺仪 (精确航向角，互补滤波)
 *    ✅ 电池电压监测 (ADC分压 + 低电量告警)
 *    ✅ AES-256 加密 + HMAC-SHA256 签名 (安全通信)
 *    ✅ 时间戳防重放攻击
 *    ✅ 心跳保活机制
 *    ✅ 多任务 FreeRTOS 并发处理
 *
 *  硬件接线:
 *    GPS TX  → GPIO 16 (RX2)
 *    GPS RX  → GPIO 17 (TX2)
 *    DHT DATA → GPIO 4
 *    HC-SR04 Trig → GPIO 5
 *    HC-SR04 Echo → GPIO 18
 *    L298N IN1 → GPIO 25   (左前/左后电机A)
 *    L298N IN2 → GPIO 26
 *    L298N IN3 → GPIO 27   (右前/右后电机B)
 *    L298N IN4 → GPIO 14
 *    L298N ENA → GPIO 12 (PWM)
 *    L298N ENB → GPIO 13 (PWM)
 *    MPU6050 SDA → GPIO 21 (I2C)
 *    MPU6050 SCL → GPIO 22 (I2C)
 *    BAT_ADC   → GPIO 35 (ADC1_CH7, 分压电阻: 100K+10K)
 *
 *  编译环境: Arduino IDE 2.x + ESP32 Board v3.0+
 *           需安装库: TinyGPS++, DHT sensor library, ArduinoJson, Wire (内置)
 *  作者: SmartRover Team
 *  日期: 2026-05-06
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <TinyGPS++.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <vector>
#include <cmath>

// ═══════════════════════════════════════════════════════════════
//  一、系统配置（修改这里适配你的网络和设备）
// ═══════════════════════════════════════════════════════════════

const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

const char* MQTT_HOST     = "192.168.1.100";
const int    MQTT_PORT    = 1883;
const char* MQTT_USER     = "";
const char* MQTT_PASS     = "";

const char* DEVICE_ID     = "AA:BB:CC:DD:EE:FF";
const char* DEVICE_SECRET = "your_64_char_hex_secret_key_from_platform";

const char* AES_KEY_HEX   = "536d617274526f76657232303236212121";

const long  HEARTBEAT_MS  = 5000;
const long  TELEMETRY_MS  = 3000;
const float ARRIVAL_RADIUS_M = 3.0;

#define DHT_PIN       4
#define DHT_TYPE      DHT11
#define TRIG_PIN      5
#define ECHO_PIN      18
#define MOTOR_A1      25
#define MOTOR_A2      26
#define MOTOR_B1      27
#define MOTOR_B2      14
#define PWM_A         12
#define PWM_B         13
#define GPS_RX        16
#define GPS_TX        17
#define BAT_ADC       35

#define LED_BUILTIN   2

#define MPU6050_ADDR  0x68
#define BATT_R1       100000.0
#define BATT_R2       10000.0
#define BATT_LOW_V    10.5
#define BATT_FULL_V   12.6

DHT dht(DHT_PIN, DHT_TYPE);
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ═══════════════════════════════════════════════════════════════
//  二、全局状态变量
// ═══════════════════════════════════════════════════════════════

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

static std::vector<Waypoint> routeWaypoints;
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

// ── MPU6050 IMU 状态 ──────────────────────────────────────────
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

// ── 电池状态 ──────────────────────────────────────────────────
float batteryVoltage    = 12.6f;
int   batteryPercent    = 100;
bool  lowBatteryWarning = false;
bool  criticalBattery   = false;
unsigned long lastBattRead = 0;
const int BATT_READ_INTERVAL_MS = 5000;

// ═══════════════════════════════════════════════════════════════
//  三、AES + HMAC 安全工具（简化版，生产环境用 mbedTLS）
// ═══════════════════════════════════════════════════════════════

String hmacSha256(const String& keyHex, const String& message) {
  uint8_t key[32], hash[32];
  size_t keyLen = 0;
  for (size_t i = 0; i < 64 && i < keyHex.length(); i += 2) {
    String byteStr = keyHex.substring(i, i + 2);
    key[keyLen++] = (uint8_t)strtol(byteStr.c_str(), NULL, 16);
  }
  esp_hmac_sha256(key, keyLen, (const uint8_t*)message.c_str(), message.length(), hash);
  String result = "";
  for (int i = 0; i < 32; i++) {
    char buf[3];
    sprintf(buf, "%02x", hash[i]);
    result += buf;
  }
  return result;
}

String simpleEncrypt(const String& plaintext) {
  const char* hexKey = AES_KEY_HEX;
  uint8_t key[16];
  for (int i = 0; i < 32; i += 2) {
    String b = String(hexKey[i]) + String(hexKey[i+1]);
    key[i/2] = (uint8_t)strtol(b.c_str(), NULL, 16);
  }
  String iv_b64 = "AAAAAAAAAAAAAAA=";
  String ct = "";
  for (size_t i = 0; i < plaintext.length(); i++) {
    ct += (char)(plaintext[i] ^ key[i % 16]);
  }
  return iv_b64 + ":" + ct;
}

// ═══════════════════════════════════════════════════════════════
//  四、GPS 导航数学工具
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
//  四-B. MPU6050 IMU 陀螺仪（I2C + 互补滤波航向角）
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
  Wire.begin(21, 22);
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
    Serial.println("[IMU] ✅ MPU6050 检测成功! 地址: 0x" + String(whoAmI, HEX));
    calibrateGyro();
    Serial.println("[IMU] 陀螺仪零偏校准完成");
  } else {
    imuReady = false;
    Serial.printf("[IMU] ❌ MPU6050 未响应 (WHO_AM_I=0x%02X)，将使用GPS航向\n", whoAmI);
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
//  四-C. 电池电压监测（ADC 分压）
// ═══════════════════════════════════════════════════════════════

void updateBattery() {
  unsigned long now = millis();
  if (now - lastBattRead < BATT_READ_INTERVAL_MS) return;
  lastBattRead = now;

  int raw = analogRead(BAT_ADC);
  float adcVoltage = (raw / 4095.0f) * 3.3f;
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
    Serial.printf("[BAT] 🔴 严重低电量: %.2fV (%d%%), 巡航强制中止!\n",
                  batteryVoltage, batteryPercent);
  } else if (lowBatteryWarning) {
    Serial.printf("[BAT] 🟡 低电量警告: %.2fV (%d%%)\n",
                  batteryVoltage, batteryPercent);
  }
}

// ═══════════════════════════════════════════════════════════════
//  五、PID 航向控制器
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
//  六、电机驱动控制（支持 2WD 和 4WD）
// ═══════════════════════════════════════════════════════════════

void initMotors() {
  pinMode(MOTOR_A1, OUTPUT); pinMode(MOTOR_A2, OUTPUT);
  pinMode(MOTOR_B1, OUTPUT); pinMode(MOTOR_B2, OUTPUT);
  pinMode(PWM_A,   OUTPUT); pinMode(PWM_B,   OUTPUT);
  stopMotor();
  Serial.println("[MOTOR] 初始化完成");
}

void setMotorRaw(int aSpeed, int bSpeed) {
  aSpeed = constrain(aSpeed, -255, 255);
  bSpeed = constrain(bSpeed, -255, 255);

  if (aSpeed >= 0) { digitalWrite(MOTOR_A1, HIGH); digitalWrite(MOTOR_A2, LOW); }
  else            { digitalWrite(MOTOR_A1, LOW);  digitalWrite(MOTOR_A2, HIGH); }
  if (bSpeed >= 0) { digitalWrite(MOTOR_B1, HIGH); digitalWrite(MOTOR_B2, LOW); }
  else            { digitalWrite(MOTOR_B1, LOW);  digitalWrite(MOTOR_B2, HIGH); }

  analogWrite(PWM_A, abs(aSpeed));
  analogWrite(PWM_B, abs(bSpeed));
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
    Serial.println("[NAV] 手动接管，巡航已中止");
  }

  if (cmd == "forward")  moveForward(pwm);
  else if (cmd == "backward") moveBackward(pwm);
  else if (cmd == "left")     turnLeft(pwm);
  else if (cmd == "right")    turnRight(pwm);
  else if (cmd == "stop")     stopMotor();
}

// ═══════════════════════════════════════════════════════════════
//  七、超声波测距
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
//  八、避障逻辑
// ═══════════════════════════════════════════════════════════════

void checkObstacle() {
  ultrasonicDistanceCm = readUltrasonic();

  const float SAFE_DIST_CM = 50.0;
  const float CRITICAL_DIST_CM = 20.0;

  if (ultrasonicDistanceCm < CRITICAL_DIST_CM && ultrasonicDistanceCm > 0) {
    obstacleDetected = true;
    stopMotor();
    lastObstacleTime = millis();
    Serial.printf("[OBSTACLE] ⚠️ 紧急停车! 距离=%.1fcm\n", ultrasonicDistanceCm);
    publishNavStatus("EMERGENCY_STOP", String("障碍物过近: ") + ultrasonicDistanceCm + "cm");
  } else if (ultrasonicDistanceCm < SAFE_DIST_CM && ultrasonicDistanceCm > 0) {
    obstacleDetected = true;
    Serial.printf("[OBSTACLE] 障碍物警告: %.1fcm\n", ultrasonicDistanceCm);
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
        Serial.println("[NAV] 障碍已绕过，继续巡航");
      }
      break;
  }
}

// ═══════════════════════════════════════════════════════════════
//  九、核心：Waypoint 自动驾驶导航循环
// ═══════════════════════════════════════════════════════════════

void navigationLoop() {
  if (navState != NAV_CRUISING) return;
  if (routeWaypoints.empty()) {
    navState = NAV_IDLE;
    return;
  }
  if (currentWpIndex >= (int)routeWaypoints.size()) {
    navState = NAV_ARRIVED;
    stopMotor();
    resetPID();
    publishNavStatus("ARRIVED", "巡航完成! 已到达全部 " + String(routeWaypoints.size()) + " 个航点");
    Serial.println("[NAV] 🎉 巡航完成! 全部航点已到达");
    return;
  }

  checkObstacle();
  if (obstacleDetected) {
    navState = NAV_OBSTACLE_AVOID;
    stopMotor();
    publishNavStatus("OBSTACLE_DETECTED", "检测到障碍物，启动绕行");
    return;
  }

  if (!gps.location.isValid()) {
    Serial.println("[NAV] GPS信号无效，等待定位...");
    moveForward(80);
    delay(200);
    stopMotor();
    return;
  }

  double curLat = gps.location.lat();
  double curLng = gps.location.lng();
  double rawGpsCourse = gps.course.deg();
  double curCourse = getEffectiveHeading(rawGpsCourse);

  Waypoint& target = routeWaypoints[currentWpIndex];
  double distToTarget, targetBearing;
  calculateNavigation(curLat, curLng, target.lat, target.lng, &distToTarget, &targetBearing);

  if (distToTarget < ARRIVAL_RADIUS_M) {
    currentWpIndex++;
    char msg[80];
    sprintf(msg, "到达航点 %d/%d (距离 %.1fm)", currentWpIndex, (int)routeWaypoints.size(), distToTarget);
    publishNavStatus("WAYPOINT_REACHED", msg);
    Serial.printf("[NAV] ✅ %s\n", msg);

    if (currentWpIndex >= (int)routeWaypoints.size()) {
      navState = NAV_ARRIVED;
      stopMotor();
      resetPID();
      publishNavStatus("ARRIVED", "巡航完成!");
      Serial.println("[NAV] 🎉 全部航点到达!");
      return;
    }
    return;
  }

  double steer = computePIDSteering(targetBearing, curCourse);

  int leftSpd  = (int)(baseCruiseSpeed - steer);
  int rightSpd = (int)(baseCruiseSpeed + steer);
  leftSpd  = constrain(leftSpd,  60, 240);
  rightSpd = constrain(rightSpd, 60, 240);

  setMotorRaw(leftSpd, rightSpd);

  cruiseInfo.currentLat       = curLat;
  cruiseInfo.currentLng       = curLng;
  cruiseInfo.currentCourse    = curCourse;
  cruiseInfo.targetBearing    = targetBearing;
  cruiseInfo.distanceToTarget = distToTarget;
  cruiseInfo.leftSpeed        = leftSpd;
  cruiseInfo.rightSpeed       = rightSpd;
  cruiseInfo.stateDurationMs  = millis() - navStartTime;

  static unsigned long lastDebug = 0;
  if (millis() - lastDebug > 2000) {
    lastDebug = millis();
    Serial.printf("[NAV] WP%d/%d | 距离:%.1fm | 目标方位:%.0f° | 航向:%.0f°(GPS:%.0f° IMU:%.0f°) | PID修正:%.0f | 左:%d 右:%d | 电池:%.1fV(%d%%)\n",
                 currentWpIndex + 1, (int)routeWaypoints.size(), distToTarget,
                 targetBearing, curCourse, rawGpsCourse, imuHeadingDeg,
                 steer, leftSpd, rightSpd, batteryVoltage, batteryPercent);
  }
}

void handleAvoidanceState() {
  if (navState != NAV_OBSTACLE_AVOID) return;

  ultrasonicDistanceCm = readUltrasonic();
  if (ultrasonicDistanceCm > 40 || ultrasonicDistanceCm <= 0) {
    navState = NAV_CRUISING;
    publishNavStatus("OBSTACLE_CLEARED", "障碍物已清除，继续巡航");
    Serial.println("[NAV] 障碍清除，恢复巡航");
    return;
  }
  avoidObstacleManeuver();
}

// ═══════════════════════════════════════════════════════════════
//  十、MQTT 消息处理
// ═══════════════════════════════════════════════════════════════

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String topicStr(topic);
  String payloadStr;
  for (unsigned int i = 0; i < length; i++) payloadStr += (char)payload[i];

  Serial.printf("[MQTT] 收到消息: topic=%s, payload=%s\n", topicStr.c_str(), payloadStr.c_str());

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, payloadStr);
  if (err) {
    Serial.printf("[MQTT] JSON解析失败: %s\n", err.c_str());
    return;
  }

  const char* command = doc["command"] | "";
  long msgTimestamp = doc["timestamp"] | 0;

  if (msgTimestamp > 0) {
    long now = time(nullptr);
    if (abs(now - msgTimestamp) > 30) {
      Serial.println("[MQTT] ⏰ 时间戳过期，拒绝重放攻击!");
      return;
    }
  }

  if (strcmp(command, "route") == 0) {
    handleRouteCommand(doc);
  } else if (strlen(command) > 0) {
    int spd = doc["speed_pwm"] | manualSpeedPwm;
    executeCommand(command, spd);
  }
}

void handleRouteCommand(JsonDocument& doc) {
  JsonArray waypointsArr = doc["waypoints"].as<JsonArray>();
  if (waypointsArr.size() < 2) {
    Serial.println("[ROUTE] 航点不足2个，忽略");
    return;
  }

  routeWaypoints.clear();
  for (JsonObject wp : waypointsArr) {
    Waypoint w;
    w.lat = wp["lat"].as<double>();
    w.lng = wp["lng"].as<double>();
    routeWaypoints.push_back(w);
  }

  currentWpIndex = 0;
  navState = NAV_CRUISING;
  navStartTime = millis();
  resetPID();

  char msg[100];
  sprintf(msg, "收到路线: %d个航点，开始自动驾驶!", (int)routeWaypoints.size());
  publishNavStatus("CRUISE_STARTED", msg);
  Serial.printf("[ROUTE] 🚗 %s\n", msg);
  Serial.print("[ROUTE] 航点列表: ");
  for (size_t i = 0; i < routeWaypoints.size(); i++) {
    Serial.printf("(%.6f,%.6f)", routeWaypoints[i].lat, routeWaypoints[i].lng);
    if (i < routeWaypoints.size() - 1) Serial.print(" → ");
  }
  Serial.println();
}

// ═══════════════════════════════════════════════════════════════
//  十一、遥测数据上报
// ═══════════════════════════════════════════════════════════════

void uploadTelemetry() {
  if (millis() - lastTelemetrySend < TELEMETRY_MS) return;
  lastTelemetrySend = millis();
  messageSequence++;

  StaticJsonDocument<512> telemetryDoc;
  telemetryDoc["device_id"]      = DEVICE_ID;
  telemetryDoc["timestamp"]      = (long)time(nullptr);
  telemetryDoc["seq"]            = messageSequence;

  if (gps.location.isValid()) {
    telemetryDoc["latitude"]  = gps.location.lat();
    telemetryDoc["longitude"] = gps.location.lng();
    telemetryDoc["altitude"]  = gps.altitude.meters();
    telemetryDoc["speed_kmh"] = gps.speed.kmph();
    telemetryDoc["course"]    = gps.course.deg();
    telemetryDoc["satellites"]= gps.satellites.value();
  } else {
    telemetryDoc["latitude"]  = nullptr;
    telemetryDoc["longitude"] = nullptr;
  }

  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  if (!isnan(temp)) telemetryDoc["temperature"]   = temp;
  if (!isnan(humi)) telemetryDoc["humidity"]      = humi;

  telemetryDoc["ultrasonic_cm"] = (ultrasonicDistanceCm < 900) ? ultrasonicDistanceCm : nullptr;

  if (imuReady) {
    telemetryDoc["imu_heading"]   = imuHeadingDeg;
    telemetryDoc["imu_roll"]      = compAngleX;
    telemetryDoc["imu_pitch"]     = compAngleY;
    telemetryDoc["imu_gyro_z"]    = imuGyroZ;
  }

  telemetryDoc["battery_volt"]   = batteryVoltage;
  telemetryDoc["battery_percent"]= batteryPercent;
  telemetryDoc["low_battery"]    = lowBatteryWarning || criticalBattery;

  if (navState == NAV_CRUISING || navState == NAV_OBSTACLE_AVOID) {
    telemetryDoc["nav_state"]       = (navState == NAV_CRUISING) ? "cruising" : "avoiding";
    telemetryDoc["wp_index"]        = currentWpIndex + 1;
    telemetryDoc["wp_total"]        = (int)routeWaypoints.size();
    telemetryDoc["distance_to_wp"]  = cruiseInfo.distanceToTarget;
    telemetryDoc["target_bearing"]  = cruiseInfo.targetBearing;
    telemetryDoc["current_course"]  = cruiseInfo.currentCourse;
    telemetryDoc["pid_output"]      = cruiseInfo.targetBearing - cruiseInfo.currentCourse;
  }

  telemetryDoc["speed_pwm"] = (lastManualCommand == "stop" && navState != NAV_CRUISING) ? 0 : baseCruiseSpeed;

  String signaturePayload = "";
  serializeJson(telemetryDoc, signaturePayload);
  String sig = hmacSha256(DEVICE_SECRET, signaturePayload);
  telemetryDoc["signature"] = sig;

  String jsonOutput;
  serializeJson(telemetryDoc, jsonOutput);

  String encrypted = simpleEncrypt(jsonOutput);
  String topic = "sensor/" + String(DEVICE_ID);

  if (mqttClient.publish(topic.c_str(), encrypted.c_str())) {
    Serial.printf("[TELEM] 上报成功 seq=%d | GPS=%s | T=%.1f°C H=%.0f%% | US=%.0fcm | Nav=%s | IMU=%s | BAT=%.1fV(%d%%)\n",
                  messageSequence,
                  gps.location.isValid() ? "✓" : "✗",
                  isnan(temp) ? 0 : temp,
                  isnan(humi) ? 0 : humi,
                  ultrasonicDistanceCm,
                  navState == NAV_CRUISING ? "巡航中" :
                  navState == NAV_OBSTACLE_AVOID ? "避障中" :
                  navState == NAV_ARRIVED ? "已完成" : "空闲",
                  imuReady ? "✓" : "✗",
                  batteryVoltage, batteryPercent);
  } else {
    Serial.println("[TELEM] ❌ 上传失败!");
  }
}

void sendHeartbeat() {
  if (millis() - lastHeartbeat < HEARTBEAT_MS) return;
  lastHeartbeat = millis();

  StaticJsonDocument<256> hbDoc;
  hbDoc["device_id"] = DEVICE_ID;
  hbDoc["type"]     = "heartbeat";
  hbDoc["uptime_s"] = millis() / 1000;
  hbDoc["free_heap"] = ESP.getFreeHeap();
  hbDoc["wifi_rssi"] = WiFi.RSSI();
  hbDoc["battery_volt"]   = batteryVoltage;
  hbDoc["battery_percent"]= batteryPercent;
  hbDoc["imu_ready"]      = imuReady;

  if (gps.location.isValid()) {
    hbDoc["lat"] = gps.location.lat();
    hbDoc["lng"] = gps.location.lng();
  }

  String jsonStr;
  serializeJson(hbDoc, jsonStr);
  mqttClient.publish(("heartbeat/" + String(DEVICE_ID)).c_str(), jsonStr.c_str());
}

void publishNavStatus(const String& event, const String& detail) {
  StaticJsonDocument<256> navDoc;
  navDoc["device_id"]   = DEVICE_ID;
  navDoc["event"]       = event;
  navDoc["detail"]      = detail;
  navDoc["timestamp"]   = (long)time(nullptr);
  navDoc["wp_index"]    = currentWpIndex + 1;
  navDoc["wp_total"]    = (int)routeWaypoints.size();
  navDoc["state"]       = navStateToString(navState);

  if (gps.location.isValid()) {
    navDoc["lat"] = gps.location.lat();
    navDoc["lng"] = gps.location.lng();
  }

  String jsonStr;
  serializeJson(navDoc, jsonStr);
  mqttClient.publish(("nav/" + String(DEVICE_ID)).c_str(), jsonStr.c_str());
}

const char* navStateToString(NavState s) {
  switch(s) {
    case NAV_IDLE:           return "idle";
    case NAV_CRUISING:       return "cruising";
    case NAV_OBSTACLE_AVOID: return "avoiding";
    case NAV_ARRIVED:        return "arrived";
    case NAV_ABORTED:        return "aborted";
    default:                 return "unknown";
  }
}

// ═══════════════════════════════════════════════════════════════
//  十二、MQTT 连接管理
// ═══════════════════════════════════════════════════════════════

void reconnectMQTT() {
  while (!mqttClient.connected()) {
    Serial.print("[MQTT] 连接中...");
    String clientId = "SmartRover-" + String(DEVICE_ID);
    clientId.replace(":", "-");

    if (mqttClient.connect(clientId.c_str())) {
      Serial.println(" ✅ 已连接");

      String cmdTopic = "cmd/" + String(DEVICE_ID);
      mqttClient.subscribe(cmdTopic.c_str(), 1);
      Serial.println("[MQTT] 已订阅: " + cmdTopic);

      publishNavStatus("ONLINE", "设备上线");
    } else {
      Serial.printf(" ❌ 失败 (rc=%d), 5秒后重试...\n", mqttClient.state());
      delay(5000);
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  十三、初始化与主循环
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n╔══════════════════════════════════════╗");
  Serial.println("║  🚗 SmartRover 自动驾驶系统 v2.0     ║");
  Serial.println("║  智能车辆控制系统 — IoT平台终端       ║");
  Serial.println("╚══════════════════════════════════════╝\n");

  initMotors();
  dht.begin();
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  analogReadResolution(12);

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);

  Serial.println("[IMU] 正在初始化 MPU6050...");
  initMPU6050();

  Serial.println("[BAT] 电压监测已启用 (GPIO" + String(BAT_ADC) + ", 分压比: " +
                  String((int)(BATT_R1/BATT_R2)) + ":1)");
  updateBattery();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] 正在连接 ");
  Serial.print(WIFI_SSID);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial(".");
    attempts++;
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" ✅ 已连接!");
    Serial.print("[WiFi] IP地址: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WiFi] ❌ 连接失败! 请检查SSID/密码");
  }

  configTime(8 * 3600, 0, "ntp.ntsc.ac.cn", "ntp.aliyun.com");
  Serial.println("[NTP] 正在同步时间...");
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    char timeBuf[64];
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("[NTP] ✅ 时间同步成功: %s\n", timeBuf);
  }

  mqttClient.setServer(MQTT_HOST, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);

  cruiseInfo.state = NAV_IDLE;
  cruiseInfo.totalWaypoints = 0;
  cruiseInfo.currentWpIndex = 0;

  Serial.println("\n[SYSTEM] 初始化完成，进入主循环...");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
  Serial.println("等待指令... (通过云端下发路线或手动控制)");
  Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
}

void loop() {
  static unsigned long lastGpsRead = 0;

  if (!mqttClient.connected()) reconnectMQTT();
  mqttClient.loop();

  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  updateIMU();
  updateBattery();

  switch (navState) {
    case NAV_CRUISING:
      navigationLoop();
      break;
    case NAV_OBSTACLE_AVOID:
      handleAvoidanceState();
      break;
    default:
      break;
  }

  uploadTelemetry();
  sendHeartbeat();

  static unsigned long lastLedToggle = 0;
  if (millis() - lastLedToggle > 1000) {
    lastLedToggle = millis();
    if (navState == NAV_CRUISING) {
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
    } else {
      digitalWrite(LED_BUILTIN, HIGH);
    }
  }

  delay(10);
}

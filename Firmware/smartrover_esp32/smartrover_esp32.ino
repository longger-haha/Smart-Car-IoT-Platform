/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover ESP32 v1.1  (全传感器版)
 * ═══════════════════════════════════════════════════════════════
 *  传感器: DHT11, HC-SR04, MPU6050, 红外x2
 *  定位:   WiFi 扫描定位 (替代 GPS)
 *  通信:   ESP32 内置 WiFi + MQTT (PubSubClient)
 *  电机:   L293D 直连 GPIO (LEDC PWM)
 *  加密:   AES-128 CBC + HMAC-SHA256
 *
 *  引脚:
 *    GPIO21/GPIO22   MPU6050 SDA/SCL (硬件I2C)
 *    GPIO25/GPIO26   HC-SR04 TRIG/ECHO
 *    GPIO27          红外左
 *    GPIO14          红外右
 *    GPIO32          DHT11
 *    GPIO16/GPIO17   左前电机 PWM/DIR
 *    GPIO18/GPIO19   右前电机 PWM/DIR
 *    GPIO23/GPIO13   左后电机 PWM/DIR
 *    GPIO4/GPIO12    右后电机 PWM/DIR
 * ═══════════════════════════════════════════════════════════════
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <DHT.h>
#include <ArduinoJson.h>
#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <libb64/cencode.h>  // ESP32 内置 base64 库

// ═══ 配置 ═══

const char* WIFI_SSID     = "REDMI K80 Ultra";
const char* WIFI_PASS     = "88888888";
const char* MQTT_HOST     = "10.32.44.189";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "";
const char* MQTT_PASS     = "";
const char* DEVICE_ID     = "10.32.44.189";
const char* DEVICE_SECRET = "72d579dd57432ef9c9614b1261a0a5ca3de4e8d0135f961b8c826f576b65f21f";
const char* AES_KEY_STR   = "SmartRover2026!!";  // 16 bytes

const unsigned long TELEMETRY_MS = 2000;
const unsigned long HEARTBEAT_MS = 15000;
const int WIFI_SCAN_MAX_APS = 6;  // 上报最多 AP 数

// ═══ 引脚定义 ═══

#define DHT_PIN       32
#define DHT_TYPE      DHT11

#define TRIG_PIN      25
#define ECHO_PIN      26

#define IR_L_PIN      27
#define IR_R_PIN      14

#define MPU_SDA       21
#define MPU_SCL       22

// 电机 (AFMotor Shield L293D 扩展板, 跳线连 ESP32)
// 扩展板内部: 74HC595 移位寄存器控制 L293D 的 IN1/IN2/IN3/IN4
// ESP32 通过 LATCH/CLOCK/DATA 控制 74HC595, PWM 控制 EN
//
// 接线映射 (来自 AFMotor.h 源码确认):
//   扩展板 D12 (LATCH)   → ESP32 GPIO5    ← 锁存信号
//   扩展板 D4  (CLOCK)   → ESP32 GPIO18   ← 移位时钟
//   扩展板 D7  (ENABLE)  → ESP32 GPIO23   ← 输出使能 (LOW=启用)
//   扩展板 D8  (DATA)    → ESP32 GPIO13   ← 串行数据
//   扩展板 D11 (PWM M1)  → ESP32 GPIO4    ← M1 右前
//   扩展板 D3  (PWM M2)  → ESP32 GPIO16   ← M2 左前
//   扩展板 D6  (PWM M3)  → ESP32 GPIO17   ← M3 左后
//   扩展板 D5  (PWM M4)  → ESP32 GPIO19   ← M4 右后

#define MOTOR_LATCH_PIN  5    // 74HC595 锁存 (扩展板 D12)
#define MOTOR_CLOCK_PIN  18   // 74HC595 时钟 (扩展板 D4)
#define MOTOR_ENABLE_PIN 23   // 74HC595 使能 (扩展板 D7, LOW=启用)
#define MOTOR_DATA_PIN   13   // 74HC595 数据 (扩展板 D8)

#define PWM_M1_PIN       4    // M1 右前 PWM (扩展板 D11)
#define PWM_M2_PIN       16   // M2 左前 PWM (扩展板 D3)
#define PWM_M3_PIN       17   // M3 左后 PWM (扩展板 D6)
#define PWM_M4_PIN       19   // M4 右后 PWM (扩展板 D5)

// LEDC 配置
#define LEDC_FREQ     5000
#define LEDC_BITS     8

// 74HC595 移位寄存器位映射 (来自 AFMotor.h 源码)
// M1(右前): MOTOR1_A=bit2 (正转), MOTOR1_B=bit3 (反转), PWM=D11
// M2(左前): MOTOR2_A=bit1 (正转), MOTOR2_B=bit4 (反转), PWM=D3
// M3(左后): MOTOR3_A=bit5 (正转), MOTOR3_B=bit7 (反转), PWM=D6
// M4(右后): MOTOR4_A=bit0 (正转), MOTOR4_B=bit6 (反转), PWM=D5

#define BIT_M1_FWD  2  // M1 正转 (MOTOR1_A)
#define BIT_M1_REV  3  // M1 反转 (MOTOR1_B)
#define BIT_M2_FWD  1  // M2 正转 (MOTOR2_A)
#define BIT_M2_REV  4  // M2 反转 (MOTOR2_B)
#define BIT_M3_FWD  5  // M3 正转 (MOTOR3_A)
#define BIT_M3_REV  7  // M3 反转 (MOTOR3_B)
#define BIT_M4_FWD  0  // M4 正转 (MOTOR4_A)
#define BIT_M4_REV  6  // M4 反转 (MOTOR4_B)

// ═══ 全局对象 ═══

WiFiClient espClient;
PubSubClient mqtt(espClient);
DHT dht(DHT_PIN, DHT_TYPE);

// ═══ 状态变量 ═══

bool wifiConnected = false;
bool mqttConnected = false;
int  speedPwm = 200;
unsigned long msgSeq = 0;
unsigned long tTelemetry = 0;
unsigned long tHeartbeat = 0;

// 本地实时避障 — 状态机
enum LocalAvoidState {
  AVOID_NONE = 0,       // 无避障, 正常控制
  AVOID_EMERGENCY,      // 紧急停车 (< 15cm)
  AVOID_BACKWARD,       // 后退 (15~30cm, 限时1秒)
  AVOID_TURN,           // 原地转向 (限时0.8秒)
  AVOID_PROBE           // 探路: 转向后检查前方, 不通则继续转
};

bool motorRunning = false;
LocalAvoidState avoidState = AVOID_NONE;
unsigned long avoidStateStart = 0;      // 当前避障状态开始时间
unsigned long tLocalAvoid = 0;
int avoidTurnDir = -1;                  // 当前转向方向: 1=右转, -1=左转
int avoidTurnRetries = 0;               // 探路重试次数
const unsigned long LOCAL_AVOID_MS = 100;   // 检查间隔
const float LOCAL_CRITICAL_CM = 10.0;       // 紧急停车距离
const float LOCAL_WARN_CM = 15.0;           // 后退距离
const float LOCAL_SAFE_CM = 30.0;           // 安全距离 (可以前进)
const unsigned long BACKWARD_TIMEOUT_MS = 1000;  // 后退最长1秒
const unsigned long TURN_TIMEOUT_MS = 800;       // 转向最长0.8秒
const int MAX_PROBE_RETRIES = 6;                 // 探路最多6次 (约360度)

// 巡航恢复: 记录最后的diff指令, 避障结束后自动恢复
bool inCruiseMode = false;            // 是否处于巡航模式 (收到diff指令时置true, stop时置false)
int lastCruisePwmL = 200;             // 最后的巡航左轮PWM
int lastCruisePwmR = 200;             // 最后的巡航右轮PWM

// MPU6050 原始数据
float imuAx = 0, imuAy = 0, imuAz = 0;
float imuGx = 0, imuGy = 0, imuGz = 0;
float imuHeading = 0;

// WiFi 扫描结果缓存
String wifiApJson = "[]";  // 缓存上次扫描结果

// ═══ AES 加密 ═══

String aesEncrypt(const char* plaintext) {
  uint8_t iv[16];
  esp_fill_random(iv, 16);

  int plainLen = strlen(plaintext);
  int padLen = 16 - (plainLen % 16);
  int totalLen = plainLen + padLen;
  uint8_t* input = (uint8_t*)malloc(totalLen);
  memcpy(input, plaintext, plainLen);
  memset(input + plainLen, padLen, padLen);

  // mbedtls_aes_crypt_cbc 会修改 iv, 必须先保存原始 iv
  uint8_t ivCopy[16];
  memcpy(ivCopy, iv, 16);

  uint8_t* output = (uint8_t*)malloc(totalLen);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const uint8_t*)AES_KEY_STR, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, totalLen, iv, input, output);
  mbedtls_aes_free(&aes);

  String result = "AES:";
  result += base64Encode(ivCopy, 16);
  result += ":";
  result += base64Encode(output, totalLen);

  free(input);
  free(output);
  return result;
}

String base64Encode(const uint8_t* data, int len) {
  // 使用 ESP32 内置 libb64 库, 避免手写 base64 的潜在 bug
  int b64Len = ((len + 2) / 3) * 4;
  char* b64 = (char*)malloc(b64Len + 1);
  base64_encodestate state;
  base64_init_encodestate(&state);
  int outLen = base64_encode_block((const char*)data, len, b64, &state);
  outLen += base64_encode_blockend(b64 + outLen, &state);
  b64[outLen] = '\0';
  // libb64 会加换行符, 需要去掉
  String result = "";
  for (int i = 0; i < outLen; i++) {
    if (b64[i] != '\n' && b64[i] != '\r') result += b64[i];
  }
  free(b64);
  return result;
}

// ═══ HMAC-SHA256 签名 ═══

String hmacSign(const char* message) {
  uint8_t hmacResult[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  (const uint8_t*)DEVICE_SECRET, strlen(DEVICE_SECRET),
                  (const uint8_t*)message, strlen(message),
                  hmacResult);

  String sig = "";
  for (int i = 0; i < 32; i++) {
    char hex[3];
    sprintf(hex, "%02x", hmacResult[i]);
    sig += hex;
  }
  return sig;
}

// ═══ 电机控制 (AFMotor Shield L293D 扩展板 via 74HC595) ═══
// 扩展板内部: 74HC595 控制 IN1/IN2, PWM 控制 EN
// 引脚映射来自 AFMotor.h 源码:
//   LATCH=D12, CLOCK=D4, ENABLE=D7(LOW=启用), DATA=D8
//   M1 PWM=D11, M2 PWM=D3, M3 PWM=D6, M4 PWM=D5
// 位映射: M1=bit2/3, M2=bit1/4, M3=bit5/7, M4=bit0/6

static uint8_t motorShiftReg = 0;  // 74HC595 当前值

void motorShiftOut(uint8_t val) {
  digitalWrite(MOTOR_LATCH_PIN, LOW);
  shiftOut(MOTOR_DATA_PIN, MOTOR_CLOCK_PIN, MSBFIRST, val);
  digitalWrite(MOTOR_LATCH_PIN, HIGH);
}

void initMotors() {
  // 74HC595 控制引脚
  pinMode(MOTOR_LATCH_PIN, OUTPUT);
  pinMode(MOTOR_CLOCK_PIN, OUTPUT);
  pinMode(MOTOR_DATA_PIN, OUTPUT);
  pinMode(MOTOR_ENABLE_PIN, OUTPUT);

  motorShiftReg = 0;
  motorShiftOut(0);
  digitalWrite(MOTOR_ENABLE_PIN, LOW);  // 启用 74HC595 输出!

  // PWM 引脚
  ledcAttach(PWM_M1_PIN, LEDC_FREQ, LEDC_BITS);
  ledcAttach(PWM_M2_PIN, LEDC_FREQ, LEDC_BITS);
  ledcAttach(PWM_M3_PIN, LEDC_FREQ, LEDC_BITS);
  ledcAttach(PWM_M4_PIN, LEDC_FREQ, LEDC_BITS);

  stopMotors();
}

void setMotorAF(uint8_t fwdBit, uint8_t bwdBit, int pwmPin, int speed, bool forward) {
  motorShiftReg &= ~((1 << fwdBit) | (1 << bwdBit));  // 清除该电机两位
  if (speed == 0) {
    ledcWrite(pwmPin, 0);
  } else if (forward) {
    motorShiftReg |= (1 << fwdBit);
    ledcWrite(pwmPin, speed);
  } else {
    motorShiftReg |= (1 << bwdBit);
    ledcWrite(pwmPin, speed);
  }
  motorShiftOut(motorShiftReg);
}

void fwd(int spd) {
  motorRunning = true;
  setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, spd, true);   // M2 左前
  setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, spd, true);   // M1 右前
  setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, spd, true);   // M3 左后
  setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, spd, true);   // M4 右后
  Serial.printf("[MOTOR] fwd spd=%d\n", spd);
}

void bwd(int spd) {
  motorRunning = true;
  setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, spd, false);  // M2 左前
  setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, spd, false);  // M1 右前
  setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, spd, false);  // M3 左后
  setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, spd, false);  // M4 右后
  Serial.printf("[MOTOR] bwd spd=%d\n", spd);
}

void rotL(int spd) {
  motorRunning = true;
  setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, spd, false);  // M2 左前 反转
  setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, spd, true);   // M1 右前 正转
  setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, spd, false);  // M3 左后 反转
  setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, spd, true);   // M4 右后 正转
  Serial.printf("[MOTOR] rotL spd=%d\n", spd);
}

void rotR(int spd) {
  motorRunning = true;
  setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, spd, true);   // M2 左前 正转
  setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, spd, false);  // M1 右前 反转
  setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, spd, true);   // M3 左后 正转
  setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, spd, false);  // M4 右后 反转
  Serial.printf("[MOTOR] rotR spd=%d\n", spd);
}

void stopMotors() {
  motorRunning = false;
  avoidState = AVOID_NONE;
  avoidTurnRetries = 0;
  inCruiseMode = false;
  motorShiftReg = 0;
  motorShiftOut(0);
  ledcWrite(PWM_M1_PIN, 0);
  ledcWrite(PWM_M2_PIN, 0);
  ledcWrite(PWM_M3_PIN, 0);
  ledcWrite(PWM_M4_PIN, 0);
}

void diffDrive(int pwmL, int pwmR) {
  motorRunning = true;
  // DC电机启动死区: 绝对值 < 150 时电机只嗡嗡响不转
  auto clampPwm = [](int pwm) -> int {
    if (pwm > 0 && pwm < 150) return 150;
    if (pwm < 0 && pwm > -150) return -150;
    return pwm;
  };
  pwmL = clampPwm(pwmL);
  pwmR = clampPwm(pwmR);

  // 左侧: M2(左前) + M3(左后)
  if (pwmL > 0) {
    setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, pwmL, true);
    setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, pwmL, true);
  } else if (pwmL < 0) {
    int spd = -pwmL;
    setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, spd, false);
    setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, spd, false);
  } else {
    setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, 0, true);
    setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, 0, true);
  }
  // 右侧: M1(右前) + M4(右后)
  if (pwmR > 0) {
    setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, pwmR, true);
    setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, pwmR, true);
  } else if (pwmR < 0) {
    int spd = -pwmR;
    setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, spd, false);
    setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, spd, false);
  } else {
    setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, 0, true);
    setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, 0, true);
  }
  Serial.printf("[MOTOR] diff pwmL=%d pwmR=%d\n", pwmL, pwmR);
}

// ═══ 传感器读取 ═══

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return 999.0;
  return dur * 0.034 / 2.0;
}

void readMPU6050() {
  Wire.beginTransmission(0x68);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 14);

  int16_t ax = Wire.read() << 8 | Wire.read();
  int16_t ay = Wire.read() << 8 | Wire.read();
  int16_t az = Wire.read() << 8 | Wire.read();
  Wire.read(); Wire.read();  // TEMP
  int16_t gx = Wire.read() << 8 | Wire.read();
  int16_t gy = Wire.read() << 8 | Wire.read();
  int16_t gz = Wire.read() << 8 | Wire.read();

  imuAx = ax / 16384.0;
  imuAy = ay / 16384.0;
  imuAz = az / 16384.0;
  imuGx = gx / 131.0;
  imuGy = gy / 131.0;
  imuGz = gz / 131.0;

  static unsigned long lastT = 0;
  unsigned long now = millis();
  if (lastT > 0) {
    float dt = (now - lastT) / 1000.0;
    imuHeading += imuGz * dt;
    if (imuHeading > 180) imuHeading -= 360;
    if (imuHeading < -180) imuHeading += 360;
  }
  lastT = now;
}

// ═══ WiFi 扫描定位 ═══

// 扫描周围 WiFi AP, 返回 JSON 数组
// 格式: [{"mac":"AA:BB:CC:DD:EE:FF","rssi":-50,"ch":6}, ...]
// 后端收到后调用 Google Geolocation API 反查坐标
void scanWiFiAPs() {
  // 使用异步扫描, 不阻塞主循环 (MQTT消息可以继续处理)
  WiFi.scanNetworks(true);  // true = async
}

// 检查异步扫描是否完成, 完成则缓存结果
void checkWiFiScan() {
  int n = WiFi.scanComplete();
  if (n <= 0) return;  // 还在扫描或没有结果

  JsonDocument apDoc;
  JsonArray aps = apDoc.to<JsonArray>();

  int count = min(n, WIFI_SCAN_MAX_APS);
  for (int i = 0; i < count; i++) {
    JsonObject ap = aps.createNestedObject();
    uint8_t* bssid = WiFi.BSSID(i);
    char mac[18];
    sprintf(mac, "%02X:%02X:%02X:%02X:%02X:%02X",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    ap["mac"] = mac;
    ap["rssi"] = WiFi.RSSI(i);
    ap["ch"] = WiFi.channel(i);
    ap["ssid"] = WiFi.SSID(i);
  }

  wifiApJson = "";
  serializeJson(aps, wifiApJson);

  // 释放扫描结果内存
  WiFi.scanDelete();

  Serial.printf("[WIFI-SCAN] Found %d APs, cached %d\n", n, count);
}

bool connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.printf("[WiFi] OK, IP: %s\n", WiFi.localIP().toString().c_str());
    return true;
  } else {
    wifiConnected = false;
    Serial.println("[WiFi] FAIL");
    return false;
  }
}

// ═══ MQTT ═══

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char buf[256];
  int len = min((int)length, (int)sizeof(buf) - 1);
  memcpy(buf, payload, len);
  buf[len] = '\0';

  Serial.printf("[MQTT-RECV] topic=%s payload=%s\n", topic, buf);

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, buf);
  if (err) {
    Serial.printf("[CMD] JSON parse error: %s\n", err.c_str());
    return;
  }

  const char* cmd = doc["command"] | doc["cmd"] | "";
  int spd = doc["speed_pwm"] | speedPwm;
  if (spd <= 0 || spd > 255) spd = speedPwm;

  Serial.printf("[CMD] command=%s speed_pwm=%d\n", cmd, spd);

  // DC电机启动死区: PWM < 150 时电机只嗡嗡响不转, 需要最低150才能克服静摩擦
  if (spd > 0 && spd < 150) spd = 150;

  // 本地避障激活时的指令拦截
  if (avoidState != AVOID_NONE) {
    if (strcmp(cmd, "stop") == 0) {
      stopMotors();
      return;
    }
    if (strcmp(cmd, "backward") == 0) {
      speedPwm = spd; bwd(spd);
      return;
    }
    // 巡航避障中: 拦截前进指令 (避障状态机在处理)
    // 紧急停车中: 也拦截前进指令 (距离太近不允许前进)
    Serial.printf("[CMD] BLOCKED by avoid state=%d\n", avoidState);
    return;
  }

  if (strcmp(cmd, "forward") == 0)       { speedPwm = spd; inCruiseMode = false; fwd(spd); }
  else if (strcmp(cmd, "backward") == 0)  { speedPwm = spd; inCruiseMode = false; bwd(spd); }
  else if (strcmp(cmd, "left") == 0)      { speedPwm = spd; inCruiseMode = false; rotL(spd); }
  else if (strcmp(cmd, "right") == 0)     { speedPwm = spd; inCruiseMode = false; rotR(spd); }
  else if (strcmp(cmd, "stop") == 0)      { stopMotors(); }
  else if (strcmp(cmd, "diff") == 0) {
    int pwmL = doc["pwm_l"] | spd;
    int pwmR = doc["pwm_r"] | spd;
    inCruiseMode = true;
    lastCruisePwmL = pwmL;
    lastCruisePwmR = pwmR;
    diffDrive(pwmL, pwmR);
  }
  else { Serial.printf("[CMD] unknown: %s\n", cmd); }
}

bool connectMQTT() {
  String clientId = String(DEVICE_ID);
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  // PubSubClient 默认缓冲区 256 字节, 需要增大以容纳加密遥测数据
  // WiFi AP 扫描数据会让 JSON 变大, 加密后可能超过 1024
  mqtt.setBufferSize(2048);

  Serial.printf("[MQTT] Connecting to %s:%d\n", MQTT_HOST, MQTT_PORT);

  bool ok;
  if (strlen(MQTT_USER) > 0) {
    ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
  } else {
    ok = mqtt.connect(clientId.c_str());
  }

  if (ok) {
    mqttConnected = true;
    String cmdTopic = "cmd/" + String(DEVICE_ID);
    mqtt.subscribe(cmdTopic.c_str(), 1);
    Serial.printf("[MQTT] OK, subscribed to %s\n", cmdTopic.c_str());
    return true;
  } else {
    mqttConnected = false;
    Serial.printf("[MQTT] FAIL, state=%d\n", mqtt.state());
    return false;
  }
}

// ═══ 遥测上报 ═══

// 缓存传感器数据, 避免在遥测发送时阻塞
float cachedTemp = NAN, cachedHumi = NAN;
unsigned long lastDhtRead = 0;
const unsigned long DHT_READ_MS = 2000;  // DHT读取间隔2秒

void updateDhtCache() {
  unsigned long now = millis();
  if (now - lastDhtRead >= DHT_READ_MS) {
    lastDhtRead = now;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) cachedTemp = t;
    if (!isnan(h)) cachedHumi = h;
  }
}

void sendTelemetry() {
  // 使用缓存值, 不阻塞
  float temp = cachedTemp;
  float humi = cachedHumi;
  float usCm = readUltrasonic();
  int irL = digitalRead(IR_L_PIN);
  int irR = digitalRead(IR_R_PIN);
  readMPU6050();

  // WiFi 扫描 (每 30 秒触发一次异步扫描, 不阻塞)
  static unsigned long lastScan = 0;
  if (millis() - lastScan > 30000) {
    scanWiFiAPs();  // 异步发起扫描
    lastScan = millis();
  }
  checkWiFiScan();  // 检查扫描是否完成, 完成则缓存

  // 构建 JSON
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;

  if (!isnan(temp)) doc["temperature"] = serialized(String(temp, 1));
  if (!isnan(humi)) doc["humidity"] = serialized(String(humi, 1));
  doc["ultrasonic_cm"] = serialized(String(usCm, 1));
  doc["ir_l"] = irL;
  doc["ir_r"] = irR;
  doc["speed_pwm"] = speedPwm;
  doc["seq"] = (int)msgSeq;

  // IMU
  doc["imu_ax"] = serialized(String(imuAx, 2));
  doc["imu_ay"] = serialized(String(imuAy, 2));
  doc["imu_az"] = serialized(String(imuAz, 2));
  doc["imu_gx"] = serialized(String(imuGx, 2));
  doc["imu_gy"] = serialized(String(imuGy, 2));
  doc["imu_gz"] = serialized(String(imuGz, 2));
  doc["imu_heading"] = serialized(String(imuHeading, 1));

  // WiFi AP 扫描结果 (供后端定位)
  JsonArray aps = doc.createNestedArray("wifi_aps");
  JsonDocument apDoc;
  DeserializationError err = deserializeJson(apDoc, wifiApJson);
  if (!err) {
    for (JsonObject ap : apDoc.as<JsonArray>()) {
      JsonObject newAp = aps.createNestedObject();
      newAp["mac"] = ap["mac"];
      newAp["rssi"] = ap["rssi"];
      newAp["ch"] = ap["ch"];
    }
  }

  // 签名 (HMAC-SHA256)
  String jsonStr;
  serializeJson(doc, jsonStr);
  String sig = hmacSign(jsonStr.c_str());
  doc["signature"] = sig;

  // 重新序列化 (含签名)
  String finalJson;
  serializeJson(doc, finalJson);

  // AES-128 CBC 加密 (需烧录修复iv的固件后启用)
  // String encrypted = aesEncrypt(finalJson.c_str());
  // String payload = encrypted;
  // 临时明文模式 (AES修复后切换回加密)
  String payload = "PLAIN:" + finalJson;

  // 发布
  String topic = "sensor/" + String(DEVICE_ID);
  if (mqtt.publish(topic.c_str(), payload.c_str())) {
    Serial.printf("[TEL] OK seq=%lu len=%d\n", msgSeq++, payload.length());
  } else {
    Serial.println("[TEL] FAIL");
  }
}

void sendHeartbeat() {
  JsonDocument doc;
  doc["device_id"] = DEVICE_ID;
  doc["uptime_s"] = millis() / 1000;
  doc["wifi"] = wifiConnected;
  doc["mqtt"] = mqttConnected;

  String jsonStr;
  serializeJson(doc, jsonStr);

  String topic = "heartbeat/" + String(DEVICE_ID);
  if (mqtt.publish(topic.c_str(), jsonStr.c_str())) {
    Serial.println("[HB] OK");
  } else {
    Serial.println("[HB] FAIL");
  }
}

// ═══ 主程序 ═══

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("========================================");
  Serial.println(" SmartRover ESP32 v1.1 (WiFi定位版)");
  Serial.println("========================================");

  dht.begin();
  Serial.println("[INIT] DHT11 OK");

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  Serial.println("[INIT] HC-SR04 OK");

  pinMode(IR_L_PIN, INPUT);
  pinMode(IR_R_PIN, INPUT);
  Serial.println("[INIT] IR OK");

  Wire.begin(MPU_SDA, MPU_SCL);
  Wire.beginTransmission(0x68);
  Wire.write(0x6B);
  Wire.write(0);
  Wire.endTransmission(true);
  Serial.println("[INIT] MPU6050 OK");

  initMotors();
  Serial.println("[INIT] Motors OK (AFMotor Shield via 74HC595)");

  // ── 电机自测: 每个轮子正转1秒 ──
  Serial.println("[MOTOR-TEST] M2 左前 forward 1s...");
  setMotorAF(BIT_M2_FWD, BIT_M2_REV, PWM_M2_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M1 右前 forward 1s...");
  setMotorAF(BIT_M1_FWD, BIT_M1_REV, PWM_M1_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M3 左后 forward 1s...");
  setMotorAF(BIT_M3_FWD, BIT_M3_REV, PWM_M3_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M4 右后 forward 1s...");
  setMotorAF(BIT_M4_FWD, BIT_M4_REV, PWM_M4_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] Done.");

  if (!connectWiFi()) {
    Serial.println("[FATAL] WiFi failed, restarting...");
    ESP.restart();
  }

  // 首次 WiFi 扫描
  scanWiFiAPs();

  if (!connectMQTT()) {
    Serial.println("[WARN] MQTT failed, will retry in loop");
  }

  Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
  Serial.println("Ready!");
}

void loop() {
  unsigned long now = millis();

  // DHT缓存更新 (非阻塞, 每2秒读一次)
  updateDhtCache();

  // WiFi 重连
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    mqttConnected = false;
    Serial.println("[WiFi] Lost, reconnecting...");
    connectWiFi();
    if (wifiConnected) connectMQTT();
  }

  // MQTT 重连
  if (wifiConnected && !mqtt.connected()) {
    mqttConnected = false;
    Serial.println("[MQTT] Lost, reconnecting...");
    connectMQTT();
  }

  // MQTT 循环 (处理收发, 非阻塞)
  if (mqttConnected) {
    mqtt.loop();
  }

  // ═══ 本地实时避障 — 状态机 (100ms 检查, 仅电机运行时生效) ═══
  if (now - tLocalAvoid >= LOCAL_AVOID_MS) {
    tLocalAvoid = now;

    // 仅在电机运行时才做避障 (静止时不主动启动电机)
    if (motorRunning || avoidState != AVOID_NONE) {
      float usCm = readUltrasonic();
      int irL = digitalRead(IR_L_PIN);
      int irR = digitalRead(IR_R_PIN);

      switch (avoidState) {
        case AVOID_NONE:
          // ── 紧急停车: 所有模式通用, < 15cm 必须停 ──
          if (usCm > 0 && usCm < LOCAL_CRITICAL_CM) {
            stopMotors();
            avoidState = AVOID_EMERGENCY;
            avoidStateStart = now;
            avoidTurnRetries = 0;
            Serial.printf("[AVOID] EMERGENCY us=%.1fcm\n", usCm);
          }
          // ── 巡航避障: 仅巡航模式, 15~50cm 触发自动避让 ──
          else if (inCruiseMode && usCm > 0 && usCm < LOCAL_WARN_CM) {
            avoidState = AVOID_BACKWARD;
            avoidStateStart = now;
            avoidTurnRetries = 0;
            diffDrive(-200, -200);
            Serial.printf("[AVOID] CRUISE-BACK us=%.1fcm\n", usCm);
          } else if (inCruiseMode && usCm > 0 && usCm < LOCAL_SAFE_CM) {
            avoidTurnRetries = 0;
            if (irL && !irR) {
              avoidTurnDir = 1;
              avoidState = AVOID_TURN;
              avoidStateStart = now;
              diffDrive(200, -200);
              Serial.printf("[AVOID] CRUISE-TURN-R us=%.1f irL=%d\n", usCm, irL);
            } else if (irR && !irL) {
              avoidTurnDir = -1;
              avoidState = AVOID_TURN;
              avoidStateStart = now;
              diffDrive(-200, 200);
              Serial.printf("[AVOID] CRUISE-TURN-L us=%.1f irR=%d\n", usCm, irR);
            } else {
              avoidState = AVOID_BACKWARD;
              avoidStateStart = now;
              diffDrive(-200, -200);
              Serial.printf("[AVOID] CRUISE-BACK us=%.1fcm\n", usCm);
            }
          }
          // ── 手动模式: 15~50cm 不干预, 由用户自行判断 ──
          break;

        case AVOID_EMERGENCY:
          // 紧急停车中, 等待距离拉开
          if (usCm > LOCAL_WARN_CM) {
            if (inCruiseMode) {
              // 巡航模式: 自动转向探路
              avoidTurnDir = (irL && !irR) ? 1 : -1;
              avoidState = AVOID_TURN;
              avoidStateStart = now;
              if (avoidTurnDir == 1) diffDrive(200, -200);
              else diffDrive(-200, 200);
              Serial.printf("[AVOID] EMER→TURN dir=%d\n", avoidTurnDir);
            } else {
              // 手动模式: 距离安全后直接恢复, 等待用户指令
              avoidState = AVOID_NONE;
              Serial.println("[AVOID] EMER→CLEAR, waiting for command");
            }
          }
          break;

        case AVOID_BACKWARD:
          // 后退中, 检查超时
          if (now - avoidStateStart >= BACKWARD_TIMEOUT_MS) {
            avoidTurnDir = (irL && !irR) ? 1 : ((irR && !irL) ? -1 : -1);
            avoidState = AVOID_TURN;
            avoidStateStart = now;
            if (avoidTurnDir == 1) diffDrive(200, -200);
            else diffDrive(-200, 200);
            Serial.printf("[AVOID] BACK→TURN dir=%d\n", avoidTurnDir);
          } else if (usCm > LOCAL_SAFE_CM) {
            avoidTurnDir = -1;
            avoidState = AVOID_TURN;
            avoidStateStart = now;
            diffDrive(-200, 200);
            Serial.println("[AVOID] BACK→TURN (clear)");
          }
          break;

        case AVOID_TURN:
          // 转向中, 检查超时
          if (now - avoidStateStart >= TURN_TIMEOUT_MS) {
            // 只停电机, 不重置避障状态和巡航标志
            motorRunning = false;
            motorShiftReg = 0;
            motorShiftOut(0);
            ledcWrite(PWM_M1_PIN, 0);
            ledcWrite(PWM_M2_PIN, 0);
            ledcWrite(PWM_M3_PIN, 0);
            ledcWrite(PWM_M4_PIN, 0);
            avoidState = AVOID_PROBE;
            avoidStateStart = now;
            Serial.println("[AVOID] TURN→PROBE, checking path");
          }
          break;

        case AVOID_PROBE:
          // 探路: 静止状态下检查前方距离
          if (usCm > LOCAL_SAFE_CM) {
            // 前方安全! 找到出路
            avoidTurnRetries = 0;
            if (inCruiseMode) {
              // 巡航模式 → 自动恢复前进
              avoidState = AVOID_NONE;
              diffDrive(lastCruisePwmL, lastCruisePwmR);
              Serial.printf("[AVOID] PROBE→RESUME cruise L=%d R=%d\n", lastCruisePwmL, lastCruisePwmR);
            } else {
              // 手动模式 → 停车等待指令
              avoidState = AVOID_NONE;
              Serial.println("[AVOID] PROBE→CLEAR, waiting for command");
            }
          } else if (avoidTurnRetries >= MAX_PROBE_RETRIES) {
            // 探路次数用尽, 放弃
            avoidState = AVOID_NONE;
            avoidTurnRetries = 0;
            Serial.println("[AVOID] PROBE→GIVE UP, no path found, waiting for command");
          } else {
            // 前方仍不安全, 继续同方向转向
            avoidTurnRetries++;
            avoidState = AVOID_TURN;
            avoidStateStart = now;
            if (avoidTurnDir == 1) diffDrive(200, -200);
            else diffDrive(-200, 200);
            Serial.printf("[AVOID] PROBE→TURN dir=%d retry=%d us=%.1f\n", avoidTurnDir, avoidTurnRetries, usCm);
          }
          break;
      }
    }
  }

  // 遥测上报
  if (mqttConnected && now - tTelemetry >= TELEMETRY_MS) {
    sendTelemetry();
    tTelemetry = now;
  }

  // 心跳
  if (mqttConnected && now - tHeartbeat >= HEARTBEAT_MS) {
    sendHeartbeat();
    tHeartbeat = now;
  }

  delay(1);
}

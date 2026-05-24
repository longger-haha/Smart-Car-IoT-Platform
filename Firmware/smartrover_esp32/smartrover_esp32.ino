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

const unsigned long TELEMETRY_MS = 5000;
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
// 接线映射:
//   扩展板 D3  (PWM M1)  → ESP32 GPIO16
//   扩展板 D4  (LATCH)   → ESP32 GPIO18
//   扩展板 D5  (PWM M2)  → ESP32 GPIO19
//   扩展板 D6  (PWM M3)  → ESP32 GPIO17
//   扩展板 D7  (CLOCK)   → ESP32 GPIO23
//   扩展板 D8  (DATA)    → ESP32 GPIO13
//   扩展板 D11 (PWM M4)  → ESP32 GPIO4

#define MOTOR_LATCH_PIN  18   // 74HC595 锁存 (扩展板 D4)
#define MOTOR_CLOCK_PIN  23   // 74HC595 时钟 (扩展板 D7)
#define MOTOR_DATA_PIN   13   // 74HC595 数据 (扩展板 D8)

#define PWM_M1_PIN       16   // M1 右前 PWM (扩展板 D3)
#define PWM_M2_PIN       19   // M2 左前 PWM (扩展板 D5)
#define PWM_M3_PIN       17   // M3 左后 PWM (扩展板 D6)
#define PWM_M4_PIN       4    // M4 右后 PWM (扩展板 D11)

// LEDC 配置
#define LEDC_FREQ     5000
#define LEDC_BITS     8

// 74HC595 移位寄存器位映射 (与 AFMotor Shield 一致)
// Bit7=IN4B, Bit6=IN3B, Bit5=IN4A, Bit4=IN3A, Bit3=IN2B, Bit2=IN1B, Bit1=IN2A, Bit0=IN1A
// M1(右前): IN1A=bit0, IN1B=bit2  (PWM=M1)
// M2(左前): IN2A=bit1, IN2B=bit3  (PWM=M2)
// M3(左后): IN3A=bit4, IN3B=bit6  (PWM=M3)
// M4(右后): IN4A=bit5, IN4B=bit7  (PWM=M4)

#define BIT_IN1A  0  // M1 正转
#define BIT_IN1B  2  // M1 反转
#define BIT_IN2A  1  // M2 正转
#define BIT_IN2B  3  // M2 反转
#define BIT_IN3A  4  // M3 正转
#define BIT_IN3B  6  // M3 反转
#define BIT_IN4A  5  // M4 正转
#define BIT_IN4B  7  // M4 反转

// ═══ 全局对象 ═══

WiFiClient espClient;
PubSubClient mqtt(espClient);
DHT dht(DHT_PIN, DHT_TYPE);

// ═══ 状态变量 ═══

bool wifiConnected = false;
bool mqttConnected = false;
int  speedPwm = 150;
unsigned long msgSeq = 0;
unsigned long tTelemetry = 0;
unsigned long tHeartbeat = 0;

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

  uint8_t* output = (uint8_t*)malloc(totalLen);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, (const uint8_t*)AES_KEY_STR, 128);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, totalLen, iv, input, output);
  mbedtls_aes_free(&aes);

  String result = "AES:";
  result += base64Encode(iv, 16);
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
// 不用 AFMotor 库, 直接操作移位寄存器

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
  motorShiftReg = 0;
  motorShiftOut(0);

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
    // 释放: 两位都0, PWM=0
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
  setMotorAF(BIT_IN2A, BIT_IN2B, PWM_M2_PIN, spd, true);   // M2 左前
  setMotorAF(BIT_IN1A, BIT_IN1B, PWM_M1_PIN, spd, true);   // M1 右前
  setMotorAF(BIT_IN3A, BIT_IN3B, PWM_M3_PIN, spd, true);   // M3 左后
  setMotorAF(BIT_IN4A, BIT_IN4B, PWM_M4_PIN, spd, true);   // M4 右后
  Serial.printf("[MOTOR] fwd spd=%d\n", spd);
}

void bwd(int spd) {
  setMotorAF(BIT_IN2A, BIT_IN2B, PWM_M2_PIN, spd, false);  // M2 左前
  setMotorAF(BIT_IN1A, BIT_IN1B, PWM_M1_PIN, spd, false);  // M1 右前
  setMotorAF(BIT_IN3A, BIT_IN3B, PWM_M3_PIN, spd, false);  // M3 左后
  setMotorAF(BIT_IN4A, BIT_IN4B, PWM_M4_PIN, spd, false);  // M4 右后
  Serial.printf("[MOTOR] bwd spd=%d\n", spd);
}

void rotL(int spd) {
  setMotorAF(BIT_IN2A, BIT_IN2B, PWM_M2_PIN, spd, false);  // M2 左前 反转
  setMotorAF(BIT_IN1A, BIT_IN1B, PWM_M1_PIN, spd, true);   // M1 右前 正转
  setMotorAF(BIT_IN3A, BIT_IN3B, PWM_M3_PIN, spd, false);  // M3 左后 反转
  setMotorAF(BIT_IN4A, BIT_IN4B, PWM_M4_PIN, spd, true);   // M4 右后 正转
  Serial.printf("[MOTOR] rotL spd=%d\n", spd);
}

void rotR(int spd) {
  setMotorAF(BIT_IN2A, BIT_IN2B, PWM_M2_PIN, spd, true);   // M2 左前 正转
  setMotorAF(BIT_IN1A, BIT_IN1B, PWM_M1_PIN, spd, false);  // M1 右前 反转
  setMotorAF(BIT_IN3A, BIT_IN3B, PWM_M3_PIN, spd, true);   // M3 左后 正转
  setMotorAF(BIT_IN4A, BIT_IN4B, PWM_M4_PIN, spd, false);  // M4 右后 反转
  Serial.printf("[MOTOR] rotR spd=%d\n", spd);
}

void stopMotors() {
  motorShiftReg = 0;
  motorShiftOut(0);
  ledcWrite(PWM_M1_PIN, 0);
  ledcWrite(PWM_M2_PIN, 0);
  ledcWrite(PWM_M3_PIN, 0);
  ledcWrite(PWM_M4_PIN, 0);
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
  // 断开 MQTT 避免扫描时断连
  // WiFi.scanNetworks 会短暂断开 STA 连接
  // 使用 async=false 同步扫描 (约 2 秒)
  int n = WiFi.scanNetworks(false, true, false, 300);

  JsonDocument apDoc;
  JsonArray aps = apDoc.to<JsonArray>();

  int count = min(n, WIFI_SCAN_MAX_APS);
  for (int i = 0; i < count; i++) {
    JsonObject ap = aps.createNestedObject();
    // BSSID (MAC 地址)
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

// ═══ WiFi 连接 ═══

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

  if (strcmp(cmd, "forward") == 0)       { speedPwm = spd; fwd(spd); }
  else if (strcmp(cmd, "backward") == 0)  { speedPwm = spd; bwd(spd); }
  else if (strcmp(cmd, "left") == 0)      { speedPwm = spd; rotL(spd); }
  else if (strcmp(cmd, "right") == 0)     { speedPwm = spd; rotR(spd); }
  else if (strcmp(cmd, "stop") == 0)      { stopMotors(); }
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

void sendTelemetry() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  float usCm = readUltrasonic();
  int irL = digitalRead(IR_L_PIN);
  int irR = digitalRead(IR_R_PIN);
  readMPU6050();

  // WiFi 扫描 (每 30 秒一次, 避免频繁断连)
  static unsigned long lastScan = 0;
  if (millis() - lastScan > 30000) {
    scanWiFiAPs();
    lastScan = millis();
  }

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

  // TODO: AES 加密暂时禁用, 调通管道后再启用
  // String encrypted = aesEncrypt(finalJson.c_str());
  // 用 PLAIN: 前缀标记明文数据
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
  setMotorAF(BIT_IN2A, BIT_IN2B, PWM_M2_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M1 右前 forward 1s...");
  setMotorAF(BIT_IN1A, BIT_IN1B, PWM_M1_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M3 左后 forward 1s...");
  setMotorAF(BIT_IN3A, BIT_IN3B, PWM_M3_PIN, 200, true);
  delay(1000); stopMotors(); delay(300);

  Serial.println("[MOTOR-TEST] M4 右后 forward 1s...");
  setMotorAF(BIT_IN4A, BIT_IN4B, PWM_M4_PIN, 200, true);
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

  delay(10);
}

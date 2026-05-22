/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO — 智能小车固件 v3.1 (内存优化版)
 * ═══════════════════════════════════════════════════════════════
 *
 *  针对 UNO 2KB SRAM 极限优化:
 *    - 消除所有 String 对象，改用 char[] + snprintf
 *    - AT 响应缓冲区复用全局静态区
 *    - 航点数上限降至 5
 *    - 精简 IMU 变量，去掉不用的 compAngle
 *    - GPS 串口仅在读取时 listen()
 *    - 遥测 JSON 缓冲区复用
 *
 *  预估 SRAM 占用: ~1100B (剩余 ~900B)
 *
 *  硬件: Arduino UNO R3 + ESP-01S(AT+MQTT) + L293D Shield
 *  传感器: HC-SR04 + DHT11 + MPU6050 + NEO-6M GPS + 红外避障
 *  通信: ESP-01S AT+MQTT  加密: XOR 轻量化
 *
 *  接线:
 *    L293D Shield: M1→左前 M2→右前 M3→左后 M4→右后
 *    HC-SR04:  Trig→D5  Echo→D2
 *    DHT11:    DATA→D7
 *    IR避障:   OUT→D4
 *    MPU6050:  SDA→A4  SCL→A5  (I2C)
 *    GPS:      TX→D8   RX→D9   (SoftwareSerial)
 *    ESP-01S:  TX→A3   RX→A2   (SoftwareSerial)
 *
 *  MQTT 主题:
 *    发布: sensor/<device_id>     遥测 (XOR加密)
 *    发布: heartbeat/<device_id>  心跳
 *    发布: nav/<device_id>        导航事件
 *    订阅: cmd/<device_id>        控制指令
 */

#include <SoftwareSerial.h>
#include <AFMotor.h>
#include <DHT.h>
#include <Wire.h>
#include <TinyGPS++.h>

// ═══════════════════════════════════════════════════════════════
//  配置区
// ═══════════════════════════════════════════════════════════════

const char WIFI_SSID[]     PROGMEM = "YOUR_WIFI_SSID";
const char WIFI_PASSWORD[] PROGMEM = "YOUR_WIFI_PASSWORD";
const char MQTT_HOST[]     PROGMEM = "192.168.1.100";
const char DEVICE_ID[]     PROGMEM = "SMARTROVER_UNO_001";
const char DEVICE_SECRET[] PROGMEM = "your_secret_key";
const char XOR_KEY[]       PROGMEM = "SmartRover2026!!";

const int   MQTT_PORT    = 1883;
const long  HEARTBEAT_MS = 8000;
const long  TELEMETRY_MS = 5000;
const float ARRIVAL_M    = 3.0;

// PID
float pidKp = 2.5, pidKi = 0.02, pidKd = 0.8;
float baseCruiseSpeed = 160;

// ═══════════════════════════════════════════════════════════════
//  引脚
// ═══════════════════════════════════════════════════════════════

#define DHT_PIN       7
#define DHT_TYPE      DHT11
#define TRIG_PIN      5
#define ECHO_PIN      2
#define IR_PIN        4
#define GPS_RX_PIN    8
#define GPS_TX_PIN    9
#define ESP_RX_PIN    A2
#define ESP_TX_PIN    A3
#define MPU_ADDR      0x68

// ═══════════════════════════════════════════════════════════════
//  全局对象
// ═══════════════════════════════════════════════════════════════

SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
DHT dht(DHT_PIN, DHT_TYPE);
TinyGPSPlus gps;

AF_DCMotor motorLF(1);
AF_DCMotor motorRF(2);
AF_DCMotor motorLB(3);
AF_DCMotor motorRB(4);

// ═══════════════════════════════════════════════════════════════
//  共享缓冲区 (避免重复分配)
// ═══════════════════════════════════════════════════════════════

// AT 响应缓冲 — 所有 AT 通信复用
static char atBuf[320];

// 遥测 JSON + 加密输出复用区
static char jsonBuf[300];
static char encBuf[420];

// MQTT 指令缓冲
static char cmdBuf[200];

// ═══════════════════════════════════════════════════════════════
//  状态变量 (精简)
// ═══════════════════════════════════════════════════════════════

bool  wifiOk = false;
bool  mqttOk = false;
int   manualPwm = 150;
float usCm = 999.0;
bool  irObs = false;
unsigned long tTel = 0, tHb = 0;
int   msgSeq = 0;

// IMU (精简: 只保留航向和角速度)
bool  imuOk = false;
float imuHeading = 0.0;
float imuGyroZ = 0.0;
float gyroOffZ = 0.0;
float headingInteg = 0.0;
unsigned long tImu = 0;

// 导航
enum NavState : uint8_t { NAV_IDLE, NAV_CRUISE, NAV_AVOID, NAV_DONE, NAV_ABORT };

struct WP { float lat; float lng; };  // float=4B, 精度~1.1m 足够

static WP wps[5];       // 5航点 x 8B = 40B
static uint8_t wpCount = 0;
static NavState navSt = NAV_IDLE;
static uint8_t wpIdx = 0;
static unsigned long tObs = 0;
float pidI = 0, pidE = 0;

// ═══════════════════════════════════════════════════════════════
//  XOR 加密
// ═══════════════════════════════════════════════════════════════

static const char B64[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void xorEncrypt(const char* plain, const char* key, char* out, int maxLen) {
  int pL = strlen(plain), kL = strlen(key);
  if (pL > 250) pL = 250;  // 限制以适配缓冲区

  static uint8_t x[254];
  for (int i = 0; i < pL; i++)
    x[i] = (uint8_t)plain[i] ^ (uint8_t)key[i % kL];

  int o = 0;
  out[o++] = 'X'; out[o++] = 'O'; out[o++] = 'R'; out[o++] = ':';

  int i = 0;
  while (i < pL) {
    if (o + 5 > maxLen) break;
    uint8_t b0 = x[i];
    uint8_t b1 = (i+1 < pL) ? x[i+1] : 0;
    uint8_t b2 = (i+2 < pL) ? x[i+2] : 0;
    out[o++] = pgm_read_byte(&B64[b0 >> 2]);
    out[o++] = pgm_read_byte(&B64[((b0 & 3) << 4) | (b1 >> 4)]);
    out[o++] = (i+1 < pL) ? pgm_read_byte(&B64[((b1 & 0xF) << 2) | (b2 >> 6)]) : '=';
    out[o++] = (i+2 < pL) ? pgm_read_byte(&B64[b2 & 0x3F]) : '=';
    i += 3;
  }
  out[o] = '\0';
}

// ═══════════════════════════════════════════════════════════════
//  ESP-01S AT+MQTT (无 String 版)
// ═══════════════════════════════════════════════════════════════

// 辅助: 从 PROGMEM 拷贝到栈缓冲
void pgmToBuf(char* buf, int maxLen, const char* pgmStr) {
  int i = 0;
  char c;
  while ((c = pgm_read_byte(pgmStr + i)) && i < maxLen - 1) {
    buf[i++] = c;
  }
  buf[i] = '\0';
}

void sendATBuf(const char* cmd, unsigned long timeout = 2000) {
  espSerial.println(cmd);
  atBuf[0] = '\0';
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    while (espSerial.available()) {
      char c = espSerial.read();
      if (idx < (int)sizeof(atBuf) - 1) atBuf[idx++] = c;
    }
  }
  atBuf[idx < (int)sizeof(atBuf) ? idx : (int)sizeof(atBuf) - 1] = '\0';
}

bool atContains(const char* needle) {
  return strstr(atBuf, needle) != NULL;
}

bool initESP() {
  Serial.println(F("[ESP] 初始化..."));
  delay(1000);
  sendATBuf("AT", 2000);
  if (!atContains("OK")) { Serial.println(F("[ESP] 无响应!")); return false; }
  sendATBuf("ATE0", 1000);
  sendATBuf("AT+CWMODE=1", 2000);
  delay(300);
  sendATBuf("AT+CIPMUX=0", 1000);
  Serial.println(F("[ESP] 就绪"));
  return true;
}

bool connectWiFi() {
  if (!initESP()) return false;

  char ssid[40], pass[40];
  pgmToBuf(ssid, sizeof(ssid), WIFI_SSID);
  pgmToBuf(pass, sizeof(pass), WIFI_PASSWORD);

  Serial.print(F("[WiFi] 连接: "));
  Serial.println(ssid);

  // AT+CWJAP="ssid","pass"
  char cmd[100];
  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
  sendATBuf(cmd, 20000);

  if (atContains("OK") || atContains("GOT IP")) {
    wifiOk = true;
    Serial.println(F("[WiFi] OK!"));
    sendATBuf("AT+CIFSR", 3000);
    // 简单打印 IP
    char* ip = strstr(atBuf, "\"");
    if (ip) { char* e = strstr(ip + 1, "\""); if (e) *e = '\0';
      Serial.print(F("[WiFi] IP: ")); Serial.println(ip + 1); }
    return true;
  }
  wifiOk = false;
  Serial.println(F("[WiFi] 失败!"));
  return false;
}

bool connectMQTT() {
  if (!wifiOk) return false;

  char host[40], devId[30];
  pgmToBuf(host, sizeof(host), MQTT_HOST);
  pgmToBuf(devId, sizeof(devId), DEVICE_ID);

  Serial.print(F("[MQTT] 连接 "));
  Serial.println(host);

  // USERCFG
  char cmd[120];
  snprintf(cmd, sizeof(cmd), "AT+MQTTUSERCFG=0,1,\"%s\",\"\",\"\",0,0,\"\"", devId);
  sendATBuf(cmd, 3000);
  if (!atContains("OK")) { Serial.println(F("[MQTT] CFG 失败")); return false; }

  // CONN
  snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%d,0", host, MQTT_PORT);
  sendATBuf(cmd, 10000);
  if (atContains("OK")) {
    mqttOk = true;
    Serial.println(F("[MQTT] OK!"));
    // SUB
    snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"cmd/%s\",1", devId);
    sendATBuf(cmd, 3000);
    return true;
  }
  mqttOk = false;
  Serial.println(F("[MQTT] 失败!"));
  return false;
}

bool mqttPub(const char* topic, const char* payload) {
  if (!mqttOk) return false;
  // AT+MQTTPUB=0,"topic","payload",0,0
  char cmd[500];
  snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",0,0", topic, payload);
  sendATBuf(cmd, 5000);
  return atContains("OK");
}

// 检查 MQTT 订阅消息，写入 cmdBuf
bool checkCmd() {
  if (!espSerial.available()) return false;

  // 切换到 ESP 串口读取
  espSerial.listen();
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 200) {
    while (espSerial.available() && idx < (int)sizeof(cmdBuf) - 1)
      cmdBuf[idx++] = espSerial.read();
  }
  cmdBuf[idx] = '\0';

  char* recv = strstr(cmdBuf, "+MQTT_SUB_RECV:");
  if (!recv) return false;

  // 找 payload: 第5和第6个引号之间
  int qc = 0, ps = -1, pe = -1;
  for (int i = recv - cmdBuf; i < idx; i++) {
    if (cmdBuf[i] == '"') {
      qc++;
      if (qc == 5) ps = i + 1;
      if (qc == 6) { pe = i; break; }
    }
  }
  if (ps == -1 || pe == -1) return false;

  // 把 payload 移到 cmdBuf 开头
  int len = pe - ps;
  memmove(cmdBuf, cmdBuf + ps, len);
  cmdBuf[len] = '\0';
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  MPU6050 (精简)
// ═══════════════════════════════════════════════════════════════

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission(true);
}

void mpuRead6(int16_t* ax, int16_t* ay, int16_t* az, int16_t* gx, int16_t* gy, int16_t* gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)14, true);
  *ax = Wire.read()<<8 | Wire.read();
  *ay = Wire.read()<<8 | Wire.read();
  *az = Wire.read()<<8 | Wire.read();
  Wire.read(); Wire.read(); // temp
  *gx = Wire.read()<<8 | Wire.read();
  *gy = Wire.read()<<8 | Wire.read();
  *gz = Wire.read()<<8 | Wire.read();
}

void calibGyro() {
  long sum = 0;
  for (int i = 0; i < 300; i++) {  // 300次够了
    int16_t ax,ay,az,gx,gy,gz;
    mpuRead6(&ax,&ay,&az,&gx,&gy,&gz);
    sum += gz; delay(2);
  }
  gyroOffZ = (float)sum / 300.0;
}

void initMPU() {
  Wire.begin(); delay(100);
  mpuWrite(0x6B, 0x80); delay(100);
  mpuWrite(0x6B, 0x03); delay(10);
  mpuWrite(0x1A, 0x03); delay(10);
  mpuWrite(0x1B, 0x18); delay(10);  // 2000°/s
  mpuWrite(0x1C, 0x00); delay(50);  // 2g

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75); Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, (uint8_t)1, true);
  uint8_t id = Wire.read();

  if (id == 0x68 || id == 0x69) {
    imuOk = true;
    calibGyro();
    Serial.println(F("[IMU] MPU6050 OK"));
  } else {
    imuOk = false;
    Serial.println(F("[IMU] 未检测到"));
  }
}

void updateIMU() {
  if (!imuOk) return;
  unsigned long now = millis();
  if (now - tImu < 20) return;
  tImu = now;

  int16_t ax,ay,az,gx,gy,gz;
  mpuRead6(&ax,&ay,&az,&gx,&gy,&gz);
  imuGyroZ = ((float)gz - gyroOffZ) / 131.07f;

  // 只积分 Z 轴航向 (省去 atan2/sqrt 的浮点运算)
  headingInteg += imuGyroZ * 0.02f * 3.14159265f / 180.0f;
  imuHeading = headingInteg * 180.0f / 3.14159265f;
  if (imuHeading < 0) imuHeading += 360.0f;
  if (imuHeading >= 360.0f) imuHeading -= 360.0f;
}

// ═══════════════════════════════════════════════════════════════
//  传感器
// ═══════════════════════════════════════════════════════════════

float readUS() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long d = pulseIn(ECHO_PIN, HIGH, 25000);  // 缩短超时省时间
  if (d == 0) return 999.0f;
  float cm = d * 0.017f;
  return (cm < 2.0f) ? 0.0f : cm;
}

bool readIR() { return digitalRead(IR_PIN) == LOW; }

// ═══════════════════════════════════════════════════════════════
//  电机
// ═══════════════════════════════════════════════════════════════

void stopMotors() {
  motorLF.run(RELEASE); motorRF.run(RELEASE);
  motorLB.run(RELEASE); motorRB.run(RELEASE);
}

void fwd(int s) {
  motorLF.setSpeed(s); motorLF.run(FORWARD);
  motorRF.setSpeed(s); motorRF.run(FORWARD);
  motorLB.setSpeed(s); motorLB.run(FORWARD);
  motorRB.setSpeed(s); motorRB.run(FORWARD);
}

void bwd(int s) {
  motorLF.setSpeed(s); motorLF.run(BACKWARD);
  motorRF.setSpeed(s); motorRF.run(BACKWARD);
  motorLB.setSpeed(s); motorLB.run(BACKWARD);
  motorRB.setSpeed(s); motorRB.run(BACKWARD);
}

void rotL(int s) {
  motorLF.setSpeed(s); motorLF.run(BACKWARD);
  motorLB.setSpeed(s); motorLB.run(BACKWARD);
  motorRF.setSpeed(s); motorRF.run(FORWARD);
  motorRB.setSpeed(s); motorRB.run(FORWARD);
}

void rotR(int s) {
  motorLF.setSpeed(s); motorLF.run(FORWARD);
  motorLB.setSpeed(s); motorLB.run(FORWARD);
  motorRF.setSpeed(s); motorRF.run(BACKWARD);
  motorRB.setSpeed(s); motorRB.run(BACKWARD);
}

void diffDrive(int lSpd, int rSpd) {
  lSpd = constrain(lSpd, 0, 255);
  rSpd = constrain(rSpd, 0, 255);
  if (lSpd > 0) { motorLF.setSpeed(lSpd); motorLF.run(FORWARD);
                   motorLB.setSpeed(lSpd); motorLB.run(FORWARD); }
  else          { motorLF.run(RELEASE); motorLB.run(RELEASE); }
  if (rSpd > 0) { motorRF.setSpeed(rSpd); motorRF.run(FORWARD);
                   motorRB.setSpeed(rSpd); motorRB.run(FORWARD); }
  else          { motorRF.run(RELEASE); motorRB.run(RELEASE); }
}

void execCmd(const char* cmd, int spd) {
  Serial.print(F("[M] ")); Serial.print(cmd);
  Serial.print(F(" ")); Serial.println(spd);
  if (!strcmp(cmd,"forward"))  fwd(spd);
  else if (!strcmp(cmd,"backward")) bwd(spd);
  else if (!strcmp(cmd,"left"))     rotL(spd);
  else if (!strcmp(cmd,"right"))    rotR(spd);
  else if (!strcmp(cmd,"stop"))     stopMotors();
}

// ═══════════════════════════════════════════════════════════════
//  导航数学 (用 float 替代 double)
// ═══════════════════════════════════════════════════════════════

const float D2R = 0.01745329f;
const float R2D = 57.29578f;
const float R_EARTH = 6371000.0f;

void navCalc(float lat1, float lng1, float lat2, float lng2,
             float* dist, float* bearing) {
  float dLat = (lat2 - lat1) * D2R;
  float dLng = (lng2 - lng1) * D2R;
  float c1 = cos(lat1 * D2R), c2 = cos(lat2 * D2R);
  float a = sin(dLat/2) * sin(dLat/2) + c1 * c2 * sin(dLng/2) * sin(dLng/2);
  *dist = R_EARTH * 2 * atan2(sqrt(a), sqrt(1 - a));

  float y = sin(dLng) * c2;
  float x = c1 * sin(lat2*D2R) - sin(lat1*D2R) * c2 * cos(dLng);
  *bearing = atan2(y, x) * R2D;
}

float normAngle(float a) {
  while (a > 180)  a -= 360;
  while (a < -180) a += 360;
  return a;
}

// ═══════════════════════════════════════════════════════════════
//  PID + 巡航
// ═══════════════════════════════════════════════════════════════

float pidSteer(float target, float cur) {
  float e = normAngle(target - cur);
  pidI += e; pidI = constrain(pidI, -80, 80);
  float d = e - pidE; pidE = e;
  float out = pidKp * e + pidKi * pidI + pidKd * d;
  return constrain(out, -120, 120);
}

void resetPID() { pidI = 0; pidE = 0; }

void pubNav(const char* evt, const char* detail) {
  char devId[30]; pgmToBuf(devId, sizeof(devId), DEVICE_ID);
  char topic[40]; snprintf(topic, sizeof(topic), "nav/%s", devId);

  char latS[16], lngS[16];
  if (gps.location.isValid()) {
    dtostrf(gps.location.lat(), 2, 6, latS);
    dtostrf(gps.location.lng(), 2, 6, lngS);
  } else {
    strcpy(latS, "null"); strcpy(lngS, "null");
  }

  const char* stStr = navSt == NAV_IDLE ? "idle" :
                      navSt == NAV_CRUISE ? "cruising" :
                      navSt == NAV_AVOID ? "avoiding" :
                      navSt == NAV_DONE ? "arrived" : "aborted";

  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"event_type\":\"%s\",\"detail\":\"%s\","
    "\"lat\":%s,\"lng\":%s,\"wp\":%d/%d,\"state\":\"%s\"}",
    devId, evt, detail, latS, lngS, wpIdx, wpCount, stStr);
  mqttPub(topic, jsonBuf);
}

void navLoop() {
  if (navSt != NAV_CRUISE && navSt != NAV_AVOID) return;

  // 避障
  if (usCm < 25 || irObs) {
    if (navSt == NAV_CRUISE) {
      navSt = NAV_AVOID; stopMotors(); tObs = millis();
      pubNav("OBSTACLE", usCm < 25 ? "ultrasonic" : "ir");
    }
    unsigned long dt = millis() - tObs;
    if (dt < 1500)       bwd(120);
    else if (dt < 3000)  rotR(150);
    else                 { navSt = NAV_CRUISE; pubNav("CLEARED","ok"); }
    return;
  }

  if (!gps.location.isValid()) { Serial.println(F("[NAV] no GPS")); return; }

  if (wpIdx >= wpCount) {
    navSt = NAV_DONE; stopMotors(); resetPID();
    pubNav("ARRIVED","all done"); navSt = NAV_IDLE;
    return;
  }

  float cLat = gps.location.lat(), cLng = gps.location.lng();
  float dist, bear;
  navCalc(cLat, cLng, wps[wpIdx].lat, wps[wpIdx].lng, &dist, &bear);

  if (dist < ARRIVAL_M) {
    wpIdx++;
    char d[32]; snprintf(d, sizeof(d), "wp %d/%d", wpIdx, wpCount);
    pubNav("WP_REACHED", d);
    if (wpIdx >= wpCount) {
      navSt = NAV_DONE; stopMotors(); resetPID();
      pubNav("ARRIVED","all done"); navSt = NAV_IDLE;
    }
    return;
  }

  float heading = imuHeading;
  if (gps.course.isValid() && gps.speed.kmph() > 3.0f)
    heading = gps.course.deg();

  float steer = pidSteer(bear, heading);
  diffDrive((int)(baseCruiseSpeed - steer), (int)(baseCruiseSpeed + steer));
}

// ═══════════════════════════════════════════════════════════════
//  指令解析 (无 String 版)
// ═══════════════════════════════════════════════════════════════

// 在 cmdBuf 中找 "key": 后面的字符串值，写入 out (最多 outLen)
bool extractStr(const char* key, char* out, int outLen) {
  char search[24];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char* p = strstr(cmdBuf, search);
  if (!p) return false;
  char* colon = strchr(p + strlen(search), ':');
  if (!colon) return false;
  char* q1 = strchr(colon, '"');
  if (!q1) return false;
  char* q2 = strchr(q1 + 1, '"');
  if (!q2) return false;
  int len = q2 - q1 - 1;
  if (len >= outLen) len = outLen - 1;
  memcpy(out, q1 + 1, len);
  out[len] = '\0';
  return true;
}

// 提取数值
float extractNum(const char* key) {
  char search[24];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char* p = strstr(cmdBuf, search);
  if (!p) return NAN;
  char* colon = strchr(p + strlen(search), ':');
  if (!colon) return NAN;
  return atof(colon + 1);
}

void handleCmd() {
  if (!checkCmd()) return;
  Serial.print(F("[CMD] ")); Serial.println(cmdBuf);

  char cmd[16];
  if (!extractStr("command", cmd, sizeof(cmd))) {
    if (!extractStr("cmd", cmd, sizeof(cmd))) return;
  }

  int spd = (int)extractNum("speed_pwm");
  if (spd <= 0 || spd > 255) spd = manualPwm;

  // 运动指令
  if (!strcmp(cmd,"forward") || !strcmp(cmd,"backward") ||
      !strcmp(cmd,"left") || !strcmp(cmd,"right") || !strcmp(cmd,"stop")) {
    if (navSt == NAV_CRUISE || navSt == NAV_AVOID) {
      navSt = NAV_ABORT; stopMotors(); resetPID();
      pubNav("ABORTED","manual"); navSt = NAV_IDLE;
    }
    manualPwm = spd;
    execCmd(cmd, spd);
  }
  // 路线指令
  else if (!strcmp(cmd,"route")) {
    wpCount = 0;
    // 解析航点: 找所有 "lat":xxx,"lng":yyy
    char* p = cmdBuf;
    while (wpCount < 5) {
      char* latP = strstr(p, "\"lat\"");
      if (!latP) break;
      char* lngP = strstr(latP, "\"lng\"");
      if (!lngP) break;

      wps[wpCount].lat = atof(strchr(latP, ':') + 1);
      wps[wpCount].lng = atof(strchr(lngP, ':') + 1);
      wpCount++;
      p = lngP + 4;
    }

    if (wpCount >= 2) {
      wpIdx = 0; navSt = NAV_CRUISE; resetPID();
      headingInteg = 0; imuHeading = 0;
      char d[24]; snprintf(d, sizeof(d), "%d waypoints", wpCount);
      pubNav("START", d);
      Serial.print(F("[NAV] 巡航 ")); Serial.print(wpCount); Serial.println(F(" wp"));
    } else {
      Serial.println(F("[NAV] wp<2"));
    }
  }
  // 中止
  else if (!strcmp(cmd,"abort")) {
    if (navSt == NAV_CRUISE || navSt == NAV_AVOID) {
      navSt = NAV_ABORT; stopMotors(); resetPID();
      pubNav("ABORTED","remote"); navSt = NAV_IDLE;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  遥测上报
// ═══════════════════════════════════════════════════════════════

void sendTelemetry() {
  float temp = dht.readTemperature();
  float hum  = dht.readHumidity();
  usCm = readUS();
  irObs = readIR();
  updateIMU();

  char devId[30]; pgmToBuf(devId, sizeof(devId), DEVICE_ID);
  char sec[30];   pgmToBuf(sec, sizeof(sec), DEVICE_SECRET);
  char key[20];   pgmToBuf(key, sizeof(key), XOR_KEY);

  int n = 0;
  n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "{\"device_id\":\"%s\",", devId);

  // GPS
  if (gps.location.isValid()) {
    char la[16],lo[16],al[12],sp[12];
    dtostrf(gps.location.lat(),2,7,la);
    dtostrf(gps.location.lng(),2,7,lo);
    dtostrf(gps.altitude.meters(),1,1,al);
    dtostrf(gps.speed.kmph(),1,1,sp);
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
      "\"latitude\":%s,\"longitude\":%s,\"altitude\":%s,"
      "\"speed_kmh\":%s,\"satellites\":%d,",
      la, lo, al, sp, gps.satellites.value());
  } else {
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
      "\"latitude\":null,\"longitude\":null,"
      "\"altitude\":null,\"speed_kmh\":null,\"satellites\":0,");
  }

  // 温湿度
  if (!isnan(temp)) { char t[10]; dtostrf(temp,1,1,t);
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "\"temperature\":%s,", t); }
  else n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "\"temperature\":null,");

  if (!isnan(hum)) { char h[10]; dtostrf(hum,1,1,h);
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "\"humidity\":%s,", h); }
  else n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "\"humidity\":null,");

  // 超声波 + 红外
  n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
    "\"ultrasonic_cm\":%.1f,\"ir_obstacle\":%s,",
    usCm, irObs ? "true" : "false");

  // IMU
  if (imuOk) {
    char hd[12],gz[12];
    dtostrf(imuHeading,1,1,hd);
    dtostrf(imuGyroZ,1,2,gz);
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
      "\"imu_heading\":%s,\"imu_gyro_z\":%s,", hd, gz);
  } else {
    n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
      "\"imu_heading\":null,\"imu_gyro_z\":null,");
  }

  // PWM
  n += snprintf(jsonBuf+n, sizeof(jsonBuf)-n, "\"speed_pwm\":%d,", manualPwm);

  // 签名
  uint8_t sig[4] = {0};
  int sL = strlen(sec);
  for (int i = 0; i < n && i < 200; i++)
    sig[i%4] ^= (uint8_t)jsonBuf[i] ^ (uint8_t)sec[i%sL];
  snprintf(jsonBuf+n, sizeof(jsonBuf)-n,
    "\"signature\":\"%02X%02X%02X%02X\"}", sig[0],sig[1],sig[2],sig[3]);

  // XOR 加密
  xorEncrypt(jsonBuf, key, encBuf, sizeof(encBuf));

  char topic[40];
  snprintf(topic, sizeof(topic), "sensor/%s", devId);

  if (mqttPub(topic, encBuf)) {
    Serial.print(F("[T] ")); Serial.println(msgSeq++);
  } else {
    Serial.println(F("[T] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  心跳
// ═══════════════════════════════════════════════════════════════

void sendHb() {
  char devId[30]; pgmToBuf(devId, sizeof(devId), DEVICE_ID);
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"uptime\":%lu,\"wifi\":%d,\"mqtt\":%d}",
    devId, millis()/1000, wifiOk?1:0, mqttOk?1:0);

  char topic[40];
  snprintf(topic, sizeof(topic), "heartbeat/%s", devId);

  if (mqttPub(topic, jsonBuf)) {
    static bool led = false; led = !led;
    digitalWrite(LED_BUILTIN, led ? HIGH : LOW);
  }
}

// ═══════════════════════════════════════════════════════════════
//  避障 (非巡航)
// ═══════════════════════════════════════════════════════════════

void checkObs() {
  usCm = readUS();
  irObs = readIR();
  if (navSt == NAV_CRUISE || navSt == NAV_AVOID) return;
  if ((usCm < 20 && usCm > 0) || irObs) {
    stopMotors();
    Serial.print(F("[OBS] US=")); Serial.print((int)usCm);
    Serial.print(F(" IR=")); Serial.println(irObs?1:0);
  }
}

// ═══════════════════════════════════════════════════════════════
//  RAM 查询
// ═══════════════════════════════════════════════════════════════

int freeRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══════════════════════════════════════════════════════════════
//  setup
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  delay(500);
  Serial.println(F("============================"));
  Serial.println(F(" SmartRover v3.1 (mem-opt)"));
  Serial.println(F("============================"));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_PIN, INPUT);
  dht.begin();
  Serial.println(F("[I] DHT+US+IR ok"));

  initMPU();

  gpsSerial.begin(9600);
  Serial.println(F("[I] GPS ok"));

  stopMotors();
  pinMode(LED_BUILTIN, OUTPUT);

  // ESP
  Serial.println(F("[NET] ..."));
  espSerial.begin(115200);
  delay(500);
  sendATBuf("AT", 2000);
  if (!atContains("OK")) {
    espSerial.begin(9600); delay(200);
    sendATBuf("AT", 2000);
  }
  if (atContains("OK")) Serial.println(F("[ESP] found"));
  else Serial.println(F("[ESP] NOT found!"));

  if (connectWiFi()) connectMQTT();
  else Serial.println(F("[NET] offline"));

  Serial.print(F("[MEM] ")); Serial.print(freeRAM()); Serial.println(F("B free"));
  Serial.println(F("Ready!"));
}

// ═══════════════════════════════════════════════════════════════
//  loop
// ═══════════════════════════════════════════════════════════════

void loop() {
  unsigned long now = millis();

  // GPS: 切换到 GPS 串口读取
  gpsSerial.listen();
  while (gpsSerial.available()) gps.encode(gpsSerial.read());

  updateIMU();

  // 指令: 切换到 ESP 串口
  espSerial.listen();
  handleCmd();

  // 避障 300ms
  static unsigned long tO = 0;
  if (now - tO > 300) { checkObs(); tO = now; }

  // 巡航
  if (navSt == NAV_CRUISE || navSt == NAV_AVOID) navLoop();

  // 遥测
  if (now - tTel > TELEMETRY_MS) { sendTelemetry(); tTel = now; }

  // 心跳
  if (now - tHb > HEARTBEAT_MS) { sendHb(); tHb = now; }

  // 重连
  static unsigned long tRc = 0;
  if (now - tRc > 30000) {
    if (!wifiOk) connectWiFi();
    else if (!mqttOk) connectMQTT();
    tRc = now;
  }

  delay(50);
}

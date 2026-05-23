/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO v3.2  (XOR加密 · 双红外 · MPU6050 · GPS)
 * ═══════════════════════════════════════════════════════════════
 *  硬件: Arduino UNO + ESP-01S + L293D Shield
 *  传感器: DHT11 + HC-SR04 + 红外x2(左/右) + MPU6050 + GPS
 *  通信: ESP-01S AT+MQTT, GPS 硬串口(Serial)
 *  加密: XOR + Base64
 *
 *  ★ 零 String, 全部 char[] + snprintf
 *  ★ 流式 XOR+Base64, 无中间缓冲
 *  ★ 共享 workBuf 互斥复用
 *  ★ 大字符串 PROGMEM
 *  ★ MPU6050 手动 bit-bang I2C (D9/D10)
 *  ★ GPS 硬串口 (Serial, D0/D1)
 *
 *  引脚分配:
 *    D0/D1   GPS 硬串口 (Serial)
 *    D9/D10  MPU6050 SDA/SCL (手动I2C)
 *    D13     DHT11
 *    A0/A1   HC-SR04 TRIG/ECHO
 *    A2/A3   ESP-01S SoftwareSerial RX/TX
 *    A4      右红外
 *    A5      左红外
 *
 *  ⚠ GPS用硬串口时, Serial.println调试输出会发到GPS模块!
 *    开发调试时建议断开GPS的RX线, 或用 #define GPS_HWSERIAL 控制
 * ═══════════════════════════════════════════════════════════════
 */

// ─── 可选功能开关 (注释掉即禁用) ───
#define USE_MPU6050    // 启用陀螺仪
#define USE_GPS        // 启用GPS (硬串口)

#include <AFMotor_R4.h>
#include <SoftwareSerial.h>
#include <DHT.h>

// ═══════════════════════════════════════════════════════════════
//  一、配置区
// ═══════════════════════════════════════════════════════════════

const char WIFI_SSID[]     = "REDMI K80 Ultra";
const char WIFI_PASS[]     = "88888888";

const char MQTT_HOST[]     = "10.32.44.189";
const int  MQTT_PORT       = 1883;
const char MQTT_USER[]     = "";
const char MQTT_PASS[]     = "";

const char DEVICE_ID[]     = "10.32.44.189";

const char DEVICE_SECRET[] PROGMEM =
  "72d579dd57432ef9c9614b1261a0a5ca3de4e8d0135f961b8c826f576b65f21f";
const char XOR_KEY[] PROGMEM = "SmartRover2026!!";

const unsigned long HEARTBEAT_MS  = 8000;
const unsigned long TELEMETRY_MS  = 5000;

// ═══════════════════════════════════════════════════════════════
//  二、引脚定义
// ═══════════════════════════════════════════════════════════════

#define DHT_PIN       13
#define DHT_TYPE      DHT11
#define TRIG_PIN      A0
#define ECHO_PIN      A1
#define IR_L_PIN      A5   // 左红外
#define IR_R_PIN      A4   // 右红外
#define ESP_RX_PIN    A2   // ESP-01S TX → UNO A2
#define ESP_TX_PIN    A3   // UNO A3 → ESP-01S RX (需电阻分压!)

// MPU6050 手动 I2C
#define MPU_SDA       9
#define MPU_SCL       10
#define MPU_ADDR      0x68

// ═══════════════════════════════════════════════════════════════
//  三、全局对象
// ═══════════════════════════════════════════════════════════════

SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
DHT dht(DHT_PIN, DHT_TYPE);

AF_DCMotor motorLF(3);
AF_DCMotor motorRF(2);
AF_DCMotor motorLB(4);
AF_DCMotor motorRB(1);

// ═══════════════════════════════════════════════════════════════
//  四、状态变量
// ═══════════════════════════════════════════════════════════════

bool wifiOk   = false;
bool mqttOk   = false;
bool irL      = false;
bool irR      = false;
int  speedPwm = 150;
float usCm    = 999.0;

// 待处理的 MQTT 指令 (readAT 中检测到 +MQTT_SUB_RECV 时暂存)
bool hasPendingCmd = false;
char pendingCmd[120];

#ifdef USE_MPU6050
float imuHeading = 0.0;
float gyroZOff  = 0.0;
unsigned long tImu = 0;
#endif

#ifdef USE_GPS
float gpsLat = 0.0, gpsLng = 0.0, gpsAlt = 0.0, gpsSpd = 0.0;
int  gpsSats = 0;
bool gpsFix  = false;
// 极简 NMEA 解析 (不用 TinyGPS++, 省 ~120B RAM)
char nmeaBuf[80];
int  nmeaIdx = 0;
#endif

unsigned long tTelemetry = 0;
unsigned long tHeartbeat = 0;
int  msgSeq = 0;

// ═══════════════════════════════════════════════════════════════
//  五、共享缓冲区
// ═══════════════════════════════════════════════════════════════

char workBuf[200];   // AT响应 / MQTT指令
char jsonBuf[280];   // JSON构造

// ═══════════════════════════════════════════════════════════════
//  六、流式 XOR+Base64
// ═══════════════════════════════════════════════════════════════

static const char B64[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 流式 XOR+Base64: 直接输出到 espSerial, 无需 encBuf 缓冲区
// 返回编码后的总字节数
int streamXor(const char* plain, const char* keyP) {
  int pL = strlen(plain);
  int kL = strlen_P(keyP);
  espSerial.print(F("XOR:"));
  int i = 0;
  while (i < pL) {
    uint8_t b0 = (uint8_t)plain[i]     ^ pgm_read_byte(&keyP[i % kL]);
    uint8_t b1 = (i+1<pL)? (uint8_t)plain[i+1] ^ pgm_read_byte(&keyP[(i+1)%kL]) : 0;
    uint8_t b2 = (i+2<pL)? (uint8_t)plain[i+2] ^ pgm_read_byte(&keyP[(i+2)%kL]) : 0;
    espSerial.write(pgm_read_byte(&B64[b0 >> 2]));
    espSerial.write(pgm_read_byte(&B64[((b0&3)<<4)|(b1>>4)]));
    espSerial.write((i+1<pL)? pgm_read_byte(&B64[((b1&0xF)<<2)|(b2>>6)]) : '=');
    espSerial.write((i+2<pL)? pgm_read_byte(&B64[b2&0x3F]) : '=');
    i += 3;
  }
  int b64Len = 4 * ((pL + 2) / 3);
  return 4 + b64Len;  // "XOR:" + base64
}

// 先计算 XOR+Base64 编码后的总长度 (不发送)
int xorEncodedLen(const char* plain) {
  int pL = strlen(plain);
  int b64Len = 4 * ((pL + 2) / 3);
  return 4 + b64Len;  // "XOR:" + base64
}

// ═══════════════════════════════════════════════════════════════
//  七、MPU6050 手动 I2C (bit-bang on D9/D10)
// ═══════════════════════════════════════════════════════════════

#ifdef USE_MPU6050

void twiInit() {
  pinMode(MPU_SCL, OUTPUT);
  pinMode(MPU_SDA, OUTPUT);
  digitalWrite(MPU_SCL, HIGH);
  digitalWrite(MPU_SDA, HIGH);
}

void twiStart() {
  pinMode(MPU_SDA, OUTPUT);
  digitalWrite(MPU_SDA, HIGH);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(MPU_SDA, LOW);
  delayMicroseconds(5);
  digitalWrite(MPU_SCL, LOW);
}

void twiStop() {
  pinMode(MPU_SDA, OUTPUT);
  digitalWrite(MPU_SDA, LOW);
  delayMicroseconds(3);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(5);
  digitalWrite(MPU_SDA, HIGH);
  delayMicroseconds(5);
}

void twiWriteBit(bool b) {
  digitalWrite(MPU_SDA, b ? HIGH : LOW);
  delayMicroseconds(2);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(4);
  digitalWrite(MPU_SCL, LOW);
  delayMicroseconds(2);
}

bool twiReadBit() {
  pinMode(MPU_SDA, INPUT);
  delayMicroseconds(2);
  digitalWrite(MPU_SCL, HIGH);
  delayMicroseconds(4);
  bool b = digitalRead(MPU_SDA);
  digitalWrite(MPU_SCL, LOW);
  delayMicroseconds(2);
  return b;
}

bool twiWriteByte(uint8_t d) {
  for (int i = 7; i >= 0; i--) twiWriteBit((d >> i) & 1);
  return !twiReadBit(); // ACK
}

uint8_t twiReadByte(bool ack) {
  uint8_t d = 0;
  for (int i = 7; i >= 0; i--) d |= (twiReadBit() << i);
  pinMode(MPU_SDA, OUTPUT);
  twiWriteBit(!ack);
  return d;
}

void mpuWrite(uint8_t reg, uint8_t val) {
  twiStart();
  twiWriteByte(MPU_ADDR << 1);
  twiWriteByte(reg);
  twiWriteByte(val);
  twiStop();
}

uint8_t mpuRead1(uint8_t reg) {
  twiStart();
  twiWriteByte(MPU_ADDR << 1);
  twiWriteByte(reg);
  twiStart(); // restart
  twiWriteByte((MPU_ADDR << 1) | 1);
  uint8_t v = twiReadByte(false);
  twiStop();
  return v;
}

// 6字节连续读取 (高字节先)
void mpuReadBurst(uint8_t reg, int16_t* d, int cnt) {
  twiStart();
  twiWriteByte(MPU_ADDR << 1);
  twiWriteByte(reg);
  twiStart();
  twiWriteByte((MPU_ADDR << 1) | 1);
  for (int i = 0; i < cnt; i++) {
    uint8_t hi = twiReadByte(true);
    uint8_t lo = twiReadByte(i < cnt - 1);
    d[i] = ((int16_t)hi << 8) | lo;
  }
  twiStop();
}

void initMPU() {
  twiInit();
  delay(100);
  mpuWrite(0x6B, 0x80);  // reset
  delay(100);
  mpuWrite(0x6B, 0x03);  // PLL_Z, 唤醒
  mpuWrite(0x1A, 0x03);  // DLPF 44Hz
  mpuWrite(0x1B, 0x18);  // ±2000°/s
  mpuWrite(0x1C, 0x00);  // ±2g

  uint8_t wai = mpuRead1(0x75);
  if (wai == 0x68) Serial.println(F("[IMU] MPU6050 OK"));
  else { Serial.print(F("[IMU] WHO_AM_I=0x")); Serial.println(wai, HEX); }

  // 校准: 200次采样取零偏
  delay(500);
  int32_t gzSum = 0;
  int16_t g[3];
  for (int i = 0; i < 200; i++) {
    mpuReadBurst(0x43, g, 3);
    gzSum += g[2];
    delay(5);
  }
  gyroZOff = gzSum / 200.0;
  Serial.print(F("[IMU] gyroZOff=")); Serial.println(gyroZOff, 1);
  tImu = millis();
}

void updateIMU() {
  int16_t g[3];
  mpuReadBurst(0x43, g, 3);
  float gz = (g[2] - gyroZOff) / 16.4;  // ±2000°/s → 16.4 LSB/°/s
  unsigned long now = millis();
  float dt = (now - tImu) / 1000.0;
  if (dt > 0.5) dt = 0.02;
  tImu = now;
  imuHeading += gz * dt;
  if (imuHeading < 0) imuHeading += 360.0;
  if (imuHeading >= 360.0) imuHeading -= 360.0;
}

#endif // USE_MPU6050

// ═══════════════════════════════════════════════════════════════
//  八、GPS 极简 NMEA 解析 (硬串口 Serial)
// ═══════════════════════════════════════════════════════════════

#ifdef USE_GPS

// 解析逗号分隔字段, 返回第fieldIdx个字段的浮点值
// 直接在 nmeaBuf 中操作, 不分配额外内存
float nmeaFloat(int idx) {
  int fi = 0;
  char* p = nmeaBuf;
  while (fi < idx) {
    char* c = strchr(p, ',');
    if (!c) return 0.0;
    p = c + 1;
    fi++;
  }
  return atof(p);
}

// 解析经纬度 (NMEA格式: ddmm.mmmm)
float nmeaCoord(int idx, int dirIdx) {
  float raw = nmeaFloat(idx);
  if (raw == 0.0) return 0.0;
  int deg = (int)(raw / 100);
  float min = raw - deg * 100;
  float coord = deg + min / 60.0;
  // 方向: S或W为负
  int fi = 0;
  char* p = nmeaBuf;
  while (fi < dirIdx) {
    char* c = strchr(p, ',');
    if (!c) return coord;
    p = c + 1;
    fi++;
  }
  if (*p == 'S' || *p == 'W') coord = -coord;
  return coord;
}

// 获取第idx个字段的首字符
char nmeaChar(int idx) {
  int fi = 0;
  char* p = nmeaBuf;
  while (fi < idx) {
    char* c = strchr(p, ',');
    if (!c) return '\0';
    p = c + 1;
    fi++;
  }
  return *p;
}

void parseNMEA() {
  if (strstr(nmeaBuf, "GPRMC") || strstr(nmeaBuf, "GNRMC")) {
    char status = nmeaChar(2);
    if (status == 'A') {
      gpsLat = nmeaCoord(3, 4);
      gpsLng = nmeaCoord(5, 6);
      gpsSpd = nmeaFloat(7) * 1.852;
      gpsFix = true;
    } else {
      gpsFix = false;
    }
  }
  else if (strstr(nmeaBuf, "GPGGA") || strstr(nmeaBuf, "GNGGA")) {
    int fixQ = (int)nmeaFloat(6);
    if (fixQ > 0) {
      gpsSats = (int)nmeaFloat(7);
      gpsAlt = nmeaFloat(9);
      gpsFix = true;
    }
  }
}

void readGPS() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '$') {
      nmeaBuf[nmeaIdx] = '\0';
      if (nmeaIdx > 10) parseNMEA();
      nmeaIdx = 0;
    }
    if (c >= ' ' && nmeaIdx < (int)sizeof(nmeaBuf) - 1) {
      nmeaBuf[nmeaIdx++] = c;
    }
  }
}

#endif // USE_GPS

// ═══════════════════════════════════════════════════════════════
//  九、ESP-01S AT+MQTT
// ═══════════════════════════════════════════════════════════════

void readAT(unsigned long timeout = 2000) {
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    while (espSerial.available()) {
      char c = espSerial.read();
      if (idx < (int)sizeof(workBuf) - 1) workBuf[idx++] = c;
    }
  }
  workBuf[idx < (int)sizeof(workBuf) ? idx : (int)sizeof(workBuf) - 1] = '\0';

  // 检测 +MQTT_SUB_RECV: 并暂存指令 (避免被后续 readAT 覆盖)
  if (!hasPendingCmd) {
    char* recv = strstr(workBuf, "+MQTT_SUB_RECV:");
    if (recv) {
      char* jsonStart = strchr(recv, '{');
      if (jsonStart) {
        char* jsonEnd = strrchr(jsonStart, '}');
        if (jsonEnd) {
          int len = jsonEnd - jsonStart + 1;
          if (len >= (int)sizeof(pendingCmd)) len = sizeof(pendingCmd) - 1;
          memcpy(pendingCmd, jsonStart, len);
          pendingCmd[len] = '\0';
          hasPendingCmd = true;
        }
      }
    }
  }
}

bool atHas(const char* s) { return strstr(workBuf, s) != NULL; }

void sendAT(const char* cmd, unsigned long timeout = 2000) {
  espSerial.println(cmd);
  readAT(timeout);
}

bool initESP() {
  Serial.println(F("[ESP] init..."));
  delay(1000);
  espSerial.begin(9600);
  delay(300);
  sendAT("AT", 2000);
  if (atHas("OK")) {
    Serial.println(F("[ESP] 9600 OK"));
  } else {
    espSerial.begin(115200);
    delay(300);
    sendAT("AT", 2000);
    if (atHas("OK")) {
      Serial.println(F("[ESP] 115200->9600"));
      sendAT("AT+UART_CUR=9600,8,1,0,0", 2000);
      delay(200);
      espSerial.begin(9600);
      delay(300);
      sendAT("AT", 2000);
      if (!atHas("OK")) { Serial.println(F("[ESP] fail")); return false; }
    } else {
      Serial.println(F("[ESP] no resp"));
      return false;
    }
  }
  sendAT("ATE0", 1000);
  sendAT("AT+CWMODE=1", 2000);
  delay(500);
  sendAT("AT+CIPMUX=0", 1000);
  Serial.println(F("[ESP] ready"));
  return true;
}

bool connectWiFi() {
  if (!initESP()) return false;
  Serial.print(F("[WiFi] ")); Serial.println(WIFI_SSID);
  char cmd[100];
  snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", WIFI_SSID, WIFI_PASS);
  sendAT(cmd, 20000);
  if (atHas("OK") || atHas("GOT IP")) {
    wifiOk = true;
    Serial.println(F("[WiFi] OK"));
    sendAT("AT+CIFSR", 3000);
    char* ip = strstr(workBuf, "STAIP");
    if (ip) {
      char* q1 = strchr(ip, '"');
      if (q1) { char* q2 = strchr(q1+1, '"'); if (q2) { *q2='\0'; Serial.print(F("[WiFi] IP: ")); Serial.println(q1+1); *q2='"'; } }
    }
    return true;
  }
  wifiOk = false;
  Serial.println(F("[WiFi] fail"));
  return false;
}

bool connectMQTT() {
  if (!wifiOk) return false;
  Serial.print(F("[MQTT] ")); Serial.print(MQTT_HOST); Serial.print(F(":")); Serial.println(MQTT_PORT);

  char cfg[160];
  snprintf(cfg, sizeof(cfg),
    "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
    DEVICE_ID, MQTT_USER, MQTT_PASS);
  sendAT(cfg, 3000);
  if (!atHas("OK")) { Serial.println(F("[MQTT] cfg fail")); return false; }

  char conn[100];
  snprintf(conn, sizeof(conn), "AT+MQTTCONN=0,\"%s\",%d,0", MQTT_HOST, MQTT_PORT);
  sendAT(conn, 10000);
  if (!atHas("OK")) { Serial.println(F("[MQTT] conn fail")); return false; }

  mqttOk = true;
  Serial.println(F("[MQTT] OK"));

  char sub[80];
  snprintf(sub, sizeof(sub), "AT+MQTTSUB=0,\"cmd/%s\",1", DEVICE_ID);
  sendAT(sub, 3000);
  if (atHas("OK")) Serial.println(F("[MQTT] sub OK"));

  return true;
}

bool mqttPub(const char* topic, const char* payload) {
  if (!mqttOk) return false;
  espSerial.print(F("AT+MQTTPUB=0,\""));
  espSerial.print(topic);
  espSerial.print(F("\",\""));
  espSerial.print(payload);
  espSerial.println(F("\",0,0"));
  readAT(5000);
  bool ok = atHas("OK");
  if (!ok) { Serial.print(F("[MQTT] pub fail: ")); Serial.println(workBuf); }
  return ok;
}

// 用 PUBRAW 发送加密数据 (避免 payload 中的特殊字符破坏 AT 指令)
// 步骤: 1) AT+MQTTPUBRAW=0,"topic",len,0,0  2) 等待 > 提示  3) 发送数据
bool mqttPubRaw(const char* topic, const char* jsonPlain, const char* keyP) {
  if (!mqttOk) return false;

  int dataLen = xorEncodedLen(jsonPlain);

  // 步骤1: 发送 PUBRAW 命令头
  espSerial.print(F("AT+MQTTPUBRAW=0,\""));
  espSerial.print(topic);
  espSerial.print(F("\","));
  espSerial.print(dataLen);
  espSerial.println(F(",0,0"));

  // 步骤2: 等待 ">" 提示符
  readAT(3000);
  if (!atHas(">")) {
    Serial.print(F("[MQTT] PUBRAW no >: ")); Serial.println(workBuf);
    return false;
  }

  // 步骤3: 发送 XOR 加密数据
  streamXor(jsonPlain, keyP);

  // 步骤4: 等待发送结果
  readAT(5000);
  bool ok = atHas("OK");
  if (!ok) {
    Serial.print(F("[MQTT] PUBRAW fail: ")); Serial.println(workBuf);
  }
  return ok;
}

bool checkMQTTMsg() {
  // 优先处理 readAT 暂存的指令
  if (hasPendingCmd) {
    memcpy(workBuf, pendingCmd, strlen(pendingCmd) + 1);
    hasPendingCmd = false;
    return true;
  }

  // 直接从串口读取
  if (!espSerial.available()) return false;
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 200 && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1)
      workBuf[idx++] = espSerial.read();
  }
  workBuf[idx] = '\0';

  // 查找 +MQTT_SUB_RECV: 中的 JSON payload
  char* recv = strstr(workBuf, "+MQTT_SUB_RECV:");
  if (!recv) return false;

  // 格式: +MQTT_SUB_RECV:0,"topic",len\r\n{...} 或 +MQTT_SUB_RECV:0,"topic",len,{...}
  char* jsonStart = strchr(recv, '{');
  if (!jsonStart) return false;
  char* jsonEnd = strrchr(jsonStart, '}');
  if (!jsonEnd) return false;

  int len = jsonEnd - jsonStart + 1;
  memmove(workBuf, jsonStart, len);
  workBuf[len] = '\0';
  return true;
}

// ═══════════════════════════════════════════════════════════════
//  十、传感器
// ═══════════════════════════════════════════════════════════════

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  if (dur == 0) return 999.0;
  float cm = dur * 0.034 / 2.0;
  return (cm < 2.0) ? 0.0 : cm;
}

void readIR() {
  irL = (digitalRead(IR_L_PIN) == LOW);
  irR = (digitalRead(IR_R_PIN) == LOW);
}

// ═══════════════════════════════════════════════════════════════
//  十一、电机
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

// ═══════════════════════════════════════════════════════════════
//  十二、指令解析
// ═══════════════════════════════════════════════════════════════

bool jsonStr(const char* key, char* out, int maxLen) {
  char search[32];
  snprintf(search, sizeof(search), "\"%s\"", key);
  char* k = strstr(workBuf, search);
  if (!k) return false;
  char* colon = strchr(k, ':');
  if (!colon) return false;
  colon++;
  while (*colon == ' ') colon++;
  if (*colon == '"') {
    char* q1 = colon + 1;
    char* q2 = strchr(q1, '"');
    if (!q2) return false;
    int len = q2 - q1;
    if (len >= maxLen) len = maxLen - 1;
    memcpy(out, q1, len); out[len] = '\0';
    return true;
  }
  char* end = colon;
  while (*end && *end != ',' && *end != '}') end++;
  int len = end - colon;
  if (len >= maxLen) len = maxLen - 1;
  memcpy(out, colon, len); out[len] = '\0';
  return true;
}

void handleCommand() {
  if (!checkMQTTMsg()) return;
  Serial.print(F("[CMD] ")); Serial.println(workBuf);
  char cmd[20] = "";
  if (!jsonStr("command", cmd, sizeof(cmd)))
    jsonStr("cmd", cmd, sizeof(cmd));
  if (cmd[0] == '\0') return;
  char spdStr[8] = "";
  int spd = speedPwm;
  if (jsonStr("speed_pwm", spdStr, sizeof(spdStr))) {
    int v = atoi(spdStr);
    if (v > 0 && v <= 255) spd = v;
  }
  if (strcmp(cmd, "forward") == 0)       { speedPwm = spd; fwd(spd); }
  else if (strcmp(cmd, "backward") == 0)  { speedPwm = spd; bwd(spd); }
  else if (strcmp(cmd, "left") == 0)      { speedPwm = spd; rotL(spd); }
  else if (strcmp(cmd, "right") == 0)     { speedPwm = spd; rotR(spd); }
  else if (strcmp(cmd, "stop") == 0)      { stopMotors(); }
}

// ═══════════════════════════════════════════════════════════════
//  十三、遥测上报
// ═══════════════════════════════════════════════════════════════

// Arduino UNO 的 snprintf 不支持 %f, 用 dtostrf 转换
char fBuf[12];  // 浮点转字符串临时缓冲

void sendTelemetry() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  usCm = readUltrasonic();
  readIR();

#ifdef USE_MPU6050
  updateIMU();
#endif

#ifdef USE_GPS
  readGPS();
#endif

  int n = 0;
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "{\"device_id\":\"%s\",", DEVICE_ID);

#ifdef USE_GPS
  if (gpsFix) {
    dtostrf(gpsLat, 1, 4, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"latitude\":%s,", fBuf);
    dtostrf(gpsLng, 1, 4, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"longitude\":%s,", fBuf);
  } else
#endif
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"latitude\":null,\"longitude\":null,");

  if (!isnan(temp)) {
    dtostrf(temp, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":%s,", fBuf);
  } else
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":null,");

  if (!isnan(humi)) {
    dtostrf(humi, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":%s,", fBuf);
  } else
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":null,");

  dtostrf(usCm, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"ultrasonic_cm\":%s,", fBuf);

  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"ir_obstacle\":%s,", (irL || irR) ? "true" : "false");

  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"ir_left\":%s,\"ir_right\":%s,", irL ? "true" : "false", irR ? "true" : "false");

#ifdef USE_MPU6050
  dtostrf(imuHeading, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"imu_heading\":%s,", fBuf);
#endif

  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"speed_pwm\":%d,", speedPwm);

#ifdef USE_GPS
  if (gpsFix) {
    dtostrf(gpsAlt, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"altitude\":%s,", fBuf);
    dtostrf(gpsSpd, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"speed_kmh\":%s,", fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"satellites\":%d,", gpsSats);
  }
#endif

  // XOR 校验和签名
  uint8_t sig[5] = {0};
  int sLen = strlen_P(DEVICE_SECRET);
  int jLen = strlen(jsonBuf);
  for (int i = 0; i < jLen && i < 250; i++)
    sig[i % 4] ^= (uint8_t)jsonBuf[i] ^ pgm_read_byte(&DEVICE_SECRET[i % sLen]);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"signature\":\"%02X%02X%02X%02X\"}", sig[0], sig[1], sig[2], sig[3]);

  // 用 PUBRAW 发送 (payload 中的特殊字符不会破坏 AT 指令)
  char topic[64];
  snprintf(topic, sizeof(topic), "sensor/%s", DEVICE_ID);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.print(F("[TEL] OK seq=")); Serial.println(msgSeq++);
  } else {
    Serial.println(F("[TEL] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十四、心跳
// ═══════════════════════════════════════════════════════════════

void sendHeartbeat() {
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":%s}",
    DEVICE_ID, millis() / 1000,
    wifiOk ? "true" : "false", mqttOk ? "true" : "false");

  // 心跳也用 PUBRAW 发送 XOR 加密数据
  char topic[64];
  snprintf(topic, sizeof(topic), "heartbeat/%s", DEVICE_ID);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.println(F("[HB] OK"));
  } else {
    Serial.println(F("[HB] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十五、智能避障 (超声波 + 双红外)
// ═══════════════════════════════════════════════════════════════

void checkObstacle() {
  usCm = readUltrasonic();
  readIR();

  if (usCm < 20 && usCm > 0) {
    stopMotors();
    Serial.print(F("[OBS] ")); Serial.print(usCm, 1); Serial.println(F("cm"));
    if (irL && !irR)       rotR(180);
    else if (irR && !irL)  rotL(180);
    else                   bwd(150);
    delay(500);
    stopMotors();
    return;
  }
  if (irL && irR) { bwd(150); delay(400); stopMotors(); }
  else if (irL)   { rotR(180); delay(300); stopMotors(); }
  else if (irR)   { rotL(180); delay(300); stopMotors(); }
}

// ═══════════════════════════════════════════════════════════════
//  十六、内存
// ═══════════════════════════════════════════════════════════════

int freeRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══════════════════════════════════════════════════════════════
//  十七、主程序
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println(F("========================================"));
  Serial.println(F(" SmartRover UNO v3.2 (full sensors)"));
  Serial.println(F("========================================"));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_L_PIN, INPUT);
  pinMode(IR_R_PIN, INPUT);
  dht.begin();
  Serial.println(F("[INIT] DHT+SR04+IR OK"));

  stopMotors();
  Serial.println(F("[INIT] L293D OK"));

#ifdef USE_MPU6050
  initMPU();
#endif

#ifdef USE_GPS
  // GPS 硬串口已通过 Serial 初始化 (9600)
  // 注意: GPS的TX接UNO D0(RX), GPS的RX接UNO D1(TX)
  // 调试时如果GPS连着, Serial.println会发到GPS模块
  Serial.println(F("[INIT] GPS on Serial (9600)"));
#endif

  Serial.println(F("[NET] connecting..."));
  if (connectWiFi()) connectMQTT();
  else Serial.println(F("[NET] WiFi fail, offline"));

  Serial.print(F("[MEM] RAM: ")); Serial.print(freeRAM()); Serial.println(F("B"));
  Serial.println(F("Ready!"));
}

void loop() {
  unsigned long now = millis();

#ifdef USE_GPS
  readGPS();
#endif

  handleCommand();

  static unsigned long tObs = 0;
  if (now - tObs > 300) { checkObstacle(); tObs = now; }

  if (now - tTelemetry > TELEMETRY_MS) { sendTelemetry(); tTelemetry = now; }

  if (now - tHeartbeat > HEARTBEAT_MS) { sendHeartbeat(); tHeartbeat = now; }

  static unsigned long tReconn = 0;
  if (now - tReconn > 30000) {
    if (!wifiOk) { Serial.println(F("[NET] reWiFi")); connectWiFi(); }
    else if (!mqttOk) { Serial.println(F("[NET] reMQTT")); connectMQTT(); }
    tReconn = now;
  }

  delay(50);
}

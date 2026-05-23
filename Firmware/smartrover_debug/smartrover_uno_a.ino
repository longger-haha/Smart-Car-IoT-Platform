/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO v5.0 (单板版 · 无GPS)
 * ═══════════════════════════════════════════════════════════════
 *  硬件: Arduino UNO + ESP-01S + L293D Shield
 *  传感器: DHT11 + HC-SR04 + 红外x2 + MPU6050
 *  通信: ESP-01S AT+MQTT
 *  加密: XOR + Base64
 *
 *  引脚分配:
 *    D0/D1   Serial (USB调试)
 *    D9/D10  红外 左/右
 *    D13     DHT11
 *    A0/A1   HC-SR04 TRIG/ECHO
 *    A2/A3   ESP-01S SoftwareSerial RX/TX
 *    A4/A5   MPU6050 SDA/SCL (硬件I2C)
 *    AFMotor: D3/D4/D5/D6/D7/D8/D11/D12
 *
 *  数据分两批发送:
 *    批1: sensor/<id>     → 环境数据 (温湿度/超声波/红外)
 *    批2: sensor/<id>/imu → 6轴IMU数据
 *
 *  RAM: ~1560B  Flash: ~22KB
 * ═══════════════════════════════════════════════════════════════
 */

#define SS_MAX_RX_BUFF 32
#include <AFMotor_R4.h>
#include <SoftwareSerial.h>
#include <Wire.h>
#include <DHT.h>

// ═══════════════════════════════════════════════════════════════
//  一、配置区
// ═══════════════════════════════════════════════════════════════

const char WIFI_SSID[]     = "REDMI K80";
const char WIFI_PASS[]     = "88888888";

const char MQTT_HOST[]     = "172.22.32.61";
const int  MQTT_PORT       = 1883;
const char MQTT_USER[]     = "";
const char MQTT_PASS[]     = "";

const char DEVICE_ID[]     = "smartrover_007";

const char DEVICE_SECRET[] PROGMEM =
  "1b8c58fbdc4be422633f61c357c7f3b4ba6b2d0b211e89c0a27771ad66449f48";
const char XOR_KEY[] PROGMEM = "SmartRover2026!!";

const unsigned long HEARTBEAT_MS   = 8000;
const unsigned long TELEMETRY_MS   = 5000;
const unsigned long CMD_TIMEOUT_MS = 15000;

// ═══════════════════════════════════════════════════════════════
//  二、引脚定义
// ═══════════════════════════════════════════════════════════════

#define DHT_PIN       13
#define DHT_TYPE      DHT11
#define TRIG_PIN      A0    // HC-SR04 TRIG
#define ECHO_PIN      A1    // HC-SR04 ECHO
#define IR_L_PIN      9
#define IR_R_PIN      10
#define ESP_RX_PIN    A2    // ESP-01S TX → UNO A2
#define ESP_TX_PIN    A3    // UNO A3 → ESP-01S RX (需电阻分压!)

// MPU6050 硬件 I2C (A4=SDA, A5=SCL)
#define MPU_ADDR      0x68

// ═══════════════════════════════════════════════════════════════
//  三、全局对象
// ═══════════════════════════════════════════════════════════════

SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
DHT dht(DHT_PIN, DHT_TYPE);

AF_DCMotor motorLF(2);
AF_DCMotor motorRF(1);
AF_DCMotor motorLB(3);
AF_DCMotor motorRB(4);

// ═══════════════════════════════════════════════════════════════
//  四、状态变量
// ═══════════════════════════════════════════════════════════════

bool wifiOk   = false;
bool mqttOk   = false;
bool irL      = false;
bool irR      = false;
int  speedPwm = 150;

unsigned long lastCmdTime = 0;
bool safetyStopped = false;

// MPU6050
float gyroZOff = 0.0;
float imuAx = 0, imuAy = 0, imuAz = 0;
float imuGx = 0, imuGy = 0, imuGz = 0;

unsigned long tTelemetry = 0;
unsigned long tHeartbeat = 0;
int  msgSeq = 0;

// ═══════════════════════════════════════════════════════════════
//  五、共享缓冲区
// ═══════════════════════════════════════════════════════════════

char workBuf[100];   // AT响应 / MQTT指令
char jsonBuf[160];   // JSON构造
char fBuf[10];       // dtostrf 临时

// ═══════════════════════════════════════════════════════════════
//  六、流式 XOR+Base64
// ═══════════════════════════════════════════════════════════════

static const char B64[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

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
  return 4 + b64Len;
}

int xorEncodedLen(const char* plain) {
  int pL = strlen(plain);
  int b64Len = 4 * ((pL + 2) / 3);
  return 4 + b64Len;
}

// ═══════════════════════════════════════════════════════════════
//  七、MPU6050 硬件 I2C
// ═══════════════════════════════════════════════════════════════

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

void mpuReadBurst(uint8_t reg, uint8_t* buf, uint8_t cnt) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)cnt);
  for (uint8_t i = 0; i < cnt; i++) buf[i] = Wire.read();
}

void initMPU() {
  Wire.begin();
  delay(100);
  mpuWrite(0x6B, 0x80);  // reset
  delay(100);
  mpuWrite(0x6B, 0x03);  // PLL_Z, 唤醒
  mpuWrite(0x1A, 0x03);  // DLPF 44Hz
  mpuWrite(0x1B, 0x18);  // ±2000°/s
  mpuWrite(0x1C, 0x00);  // ±2g

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  uint8_t wai = Wire.read();
  if (wai == 0x68) Serial.println(F("[IMU] MPU6050 OK"));
  else { Serial.print(F("[IMU] WHO_AM_I=0x")); Serial.println(wai, HEX); }

  delay(500);
  int32_t gzSum = 0;
  uint8_t buf[6];
  for (int i = 0; i < 200; i++) {
    mpuReadBurst(0x43, buf, 6);
    int16_t gz = (int16_t)((buf[4] << 8) | buf[5]);
    gzSum += gz;
    delay(5);
  }
  gyroZOff = gzSum / 200.0;
  Serial.print(F("[IMU] gyroZOff=")); Serial.println(gyroZOff, 1);
}

void readMPU() {
  uint8_t buf[6];
  mpuReadBurst(0x3B, buf, 6);  // 加速度
  int16_t ax = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t ay = (int16_t)((buf[2] << 8) | buf[3]);
  int16_t az = (int16_t)((buf[4] << 8) | buf[5]);

  mpuReadBurst(0x43, buf, 6);  // 陀螺仪
  int16_t gx = (int16_t)((buf[0] << 8) | buf[1]);
  int16_t gy = (int16_t)((buf[2] << 8) | buf[3]);
  int16_t gz = (int16_t)((buf[4] << 8) | buf[5]);

  imuAx = ax / 16384.0;
  imuAy = ay / 16384.0;
  imuAz = az / 16384.0;
  imuGx = gx / 16.4;
  imuGy = gy / 16.4;
  imuGz = (gz - gyroZOff) / 16.4;
}

// ═══════════════════════════════════════════════════════════════
//  八、ESP-01S AT+MQTT
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
  char cmd[60];
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

  char cfg[80];
  snprintf(cfg, sizeof(cfg),
    "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
    DEVICE_ID, MQTT_USER, MQTT_PASS);
  sendAT(cfg, 3000);
  if (!atHas("OK")) { Serial.println(F("[MQTT] cfg fail")); return false; }

  char conn[60];
  snprintf(conn, sizeof(conn), "AT+MQTTCONN=0,\"%s\",%d,0", MQTT_HOST, MQTT_PORT);
  sendAT(conn, 10000);
  if (!atHas("OK")) { Serial.println(F("[MQTT] conn fail")); return false; }

  mqttOk = true;
  Serial.println(F("[MQTT] OK"));

  char sub[60];
  snprintf(sub, sizeof(sub), "AT+MQTTSUB=0,\"cmd/%s\",1", DEVICE_ID);
  sendAT(sub, 3000);
  if (atHas("OK")) Serial.println(F("[MQTT] sub OK"));

  return true;
}

bool mqttPubRaw(const char* topic, const char* jsonPlain, const char* keyP) {
  if (!mqttOk) return false;

  int dataLen = xorEncodedLen(jsonPlain);

  espSerial.print(F("AT+MQTTPUBRAW=0,\""));
  espSerial.print(topic);
  espSerial.print(F("\","));
  espSerial.print(dataLen);
  espSerial.println(F(",0,0"));

  readAT(3000);
  if (!atHas(">")) {
    Serial.print(F("[MQTT] PUBRAW no >: ")); Serial.println(workBuf);
    return false;
  }

  streamXor(jsonPlain, keyP);

  readAT(5000);
  bool ok = atHas("OK");
  if (!ok) {
    Serial.print(F("[MQTT] PUBRAW fail: ")); Serial.println(workBuf);
  }
  return ok;
}

bool checkMQTTMsg() {
  if (!espSerial.available()) return false;
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 200 && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1)
      workBuf[idx++] = espSerial.read();
  }
  workBuf[idx] = '\0';

  char* recv = strstr(workBuf, "+MQTT_SUB_RECV:");
  if (!recv) return false;

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
//  九、传感器 (DHT11 + HC-SR04 + 红外)
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
//  十、电机
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
  motorRF.setSpeed(s); motorRB.run(BACKWARD);
  motorRB.setSpeed(s); motorRB.run(BACKWARD);
}

void diffDrive(int pwmL, int pwmR) {
  if (pwmL > 0) {
    motorLF.setSpeed(min(pwmL, 255)); motorLF.run(FORWARD);
    motorLB.setSpeed(min(pwmL, 255)); motorLB.run(FORWARD);
  } else if (pwmL < 0) {
    int s = min(-pwmL, 255);
    motorLF.setSpeed(s); motorLF.run(BACKWARD);
    motorLB.setSpeed(s); motorLB.run(BACKWARD);
  } else {
    motorLF.run(RELEASE); motorLB.run(RELEASE);
  }
  if (pwmR > 0) {
    motorRF.setSpeed(min(pwmR, 255)); motorRF.run(FORWARD);
    motorRB.setSpeed(min(pwmR, 255)); motorRB.run(FORWARD);
  } else if (pwmR < 0) {
    int s = min(-pwmR, 255);
    motorRF.setSpeed(s); motorRF.run(BACKWARD);
    motorRB.setSpeed(s); motorRB.run(BACKWARD);
  } else {
    motorRF.run(RELEASE); motorRB.run(RELEASE);
  }
}

// ═══════════════════════════════════════════════════════════════
//  十一、指令解析
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

int jsonInt(const char* key, int defaultVal = 0) {
  char s[12] = "";
  if (!jsonStr(key, s, sizeof(s))) return defaultVal;
  return atoi(s);
}

void handleCommand() {
  if (!checkMQTTMsg()) return;
  Serial.print(F("[CMD] ")); Serial.println(workBuf);

  char cmd[20] = "";
  if (!jsonStr("command", cmd, sizeof(cmd)))
    jsonStr("cmd", cmd, sizeof(cmd));
  if (cmd[0] == '\0') return;

  lastCmdTime = millis();
  safetyStopped = false;

  if (strcmp(cmd, "diff") == 0) {
    int pwmL = jsonInt("pwm_l", 0);
    int pwmR = jsonInt("pwm_r", 0);
    diffDrive(pwmL, pwmR);
    Serial.print(F("[DIFF] L=")); Serial.print(pwmL);
    Serial.print(F(" R=")); Serial.println(pwmR);
    return;
  }

  char spdStr[8] = "";
  int spd = speedPwm;
  if (jsonStr("speed_pwm", spdStr, sizeof(spdStr))) {
    int v = atoi(spdStr);
    if (v > 0 && v <= 255) spd = v;
  }
  if (jsonStr("pwm", spdStr, sizeof(spdStr))) {
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
//  十二、遥测上报 (分批发送)
// ═══════════════════════════════════════════════════════════════

void addSignature(int& n) {
  uint8_t sig[4] = {0};
  int sLen = strlen_P(DEVICE_SECRET);
  int jLen = strlen(jsonBuf);
  for (int i = 0; i < jLen && i < 250; i++)
    sig[i % 4] ^= (uint8_t)jsonBuf[i] ^ pgm_read_byte(&DEVICE_SECRET[i % sLen]);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"signature\":\"%02X%02X%02X%02X\"}", sig[0], sig[1], sig[2], sig[3]);
}

// ── 第一批: 环境数据 → sensor/<device_id> ──
void sendTelemetry() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  float usCm = readUltrasonic();
  readIR();

  int n = 0;
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "{\"device_id\":\"%s\",", DEVICE_ID);

  if (!isnan(temp)) {
    dtostrf(temp, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":%s,", fBuf);
  } else {
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":null,");
  }
  if (!isnan(humi)) {
    dtostrf(humi, 1, 1, fBuf);
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":%s,", fBuf);
  } else {
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":null,");
  }

  dtostrf(usCm, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"ultrasonic_cm\":%s,\"ir_l\":%d,\"ir_r\":%d,"
    "\"speed_pwm\":%d,\"seq\":%d,",
    fBuf, irL ? 1 : 0, irR ? 1 : 0, speedPwm, msgSeq);
  addSignature(n);

  char topic[30];
  snprintf(topic, sizeof(topic), "sensor/%s", DEVICE_ID);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.print(F("[ENV] OK seq=")); Serial.println(msgSeq);
  } else {
    Serial.println(F("[ENV] fail"));
  }
}

// ── 第二批: IMU数据 → sensor/<device_id>/imu ──
void sendIMU() {
  readMPU();

  int n = 0;
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "{\"device_id\":\"%s\",", DEVICE_ID);

  dtostrf(imuAx, 1, 2, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_ax\":%s,", fBuf);
  dtostrf(imuAy, 1, 2, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_ay\":%s,", fBuf);
  dtostrf(imuAz, 1, 2, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_az\":%s,", fBuf);
  dtostrf(imuGx, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_gx\":%s,", fBuf);
  dtostrf(imuGy, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_gy\":%s,", fBuf);
  dtostrf(imuGz, 1, 1, fBuf);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_gz\":%s,", fBuf);

  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"seq\":%d,", msgSeq);
  addSignature(n);

  char topic[40];
  snprintf(topic, sizeof(topic), "sensor/%s/imu", DEVICE_ID);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.print(F("[IMU] OK seq=")); Serial.println(msgSeq++);
  } else {
    Serial.println(F("[IMU] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十三、心跳
// ═══════════════════════════════════════════════════════════════

void sendHeartbeat() {
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":%s}",
    DEVICE_ID, millis() / 1000,
    wifiOk ? "true" : "false", mqttOk ? "true" : "false");

  char topic[40];
  snprintf(topic, sizeof(topic), "heartbeat/%s", DEVICE_ID);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.println(F("[HB] OK"));
  } else {
    Serial.println(F("[HB] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十四、安全保护
// ═══════════════════════════════════════════════════════════════

void checkCmdTimeout() {
  if (lastCmdTime == 0) return;
  if (safetyStopped) return;
  if (millis() - lastCmdTime > CMD_TIMEOUT_MS) {
    stopMotors();
    safetyStopped = true;
    Serial.println(F("[SAFETY] 指令超时, 自动停车"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  十五、内存
// ═══════════════════════════════════════════════════════════════

int freeRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══════════════════════════════════════════════════════════════
//  十六、主程序
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println(F("SmartRover UNO v5.0 (单板·无GPS)"));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_L_PIN, INPUT);
  pinMode(IR_R_PIN, INPUT);
  dht.begin();

  initMPU();
  stopMotors();

  Serial.println(F("[NET] connecting..."));
  if (connectWiFi()) connectMQTT();
  else Serial.println(F("[NET] WiFi fail"));

  Serial.print(F("[MEM] RAM: ")); Serial.print(freeRAM()); Serial.println(F("B")));
}

void loop() {
  unsigned long now = millis();

  handleCommand();
  checkCmdTimeout();

  if (now - tTelemetry > TELEMETRY_MS) {
    sendTelemetry();
    sendIMU();
    tTelemetry = now;
  }

  if (now - tHeartbeat > HEARTBEAT_MS) { sendHeartbeat(); tHeartbeat = now; }

  static unsigned long tReconn = 0;
  if (now - tReconn > 30000) {
    if (!wifiOk) { Serial.println(F("[NET] reWiFi")); connectWiFi(); }
    else if (!mqttOk) { Serial.println(F("[NET] reMQTT")); connectMQTT(); }
    tReconn = now;
  }

  delay(50);
}

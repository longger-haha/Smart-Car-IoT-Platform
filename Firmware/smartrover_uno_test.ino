/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO v3.2-test  (基础连接测试版)
 * ═══════════════════════════════════════════════════════════════
 *  仅测试: DHT11 + HC-SR04 + L293D + ESP-01S (WiFi+MQTT)
 *  目标: 验证与平台通信是否正常
 *
 *  引脚:
 *    D13     DHT11
 *    A0/A1   HC-SR04 TRIG/ECHO
 *    A2/A3   ESP-01S RX/TX
 * ═══════════════════════════════════════════════════════════════
 */

#include <AFMotor_R4.h>
#include <SoftwareSerial.h>
#include <DHT.h>

// ═══ 配置 ═══

const char WIFI_SSID[] PROGMEM = "REDMI K80 Ultra";
const char WIFI_PASS[] PROGMEM = "88888888";

const char MQTT_HOST[] PROGMEM = "10.32.44.189";
const int  MQTT_PORT           = 1883;
const char MQTT_USER[] PROGMEM = "";
const char MQTT_PASS[] PROGMEM = "";

const char DEVICE_ID[] PROGMEM = "10.32.44.189";

const char DEVICE_SECRET[] PROGMEM =
  "72d579dd57432ef9c9614b1261a0a5ca3de4e8d0135f961b8c826f576b65f21f";
const char XOR_KEY[] PROGMEM = "SmartRover2026!!";

const unsigned long HEARTBEAT_MS  = 15000;
const unsigned long TELEMETRY_MS  = 10000;

// ═══ 引脚 ═══

#define DHT_PIN       13
#define DHT_TYPE      DHT11
#define TRIG_PIN      A0
#define ECHO_PIN      A1
#define ESP_RX_PIN    A2
#define ESP_TX_PIN    A3

// ═══ 全局对象 ═══

SoftwareSerial espSerial(ESP_RX_PIN, ESP_TX_PIN);
DHT dht(DHT_PIN, DHT_TYPE);

AF_DCMotor motorLF(3);
AF_DCMotor motorRF(2);
AF_DCMotor motorLB(4);
AF_DCMotor motorRB(1);

// ═══ 状态 ═══

bool wifiOk   = false;
bool mqttOk   = false;
int  speedPwm = 150;
float usCm    = 999.0;
unsigned long tTelemetry = 0;
unsigned long tHeartbeat = 0;
int  msgSeq = 0;

// 待处理的 MQTT 指令 (readAT 中检测到 +MQTT_SUB_RECV 时暂存)
bool hasPendingCmd = false;
char pendingCmd[80];

// ═══ 缓冲区 ═══

char workBuf[180];
char jsonBuf[240];

// PROGMEM 字符串读取辅助 (避免重复声明局部缓冲区)
char pBuf[32];  // 临时存放 PROGMEM 字符串, 使用后立即消费

#define PSTR2BUF(dst, src) do { strncpy_P(dst, src, sizeof(dst)-1); dst[sizeof(dst)-1]='\0'; } while(0)

// ═══ 流式 XOR+Base64 (计算长度用) ═══

static const char B64[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 流式发送 XOR+Base64 到 ESP 串口，返回发送的字节数
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
  // 返回 "XOR:" + base64 编码后的总长度
  int b64Len = 4 * ((pL + 2) / 3);
  return 4 + b64Len;
}

// 先计算 XOR+Base64 编码后的总长度 (不发送)
int xorEncodedLen(const char* plain) {
  int pL = strlen(plain);
  int b64Len = 4 * ((pL + 2) / 3);
  return 4 + b64Len;  // "XOR:" + base64
}

// ═══ ESP-01S AT+MQTT ═══

void readAT(unsigned long timeout = 2000) {
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < timeout) {
    while (espSerial.available()) {
      char c = espSerial.read();
      if (idx < (int)sizeof(workBuf) - 1) workBuf[idx++] = c;
    }
    // 收到完整响应后立即返回, 不等满超时
    if (idx > 0) {
      workBuf[idx] = '\0';
      // OK/ERROR 可能在行首或行中, ">" 是 PUBRAW 提示符
      if (strstr(workBuf, "OK") || strstr(workBuf, "ERROR") ||
          strstr(workBuf, "FAIL") || strstr(workBuf, ">")) {
        // 再等一小段时间确保数据完整 (如 +MQTTSUBRECV 可能在 OK 之后)
        unsigned long tw = millis();
        while (millis() - tw < 50) {
          while (espSerial.available()) {
            if (idx < (int)sizeof(workBuf) - 1) workBuf[idx++] = espSerial.read();
          }
        }
        workBuf[idx] = '\0';
        break;
      }
    }
  }
  workBuf[idx < (int)sizeof(workBuf) ? idx : (int)sizeof(workBuf) - 1] = '\0';

  // 检测 +MQTTSUBRECV: 并暂存指令 (避免被后续 readAT 覆盖)
  if (!hasPendingCmd) {
    char* recv = strstr(workBuf, "+MQTTSUBRECV:");
    if (recv) {
      // 如果还没收到完整 JSON, 继续等待
      if (!strchr(recv, '{')) {
        unsigned long t1 = millis();
        while (millis() - t1 < 2000 && idx < (int)sizeof(workBuf) - 2) {
          while (espSerial.available()) {
            if (idx < (int)sizeof(workBuf) - 2) workBuf[idx++] = espSerial.read();
          }
        }
        workBuf[idx] = '\0';
      }
      // 找 JSON payload
      char* jsonStart = strchr(recv, '{');
      if (jsonStart) {
        char* jsonEnd = strrchr(jsonStart, '}');
        if (jsonEnd) {
          int len = jsonEnd - jsonStart + 1;
          if (len >= (int)sizeof(pendingCmd)) len = sizeof(pendingCmd) - 1;
          memcpy(pendingCmd, jsonStart, len);
          pendingCmd[len] = '\0';
          hasPendingCmd = true;
          Serial.print(F("[MQTT-PEND] ")); Serial.println(pendingCmd);
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
  delay(2000);
  espSerial.begin(9600);
  delay(500);
  // 清空串口缓冲区 (ESP 上电可能输出乱码)
  while (espSerial.available()) espSerial.read();
  delay(500);

  // 多次尝试 AT, ESP-01S 上电可能需要几秒才就绪
  for (int i = 0; i < 5; i++) {
    sendAT("AT", 1500);
    if (atHas("OK")) {
      Serial.println(F("[ESP] 9600 OK"));
      break;
    }
    Serial.print(F("[ESP] retry ")); Serial.println(i + 1);
    delay(1000);
  }
  if (!atHas("OK")) {
    // 尝试 115200
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
  PSTR2BUF(pBuf, WIFI_SSID);
  Serial.print(F("[WiFi] ")); Serial.println(pBuf);
  // 用 jsonBuf 临时构建 AT 命令
  char ssid[32], pass[32];
  PSTR2BUF(ssid, WIFI_SSID); PSTR2BUF(pass, WIFI_PASS);
  snprintf(jsonBuf, sizeof(jsonBuf), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
  sendAT(jsonBuf, 20000);
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
  Serial.println(workBuf);
  return false;
}

bool connectMQTT() {
  if (!wifiOk) return false;
  char host[32]; PSTR2BUF(host, MQTT_HOST);
  Serial.print(F("[MQTT] ")); Serial.print(host); Serial.print(F(":")); Serial.println(MQTT_PORT);

  // 先查固件版本
  sendAT("AT+GMR", 3000);
  Serial.print(F("[MQTT] FW: ")); Serial.println(workBuf);

  // 先断开已有 MQTT 连接
  sendAT("AT+MQTTCLEAN=0", 1000);

  char devid[32]; PSTR2BUF(devid, DEVICE_ID);

  // 用 jsonBuf 临时构建 AT 命令 (此时 jsonBuf 未被遥测/心跳使用)
  snprintf(jsonBuf, sizeof(jsonBuf),
    "AT+MQTTUSERCFG=0,1,\"%s\",\"\",\"\",0,0,\"\"", devid);
  sendAT(jsonBuf, 3000);
  if (!atHas("OK")) {
    Serial.print(F("[MQTT] USERCFG fail: ")); Serial.println(workBuf);
    snprintf(jsonBuf, sizeof(jsonBuf),
      "AT+MQTTUSERCFG=0,1,\"%s\"", devid);
    sendAT(jsonBuf, 3000);
  }
  if (!atHas("OK")) {
    Serial.print(F("[MQTT] USERCFG2 fail: ")); Serial.println(workBuf);
    Serial.println(F("[MQTT] 固件可能不支持 AT+MQTT 指令!"));
    return false;
  }
  Serial.println(F("[MQTT] cfg OK"));

  snprintf(jsonBuf, sizeof(jsonBuf), "AT+MQTTCONN=0,\"%s\",%d,0", host, MQTT_PORT);
  sendAT(jsonBuf, 10000);
  if (!atHas("OK")) { Serial.println(F("[MQTT] conn fail")); Serial.println(workBuf); return false; }

  mqttOk = true;
  Serial.println(F("[MQTT] OK"));

  snprintf(jsonBuf, sizeof(jsonBuf), "AT+MQTTSUB=0,\"cmd/%s\",1", devid);
  sendAT(jsonBuf, 3000);
  if (atHas("OK")) Serial.println(F("[MQTT] sub OK"));
  else { Serial.print(F("[MQTT] sub fail: ")); Serial.println(workBuf); }

  return true;
}

bool mqttPub(const char* topic, const char* payload) {
  if (!mqttOk) return false;
  espSerial.print(F("AT+MQTTPUB=0,\""));
  espSerial.print(topic);
  espSerial.print(F("\",\""));
  espSerial.print(payload);
  espSerial.println(F("\",0,0"));
  readAT(3000);
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

  // 步骤2: 等待 ">" 提示符 (缩短超时)
  readAT(2000);
  if (!atHas(">")) {
    Serial.print(F("[MQTT] PUBRAW no >: ")); Serial.println(workBuf);
    return false;
  }

  // 步骤3: 发送 XOR 加密数据
  streamXor(jsonPlain, keyP);

  // 步骤4: 等待发送结果 (缩短超时)
  readAT(3000);
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
    Serial.print(F("[MQTT-RECV] pending: ")); Serial.println(workBuf);
    return true;
  }

  // 直接从串口读取
  if (!espSerial.available()) return false;

  // 读取所有可用数据
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 300 && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1)
      workBuf[idx++] = espSerial.read();
  }
  workBuf[idx] = '\0';

  // 查找 +MQTTSUBRECV:
  char* recv = strstr(workBuf, "+MQTTSUBRECV:");
  if (!recv) return false;

  // 等待完整 JSON (可能还没到)
  if (!strchr(recv, '{')) {
    unsigned long t1 = millis();
    while (millis() - t1 < 1500 && idx < (int)sizeof(workBuf) - 2) {
      while (espSerial.available() && idx < (int)sizeof(workBuf) - 2)
        workBuf[idx++] = espSerial.read();
    }
    workBuf[idx] = '\0';
  }

  // 直接找 { 和 } 提取 JSON
  char* jsonStart = strchr(recv, '{');
  if (!jsonStart) return false;
  char* jsonEnd = strrchr(jsonStart, '}');
  if (!jsonEnd) return false;

  int len = jsonEnd - jsonStart + 1;
  memmove(workBuf, jsonStart, len);
  workBuf[len] = '\0';

  Serial.print(F("[MQTT-PAYLOAD] ")); Serial.println(workBuf);
  return true;
}

// ═══ 传感器 ═══

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

// ═══ 电机 ═══

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

// ═══ 指令解析 ═══

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
  else { Serial.print(F("[CMD] unknown: ")); Serial.println(cmd); }
}

// ═══ 遥测上报 ═══

void sendTelemetry() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  usCm = readUltrasonic();

  char fBuf[12];  // dtostrf 临时缓冲 (局部变量, 用完释放)
  char devid[32]; PSTR2BUF(devid, DEVICE_ID);

  int n = 0;
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "{\"device_id\":\"%s\",", devid);
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
    "\"speed_pwm\":%d,", speedPwm);

  // 签名
  uint8_t sig[5] = {0};
  int sLen = strlen_P(DEVICE_SECRET);
  int jLen = strlen(jsonBuf);
  for (int i = 0; i < jLen && i < 250; i++)
    sig[i % 4] ^= (uint8_t)jsonBuf[i] ^ pgm_read_byte(&DEVICE_SECRET[i % sLen]);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"signature\":\"%02X%02X%02X%02X\"}", sig[0], sig[1], sig[2], sig[3]);

  // 用 PUBRAW 发送 (payload 中的特殊字符不会破坏 AT 指令)
  char topic[64];
  snprintf(topic, sizeof(topic), "sensor/%s", devid);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.print(F("[TEL] OK seq=")); Serial.println(msgSeq++);
  } else {
    Serial.println(F("[TEL] fail"));
  }
}

// ═══ 心跳 ═══

void sendHeartbeat() {
  char devid[32]; PSTR2BUF(devid, DEVICE_ID);
  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":%s}",
    devid, millis() / 1000,
    wifiOk ? "true" : "false", mqttOk ? "true" : "false");

  // 心跳也用 PUBRAW 发送 XOR 加密数据
  char topic[64];
  snprintf(topic, sizeof(topic), "heartbeat/%s", devid);
  if (mqttPubRaw(topic, jsonBuf, XOR_KEY)) {
    Serial.println(F("[HB] OK"));
  } else {
    Serial.println(F("[HB] fail"));
  }
}

// ═══ 内存 ═══

int freeRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══ 主程序 ═══

void setup() {
  Serial.begin(9600);
  delay(500);

  Serial.println(F("========================================"));
  Serial.println(F(" SmartRover v3.2-test (DHT+SR04+WiFi)"));
  Serial.println(F("========================================"));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  dht.begin();
  Serial.println(F("[INIT] DHT11+HC-SR04 OK"));

  stopMotors();
  Serial.println(F("[INIT] L293D OK"));

  Serial.println(F("[NET] connecting..."));
  if (connectWiFi()) connectMQTT();
  else Serial.println(F("[NET] WiFi fail, offline"));

  Serial.print(F("[MEM] RAM: ")); Serial.print(freeRAM()); Serial.println(F("B"));
  Serial.println(F("Ready!"));
}

void loop() {
  unsigned long now = millis();

  // 优先处理指令 (每次 loop 都检查)
  handleCommand();

  if (now - tTelemetry > TELEMETRY_MS) { sendTelemetry(); tTelemetry = now; }
  // 遥测发完后立即再检查指令 (可能在发送期间到达)
  handleCommand();

  if (now - tHeartbeat > HEARTBEAT_MS) { sendHeartbeat(); tHeartbeat = now; }
  handleCommand();

  // 每30秒检查一次 MQTT 连接和订阅状态
  static unsigned long tReconn = 0;
  if (now - tReconn > 30000) {
    if (!wifiOk) { Serial.println(F("[NET] reWiFi")); connectWiFi(); }
    else if (!mqttOk) { Serial.println(F("[NET] reMQTT")); connectMQTT(); }
    else {
      // 检查 MQTT 连接是否还活着
      sendAT("AT+MQTTCONN?", 2000);
      if (!atHas("CONNECTED") && !atHas("+MQTTCONN:0,")) {
        Serial.println(F("[NET] MQTT lost, reconnecting..."));
        mqttOk = false;
      }
    }
    tReconn = now;
  }

  // 空闲时非阻塞检查 ESP 串口是否有订阅消息
  if (espSerial.available() && !hasPendingCmd) {
    int idx = 0;
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1) {
      workBuf[idx++] = espSerial.read();
    }
    workBuf[idx] = '\0';

    if (strstr(workBuf, "+MQTTSUBRECV:")) {
      // 等待完整 JSON
      unsigned long t1 = millis();
      while (millis() - t1 < 1500 && idx < (int)sizeof(workBuf) - 2) {
        while (espSerial.available() && idx < (int)sizeof(workBuf) - 2) {
          workBuf[idx++] = espSerial.read();
        }
      }
      workBuf[idx] = '\0';

      char* jsonStart = strchr(workBuf, '{');
      if (jsonStart) {
        char* jsonEnd = strrchr(jsonStart, '}');
        if (jsonEnd) {
          int len = jsonEnd - jsonStart + 1;
          if (len >= (int)sizeof(pendingCmd)) len = sizeof(pendingCmd) - 1;
          memcpy(pendingCmd, jsonStart, len);
          pendingCmd[len] = '\0';
          hasPendingCmd = true;
          Serial.print(F("[MQTT-PEND] ")); Serial.println(pendingCmd);
        }
      }
    }
  }

  delay(30);
}

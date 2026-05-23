/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO-B (通信板) — 双UNO架构
 * ═══════════════════════════════════════════════════════════════
 *  负责: ESP-01S WiFi + MQTT + XOR 加密
 *  通过 SoftwareSerial 与 UNO-A 通信
 *
 *  引脚:
 *    D2/D7    SoftwareSerial ↔ UNO-A
 *    A2/A3    ESP-01S (RX/TX, 注意电阻分压!)
 * ═══════════════════════════════════════════════════════════════
 */

#include <SoftwareSerial.h>

// ═══ 配置 (PROGMEM 节省 RAM) ═══
const char WIFI_SSID[] PROGMEM = "REDMI K80 Ultra";
const char WIFI_PASS[] PROGMEM = "88888888";
const char MQTT_HOST[] PROGMEM = "10.32.44.189";
const char DEVICE_ID[] PROGMEM = "10.32.44.189";
const char DEVICE_SECRET[] PROGMEM = "SmartRover2024Secret";
const char XOR_KEY[] PROGMEM = "SR2024XOR";

#define MQTT_PORT 1883

// ═══ 串口 ═══
SoftwareSerial commSerial(2, 7);   // ↔ UNO-A
SoftwareSerial espSerial(A2, A3);  // ESP-01S

// ═══ 状态 ═══
bool wifiOk = false, mqttOk = false;
unsigned long msgSeq = 0;
unsigned long tTelemetry = 0, tHeartbeat = 0;

// ═══ 缓冲区 ═══
char workBuf[300];   // AT 响应缓冲 (通信板内存充裕)
char jsonBuf[300];   // JSON 构建
char pBuf[32];       // PROGMEM 读取临时

#define PSTR2BUF(dst, src) do { strncpy_P(dst, src, sizeof(dst)-1); dst[sizeof(dst)-1]='\0'; } while(0)

// ═══════════════════════════════════════════════════════════════
//  AT 指令
// ═══════════════════════════════════════════════════════════════

void sendAT(const char* cmd, unsigned long timeout) {
  espSerial.listen();
  while (espSerial.available()) espSerial.read();  // 清空
  espSerial.println(cmd);
  unsigned long t0 = millis();
  int idx = 0;
  while (millis() - t0 < timeout && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1) {
      workBuf[idx++] = espSerial.read();
    }
  }
  workBuf[idx] = '\0';
}

bool atHas(const char* s) { return strstr(workBuf, s) != NULL; }

bool initESP() {
  espSerial.listen();
  for (int i = 0; i < 3; i++) {
    sendAT("AT", 2000);
    if (atHas("OK")) {
      Serial.println(F("[ESP] 9600 OK"));
      sendAT("ATE0", 1000);
      sendAT("AT+CWMODE=1", 2000);
      unsigned long t0 = millis();
      while (millis() - t0 < 3000) { if (espSerial.available()) espSerial.read(); }
      Serial.println(F("[ESP] ready"));
      return true;
    }
    delay(1000);
  }
  Serial.println(F("[ESP] no resp"));
  return false;
}

bool connectWiFi() {
  if (!initESP()) return false;
  PSTR2BUF(pBuf, WIFI_SSID);
  Serial.print(F("[WiFi] ")); Serial.println(pBuf);
  char ssid[32], pass[32];
  PSTR2BUF(ssid, WIFI_SSID); PSTR2BUF(pass, WIFI_PASS);
  snprintf(jsonBuf, sizeof(jsonBuf), "AT+CWJAP=\"%s\",\"%s\"", ssid, pass);
  sendAT(jsonBuf, 20000);
  if (atHas("OK") || atHas("GOT IP")) {
    wifiOk = true;
    Serial.println(F("[WiFi] OK"));
    sendAT("AT+CIFSR", 3000);
    return true;
  }
  Serial.println(F("[WiFi] fail"));
  return false;
}

bool connectMQTT() {
  if (!wifiOk) return false;
  char host[32]; PSTR2BUF(host, MQTT_HOST);
  Serial.print(F("[MQTT] ")); Serial.print(host); Serial.print(F(":"))); Serial.println(MQTT_PORT);

  sendAT("AT+GMR", 3000);
  Serial.print(F("[MQTT] FW: ")); Serial.println(workBuf);

  sendAT("AT+MQTTCLEAN=0", 1000);

  char devid[32]; PSTR2BUF(devid, DEVICE_ID);

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

// ═══════════════════════════════════════════════════════════════
//  流式 XOR + Base64 发送 (PUBRAW)
// ═══════════════════════════════════════════════════════════════

static const char B64[] PROGMEM =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int b64EncLen(int n) { return ((n + 2) / 3) * 4; }

bool mqttPubRaw(const char* topic, const char* payload, const char* xorKey) {
  if (!mqttOk) return false;
  int pLen = strlen(payload);
  int kLen = strlen(xorKey);
  int eLen = b64EncLen(pLen);

  // 发送命令头
  espSerial.listen();
  espSerial.print(F("AT+MQTTPUBRAW=0,\""));
  espSerial.print(topic);
  espSerial.print(F("\","));
  espSerial.print(eLen);
  espSerial.println(F(",0,0"));

  // 等待 > 提示
  unsigned long t0 = millis();
  bool gotPrompt = false;
  while (millis() - t0 < 3000) {
    while (espSerial.available()) {
      if (espSerial.read() == '>') { gotPrompt = true; break; }
    }
    if (gotPrompt) break;
  }
  if (!gotPrompt) { Serial.println(F("[PUB] no >")); return false; }

  // 流式 XOR+Base64 编码发送
  for (int i = 0; i < pLen; i += 3) {
    uint8_t a = (uint8_t)payload[i] ^ xorKey[i % kLen];
    uint8_t b = (i + 1 < pLen) ? ((uint8_t)payload[i + 1] ^ xorKey[(i + 1) % kLen]) : 0;
    uint8_t c = (i + 2 < pLen) ? ((uint8_t)payload[i + 2] ^ xorKey[(i + 2) % kLen]) : 0;
    int pad = (i + 1 >= pLen) ? 2 : ((i + 2 >= pLen) ? 1 : 0);

    espSerial.write(pgm_read_byte(&B64[(a >> 2) & 0x3F]));
    espSerial.write(pgm_read_byte(&B64[((a << 4 | b >> 4) & 0x3F)]));
    espSerial.write(pad >= 2 ? '=' : pgm_read_byte(&B64[((b << 2 | c >> 6) & 0x3F)]));
    espSerial.write(pad >= 1 ? '=' : pgm_read_byte(&B64[c & 0x3F]));
  }

  // 等待 OK
  t0 = millis();
  int idx = 0;
  while (millis() - t0 < 5000 && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1) {
      workBuf[idx++] = espSerial.read();
    }
  }
  workBuf[idx] = '\0';
  return atHas("OK") || atHas("PUB");
}

// ═══════════════════════════════════════════════════════════════
//  MQTT 消息接收 (指令下发)
// ═══════════════════════════════════════════════════════════════

bool checkMQTTMsg() {
  espSerial.listen();
  if (!espSerial.available()) return false;

  // 读入 workBuf
  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 500 && idx < (int)sizeof(workBuf) - 1) {
    while (espSerial.available() && idx < (int)sizeof(workBuf) - 1) {
      workBuf[idx++] = espSerial.read();
    }
  }
  workBuf[idx] = '\0';

  // 检查 +MQTT_SUB_RECV
  char* p = strstr(workBuf, "+MQTT_SUB_RECV:");
  if (!p) return false;

  // 提取 payload (第3个逗号后的内容)
  int comma = 0;
  p += 15;  // 跳过 "+MQTT_SUB_RECV:"
  while (*p && comma < 2) { if (*p == ',') comma++; p++; }
  if (!*p) return false;

  // p 现在指向 payload 长度, 跳过长度到数据
  char* dataStart = strchr(p, '\n');
  if (!dataStart) return false;
  dataStart++;

  // XOR 解密 (Base64 解码 + XOR)
  int kLen = strlen_P(XOR_KEY);
  char xk[16]; PSTR2BUF(xk, XOR_KEY);

  // 简易 Base64 解码 + XOR 解密
  static const uint8_t d64[256] = {
    62,255,255,255,63,52,53,54,55,56,57,58,59,60,61,255,
    255,255,255,255,255,255,0,1,2,3,4,5,6,7,8,9,
    10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
    255,255,255,255,255,255,26,27,28,29,30,31,32,33,34,35,
    36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51
  };

  int dLen = strlen(dataStart);
  char decoded[120];
  int dIdx = 0;
  for (int i = 0; i + 3 < dLen && dIdx < (int)sizeof(decoded) - 1; i += 4) {
    uint8_t a = (dataStart[i] == '=' || dataStart[i] < '+') ? 0 : d64[dataStart[i] - '+'];
    uint8_t b = (dataStart[i+1] == '=' || dataStart[i+1] < '+') ? 0 : d64[dataStart[i+1] - '+'];
    uint8_t c_val = (dataStart[i+2] == '=' || dataStart[i+2] < '+') ? 0 : d64[dataStart[i+2] - '+'];
    uint8_t d = (dataStart[i+3] == '=' || dataStart[i+3] < '+') ? 0 : d64[dataStart[i+3] - '+'];
    if (a > 63 || b > 63) continue;
    decoded[dIdx++] = ((a << 2 | b >> 4) & 0xFF) ^ xk[(dIdx) % kLen];
    if (dataStart[i+2] != '=' && c_val <= 63)
      decoded[dIdx++] = ((b << 4 | c_val >> 2) & 0xFF) ^ xk[(dIdx) % kLen];
    if (dataStart[i+3] != '=' && d <= 63)
      decoded[dIdx++] = ((c_val << 6 | d) & 0xFF) ^ xk[(dIdx) % kLen];
  }
  decoded[dIdx] = '\0';

  // 从解密后的 JSON 提取 command
  // 简易提取: 找 "command":"xxx" 或 "cmd":"xxx"
  char* cmdKey = strstr(decoded, "\"command\"");
  if (!cmdKey) cmdKey = strstr(decoded, "\"cmd\"");
  if (!cmdKey) return false;

  char* colon = strchr(cmdKey, ':');
  if (!colon) return false;
  colon++;
  while (*colon == ' ' || *colon == '"') colon++;
  char* end = colon;
  while (*end && *end != '"' && *end != ',' && *end != '}') end++;
  int cLen = end - colon;
  if (cLen <= 0 || cLen > 20) return false;

  // 提取 speed_pwm
  int spd = 150;
  char* spdKey = strstr(decoded, "\"speed_pwm\"");
  if (spdKey) {
    char* sc = strchr(spdKey, ':');
    if (sc) { int v = atoi(sc + 1); if (v > 0 && v <= 255) spd = v; }
  }

  // 转发给 UNO-A
  char fwd[40];
  snprintf(fwd, sizeof(fwd), "$C,%.*s,%d", cLen, colon, spd);
  commSerial.listen();
  commSerial.println(fwd);
  Serial.print(F("[CMD→A] ")); Serial.println(fwd);

  return true;
}

// ═══════════════════════════════════════════════════════════════
//  UNO-A 通信
// ═══════════════════════════════════════════════════════════════

char recvBuf[200];

bool readFromA() {
  commSerial.listen();
  if (!commSerial.available()) return false;

  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 200 && idx < (int)sizeof(recvBuf) - 1) {
    while (commSerial.available() && idx < (int)sizeof(recvBuf) - 1) {
      char c = commSerial.read();
      if (c == '\n' || c == '\r') { if (idx > 0) goto done; continue; }
      recvBuf[idx++] = c;
    }
  }
done:
  recvBuf[idx] = '\0';
  return idx > 0;
}

// 从 $D,T:26.5,H:65,U:120,IL:0,IR:0,HD:180,LA:xx,LN:xx,AL:xx,SP:xx,SA:8
// 提取字段值
char* findField(const char* tag) {
  static char valBuf[16];
  char search[4];
  search[0] = ','; search[1] = tag[0]; search[2] = tag[1]; search[3] = '\0';
  char* p = strstr(recvBuf, search);
  if (!p) return NULL;
  p += 3;  // 跳过 ,XX:
  if (*p == ',' || *p == '\0') return NULL;  // 空值
  int i = 0;
  while (*p && *p != ',' && i < 15) valBuf[i++] = *p++;
  valBuf[i] = '\0';
  return valBuf;
}

void buildAndSendTelemetry() {
  char devid[32]; PSTR2BUF(devid, DEVICE_ID);

  int n = 0;
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "{\"device_id\":\"%s\",", devid);

  // GPS
  char* la = findField("LA");
  char* ln = findField("LN");
  if (la && ln) {
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"latitude\":%s,\"longitude\":%s,", la, ln);
  } else {
    n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
      "\"latitude\":null,\"longitude\":null,");
  }

  // 温度
  char* t = findField("T:");
  if (t && t[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":%s,", t);
  else n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"temperature\":null,");

  // 湿度
  char* h = findField("H:");
  if (h && h[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":%s,", h);
  else n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"humidity\":null,");

  // 超声波
  char* u = findField("U:");
  if (u && u[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"ultrasonic_cm\":%s,", u);
  else n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"ultrasonic_cm\":null,");

  // 红外
  char* il = findField("IL");
  char* ir = findField("IR");
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"ir_obstacle\":\"%s,%s\",", il ? il : "0", ir ? ir : "0");

  // IMU
  char* hd = findField("HD");
  if (hd && hd[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_heading\":%s,", hd);
  else n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"imu_heading\":null,");

  // GPS 附加
  char* al = findField("AL");
  char* sp = findField("SP");
  char* sa = findField("SA");
  if (al && al[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"altitude\":%s,", al);
  if (sp && sp[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"speed_kmh\":%s,", sp);
  if (sa && sa[0]) n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"satellites\":%s,", sa);

  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n, "\"speed_pwm\":0,");

  // 签名
  uint8_t sig[5] = {0};
  int sLen = strlen_P(DEVICE_SECRET);
  int jLen = strlen(jsonBuf);
  for (int i = 0; i < jLen && i < 280; i++)
    sig[i % 4] ^= (uint8_t)jsonBuf[i] ^ pgm_read_byte(&DEVICE_SECRET[i % sLen]);
  n += snprintf(jsonBuf + n, sizeof(jsonBuf) - n,
    "\"signature\":\"%02X%02X%02X%02X\"}", sig[0], sig[1], sig[2], sig[3]);

  char topic[64];
  snprintf(topic, sizeof(topic), "sensor/%s", devid);
  char xk[16]; PSTR2BUF(xk, XOR_KEY);
  if (mqttPubRaw(topic, jsonBuf, xk)) {
    Serial.print(F("[TEL] OK seq=")); Serial.println(msgSeq++);
  } else {
    Serial.println(F("[TEL] fail"));
  }
}

void buildAndSendHeartbeat() {
  char devid[32]; PSTR2BUF(devid, DEVICE_ID);

  // 从 recvBuf 提取 uptime (如果有 $H,xxx)
  unsigned long uptime = millis() / 1000;

  snprintf(jsonBuf, sizeof(jsonBuf),
    "{\"device_id\":\"%s\",\"uptime_s\":%lu,\"wifi\":%s,\"mqtt\":%s}",
    devid, uptime,
    wifiOk ? "true" : "false", mqttOk ? "true" : "false");

  char topic[64];
  snprintf(topic, sizeof(topic), "heartbeat/%s", devid);
  char xk[16]; PSTR2BUF(xk, XOR_KEY);
  if (mqttPubRaw(topic, jsonBuf, xk)) {
    Serial.println(F("[HB] OK"));
  } else {
    Serial.println(F("[HB] fail"));
  }
}

// ═══════════════════════════════════════════════════════════════
//  内存
// ═══════════════════════════════════════════════════════════════

int freeRAM() {
  extern int __heap_start, *__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
}

// ═══════════════════════════════════════════════════════════════
//  主程序
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  commSerial.begin(9600);
  espSerial.begin(9600);
  delay(500);

  Serial.println(F("SmartRover UNO-B v1.0"));
  Serial.println(F("======================"));

  Serial.println(F("[NET] connecting..."));
  if (connectWiFi()) connectMQTT();
  else Serial.println(F("[NET] WiFi fail, offline"));

  Serial.print(F("[MEM] RAM: ")); Serial.print(freeRAM()); Serial.println(F("B"));
  Serial.println(F("Ready!"));
}

void loop() {
  unsigned long now = millis();

  // 检查 MQTT 下发指令
  checkMQTTMsg();

  // 读 UNO-A 数据
  if (readFromA()) {
    if (strncmp(recvBuf, "$D,", 3) == 0) {
      // 传感器数据 → MQTT
      buildAndSendTelemetry();
    } else if (strncmp(recvBuf, "$H,", 3) == 0) {
      // 心跳 → MQTT
      buildAndSendHeartbeat();
    }
    Serial.print(F("[RX←A] ")); Serial.println(recvBuf);
  }

  // 定时心跳 (如果 UNO-A 没发来)
  if (now - tHeartbeat > 10000) {
    buildAndSendHeartbeat();
    tHeartbeat = now;
  }

  // 重连
  static unsigned long tReconn = 0;
  if (now - tReconn > 30000) {
    if (!wifiOk) { Serial.println(F("[NET] reWiFi")); connectWiFi(); }
    else if (!mqttOk) { Serial.println(F("[NET] reMQTT")); connectMQTT(); }
    tReconn = now;
  }

  delay(50);
}

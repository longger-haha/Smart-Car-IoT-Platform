/**
 * ═══════════════════════════════════════════════════════════════
 *  SmartRover UNO-A (传感器板) — 双UNO架构
 * ═══════════════════════════════════════════════════════════════
 *  负责: DHT11 + HC-SR04 + MPU6050 + GPS + 红外 + 电机
 *  通过 SoftwareSerial 与 UNO-B 通信
 *
 *  引脚:
 *    D2/D7    SoftwareSerial ↔ UNO-B
 *    D9/D10   MPU6050 SDA/SCL (手动I2C)
 *    D13      DHT11
 *    A0/A1    HC-SR04 TRIG/ECHO
 *    A2/A3    GPS SoftwareSerial RX/TX
 *    A4       右红外
 *    A5       左红外
 *    AFMotor: D3,D4,D5,D6,D8,D11,D12
 * ═══════════════════════════════════════════════════════════════
 */

#include <AFMotor_R4.h>
#include <SoftwareSerial.h>
#include <DHT.h>

// ═══ 引脚 ═══
#define DHT_PIN   13
#define TRIG_PIN  A0
#define ECHO_PIN  A1
#define IR_L_PIN  A5
#define IR_R_PIN  A4
#define SDA_PIN   9
#define SCL_PIN   10
#define COMM_RX   2
#define COMM_TX   7
#define GPS_RX    A2
#define GPS_TX    A3

// ═══ 对象 ═══
AF_DCMotor motorLF(4);
AF_DCMotor motorRF(3);
AF_DCMotor motorLB(2);
AF_DCMotor motorRB(1);
DHT dht(DHT_PIN, DHT11);
SoftwareSerial commSerial(COMM_RX, COMM_TX);  // ↔ UNO-B
SoftwareSerial gpsSerial(GPS_RX, GPS_TX);     // GPS模块

// ═══ 状态 ═══
bool irL = false, irR = false;
float usCm = 999.0;
float imuHeading = 0.0, gyroZOff = 0.0;
float gpsLat = 0, gpsLng = 0, gpsAlt = 0, gpsSpd = 0;
int  gpsSats = 0;
bool gpsFix = false;
int  speedPwm = 150;

// ═══ 缓冲区 ═══
char dataBuf[180];  // 发给 UNO-B 的数据
char cmdBuf[40];    // 收到 UNO-B 的命令

// ═══ GPS NMEA ═══
char nmeaBuf[80];
int  nmeaIdx = 0;

// ═══ 定时 ═══
unsigned long tSensor = 0;
unsigned long tHeartbeat = 0;
unsigned long tImu = 0;

// ═══════════════════════════════════════════════════════════════
//  MPU6050 手动 I2C (D9/D10)
// ═══════════════════════════════════════════════════════════════

void i2cStart() {
  digitalWrite(SDA_PIN, HIGH); digitalWrite(SCL_PIN, HIGH);
  delayMicroseconds(5);
  digitalWrite(SDA_PIN, LOW); delayMicroseconds(5);
  digitalWrite(SCL_PIN, LOW);
}

void i2cStop() {
  digitalWrite(SCL_PIN, LOW); digitalWrite(SDA_PIN, LOW);
  delayMicroseconds(5);
  digitalWrite(SCL_PIN, HIGH); delayMicroseconds(5);
  digitalWrite(SDA_PIN, HIGH);
}

void i2cWrite(uint8_t b) {
  for (int i = 7; i >= 0; i--) {
    digitalWrite(SDA_PIN, (b >> i) & 1);
    digitalWrite(SCL_PIN, HIGH); delayMicroseconds(3);
    digitalWrite(SCL_PIN, LOW);  delayMicroseconds(3);
  }
  digitalWrite(SDA_PIN, HIGH);
  digitalWrite(SCL_PIN, HIGH); delayMicroseconds(3);
  digitalWrite(SCL_PIN, LOW);
}

uint8_t i2cRead(bool ack) {
  uint8_t b = 0;
  digitalWrite(SDA_PIN, HIGH);
  for (int i = 7; i >= 0; i--) {
    digitalWrite(SCL_PIN, HIGH); delayMicroseconds(3);
    if (digitalRead(SDA_PIN)) b |= (1 << i);
    digitalWrite(SCL_PIN, LOW);  delayMicroseconds(3);
  }
  digitalWrite(SDA_PIN, ack ? LOW : HIGH);
  digitalWrite(SCL_PIN, HIGH); delayMicroseconds(3);
  digitalWrite(SCL_PIN, LOW);
  return b;
}

void mpuWrite(uint8_t reg, uint8_t val) {
  i2cStart();
  i2cWrite(0x68 << 1); i2cWrite(reg); i2cWrite(val);
  i2cStop();
}

uint8_t mpuRead(uint8_t reg) {
  i2cStart();
  i2cWrite(0x68 << 1); i2cWrite(reg);
  i2cStart();
  i2cWrite(0x68 << 1 | 1);
  uint8_t v = i2cRead(false);
  i2cStop();
  return v;
}

void initMPU6050() {
  pinMode(SDA_PIN, OUTPUT); pinMode(SCL_PIN, OUTPUT);
  mpuWrite(0x6B, 0);  // 唤醒
  delay(100);
  // 校准 gyroZ
  long sum = 0;
  for (int i = 0; i < 200; i++) {
    uint8_t h = mpuRead(0x47), l = mpuRead(0x48);
    sum += (int16_t)((h << 8) | l);
    delay(5);
  }
  gyroZOff = sum / 200.0;
}

void updateIMU() {
  uint8_t h = mpuRead(0x47), l = mpuRead(0x48);
  float gz = (int16_t)((h << 8) | l) - gyroZOff;
  float dt = (millis() - tImu) / 1000.0;
  if (dt > 0.01 && dt < 1.0) imuHeading += gz * dt / 131.0;
  tImu = millis();
}

// ═══════════════════════════════════════════════════════════════
//  GPS (极简 NMEA 解析)
// ═══════════════════════════════════════════════════════════════

float nmeaFloat(int field) {
  char* p = nmeaBuf;
  int f = 0;
  while (*p && f < field) { if (*p == ',') f++; p++; }
  return p ? atof(p) : 0.0;
}

char nmeaChar(int field) {
  char* p = nmeaBuf;
  int f = 0;
  while (*p && f < field) { if (*p == ',') f++; p++; }
  return p ? *p : '\0';
}

void parseNMEA() {
  if (strncmp(nmeaBuf, "$GPRMC", 6) == 0 || strncmp(nmeaBuf, "$GNRMC", 6) == 0) {
    if (nmeaChar(2) == 'A') {
      float rawLat = nmeaFloat(3);
      float rawLng = nmeaFloat(5);
      gpsLat = (int)(rawLat / 100) + fmod(rawLat, 100) / 60.0;
      if (nmeaChar(4) == 'S') gpsLat = -gpsLat;
      gpsLng = (int)(rawLng / 100) + fmod(rawLng, 100) / 60.0;
      if (nmeaChar(6) == 'W') gpsLng = -gpsLng;
      gpsSpd = nmeaFloat(7) * 1.852;
      gpsFix = true;
    } else {
      gpsFix = false;
    }
  }
  else if (strncmp(nmeaBuf, "$GPGGA", 6) == 0 || strncmp(nmeaBuf, "$GNGGA", 6) == 0) {
    if (nmeaFloat(2) != 0) {
      gpsAlt = nmeaFloat(9);
      gpsSats = (int)nmeaFloat(7);
    }
  }
}

void readGPS() {
  gpsSerial.listen();
  unsigned long t0 = millis();
  while (millis() - t0 < 50) {  // 读50ms
    while (gpsSerial.available()) {
      char c = gpsSerial.read();
      if (c == '$') { nmeaIdx = 0; nmeaBuf[0] = '\0'; }
      else if (c == '\r' || c == '\n') {
        if (nmeaIdx > 6) { nmeaBuf[nmeaIdx] = '\0'; parseNMEA(); }
        nmeaIdx = 0;
      }
      else if (nmeaIdx < (int)sizeof(nmeaBuf) - 1) {
        nmeaBuf[nmeaIdx++] = c;
      }
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  传感器
// ═══════════════════════════════════════════════════════════════

float readUltrasonic() {
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
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

// ═══════════════════════════════════════════════════════════════
//  UNO-B 通信
// ═══════════════════════════════════════════════════════════════

void sendSensorData() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  usCm = readUltrasonic();
  readIR();
  updateIMU();

  char fBuf[12];
  int n = 0;
  n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "$D,");

  // 温度
  if (!isnan(temp)) { dtostrf(temp, 1, 1, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "T:%s,", fBuf); }
  else n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "T:,");

  // 湿度
  if (!isnan(humi)) { dtostrf(humi, 1, 1, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "H:%s,", fBuf); }
  else n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "H:,");

  // 超声波
  dtostrf(usCm, 1, 1, fBuf);
  n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "U:%s,", fBuf);

  // 红外
  n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "IL:%d,IR:%d,", irL ? 1 : 0, irR ? 1 : 0);

  // IMU
  dtostrf(imuHeading, 1, 1, fBuf);
  n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "HD:%s,", fBuf);

  // GPS
  if (gpsFix) {
    dtostrf(gpsLat, 1, 4, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "LA:%s,", fBuf);
    dtostrf(gpsLng, 1, 4, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "LN:%s,", fBuf);
    dtostrf(gpsAlt, 1, 1, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "AL:%s,", fBuf);
    dtostrf(gpsSpd, 1, 1, fBuf); n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "SP:%s,", fBuf);
    n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "SA:%d", gpsSats);
  } else {
    n += snprintf(dataBuf + n, sizeof(dataBuf) - n, "LA:,LN:,AL:,SP:,SA:");
  }

  commSerial.listen();
  commSerial.println(dataBuf);
  Serial.print(F("[TX] ")); Serial.println(dataBuf);
}

void sendHeartbeat() {
  snprintf(dataBuf, sizeof(dataBuf), "$H,%lu", millis() / 1000);
  commSerial.listen();
  commSerial.println(dataBuf);
  Serial.print(F("[TX] ")); Serial.println(dataBuf);
}

void checkCommand() {
  commSerial.listen();
  if (!commSerial.available()) return;

  int idx = 0;
  unsigned long t0 = millis();
  while (millis() - t0 < 100 && idx < (int)sizeof(cmdBuf) - 1) {
    while (commSerial.available() && idx < (int)sizeof(cmdBuf) - 1) {
      char c = commSerial.read();
      if (c == '\n' || c == '\r') { if (idx > 0) break; continue; }
      cmdBuf[idx++] = c;
    }
  }
  cmdBuf[idx] = '\0';

  // 解析 $C,command,speed
  if (strncmp(cmdBuf, "$C,", 3) != 0) return;
  char* cmd = cmdBuf + 3;
  char* comma = strchr(cmd, ',');
  int spd = speedPwm;
  if (comma) { *comma = '\0'; spd = atoi(comma + 1); if (spd <= 0 || spd > 255) spd = speedPwm; }

  Serial.print(F("[CMD] ")); Serial.print(cmd); Serial.print(F(" spd=")); Serial.println(spd);

  if (strcmp(cmd, "forward") == 0)       { speedPwm = spd; fwd(spd); }
  else if (strcmp(cmd, "backward") == 0)  { speedPwm = spd; bwd(spd); }
  else if (strcmp(cmd, "left") == 0)      { speedPwm = spd; rotL(spd); }
  else if (strcmp(cmd, "right") == 0)     { speedPwm = spd; rotR(spd); }
  else if (strcmp(cmd, "stop") == 0)      { stopMotors(); }
}

// ═══════════════════════════════════════════════════════════════
//  主程序
// ═══════════════════════════════════════════════════════════════

void setup() {
  Serial.begin(9600);
  commSerial.begin(9600);
  gpsSerial.begin(9600);
  delay(500);

  Serial.println(F("SmartRover UNO-A v1.0"));
  Serial.println(F("======================"));

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(IR_L_PIN, INPUT_PULLUP);
  pinMode(IR_R_PIN, INPUT_PULLUP);

  dht.begin();
  Serial.println(F("[INIT] DHT11 OK"));

  initMPU6050();
  Serial.println(F("[INIT] MPU6050 OK"));

  stopMotors();
  Serial.println(F("[INIT] L293D OK"));

  tImu = millis();
  Serial.println(F("Ready!"));
}

void loop() {
  unsigned long now = millis();

  readGPS();       // 读 GPS (切换到 gpsSerial, 50ms)
  checkCommand();  // 检查 UNO-B 命令 (切换到 commSerial)

  if (now - tSensor > 5000) {
    sendSensorData();
    tSensor = now;
  }
  if (now - tHeartbeat > 8000) {
    sendHeartbeat();
    tHeartbeat = now;
  }

  delay(50);
}

/**
 * ═══════════════════════════════════════════════════════════════
 *  测试5: GPS NEO-6M 定位模块
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 接收GPS NMEA数据, 解析并显示位置/时间/卫星信息
 *  接线:
 *    GPS VCC   → Arduino 5V
 *    GPS GND   → Arduino GND
 *    GPS TX    → Arduino A0 (软件串口RX)
 *    GPS RX    → Arduino A1 (软件串口TX)
 *
 *  使用方法:
 *    1. 需要安装 TinyGPS++ 库
 *       工具 → 管理库 → 搜索 "TinyGPS++" → 安装
 *    2. 上传到 Arduino Uno
 *    3. 打开串口监视器 (波特率 115200)
 *    4. **将GPS模块放到窗边或室外!** (室内可能收不到信号)
 *    5. 等待定位 (首次可能需要1~15分钟)
 *
 *  预期结果:
 *    刚开始: "搜索卫星中... 0颗"
 *    几分钟后: 卫星数逐渐增加
 *    定位成功: 显示经纬度、速度、时间等完整信息
 *
 *  ⚠️ 重要提示:
 *    - GPS在室内几乎无法定位!
 *    - 必须能看到天空(窗外或室外)
 *    - 首次定位可能需要较长时间(冷启动)
 *    - 天线面朝上放置
 */

#include <SoftwareSerial.h>
#include <TinyGPS++.h>

#define GPS_RX_PIN  A0
#define GPS_TX_PIN  A1

#define GPS_BAUD    9600

SoftwareSerial gpsSerial(GPS_RX_PIN, GPS_TX_PIN);
TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  
  gpsSerial.begin(GPS_BAUD);

  Serial.println(F(""));
  Serial.println(F("╔════════════════════════════════════════════════╗"));
  Serial.println(F("║   GPS NEO-6M 定位模块测试                    ║"));
  Serial.println(F"║   TX=A0, RX=A1 (软件串口)                   ║"));
  Serial.println(F("║   波特率: 9600                              ║"));
  Serial.println(F("╚════════════════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("⚠️ 重要: 请将GPS模块放在窗边或室外!"));
  Serial.println(F("   室内无法接收到GPS信号!"));
  Serial.println(F(""));
  Serial.println(F("等待GPS数据..."));
}

void loop() {
  unsigned long start = millis();
  
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    
    if (gps.encode(c)) {
      displayInfo();
      return;
    }
  }

  if (millis() - start > 5000 && gps.charsProcessed() < 10) {
    Serial.println(F(""));
    Serial.println(F("❌ 未检测到GPS数据! 检查以下项目:"));
    Serial.println(F("   1. 接线: GPS TX → A0, GPS RX → A1"));
    Serial.println(F("   2. 电源: GPS VCC → 5V, GND → GND"));
    Serial.println(F("   3. 波特率: 确认GPS模块是9600 (有些是4800)"));
    Serial.println(F("   4. TX/RX不要接反!"));
    while(true);
  }

  static unsigned long lastDisplay = 0;
  if (millis() - lastDisplay > 2000) {
    lastDisplay = millis();
    
    if (!gps.location.isValid()) {
      Serial.print(F("🛰️ 搜索卫星中... "));
      Serial.print(gps.satellites.value());
      Serial.print(F(" 颗 | 原始数据: "));
      Serial.print(gps.charsProcessed());
      Serial.println(F(" 字符"));
    } else {
      displayInfo();
    }
  }
}

void displayInfo() {
  Serial.print(F("┌────────────────────────────────────────┐")); Serial.println(F(""));
  Serial.print(F("│ 📍 定位状态: "));

  if (gps.location.isValid()) {
    Serial.print(F("✅ 已定位                        │")); Serial.println(F(""));
  } else {
    Serial.print(F("⏳ 搜索中...                      │")); Serial.println(F(""));
  }

  Serial.print(F("│                                        │")); Serial.println(F("");
  
  Serial.print(F("│ 纬度: "));
  if (gps.location.isValid()) {
    Serial.print(gps.location.lat(), 6);
    Serial.print(F("°                  │"));
  } else {
    Serial.print(F("--                            │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 经度: "));
  if (gps.location.isValid()) {
    Serial.print(gps.location.lng(), 6);
    Serial.print(F("°                 │"));
  } else {
    Serial.print(F("--                           │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 高度: "));
  if (gps.altitude.isValid()) {
    Serial.print(gps.altitude.meters());
    Serial.print(F(" m                       │"));
  } else {
    Serial.print(F("-- m                         │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 速度: "));
  if (gps.speed.isValid()) {
    Serial.print(gps.speed.kmph());
    Serial.print(F(" km/h                     │"));
  } else {
    Serial.print(F("-- km/h                      │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 航向: "));
  if (gps.course.isValid()) {
    Serial.print(gps.course.deg());
    Serial.print(F("°                          │"));
  } else {
    Serial.print(F("--°                          │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 卫星数: "));
  if (gps.satellites.isValid()) {
    Serial.print(gps.satellites.value());
    Serial.print(F(" 颗                        │"));
  } else {
    Serial.print(F("-- 颗                        │"));
  }
  Serial.println(F("");

  Serial.print(F("│ HDOP: "));
  if (gps.hdop.isValid()) {
    Serial.print(gps.hdop.value() / 100.0, 1);
    Serial.print(F("                           │"));
  } else {
    Serial.print(F("--                           │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 时间: "));
  if (gps.time.isValid()) {
    if (gps.time.hour() < 10) Serial.print(F("0"));
    Serial.print(gps.time.hour());
    Serial.print(F(":"));
    if (gps.time.minute() < 10) Serial.print(F("0"));
    Serial.print(gps.time.minute());
    Serial.print(F(":"));
    if (gps.time.second() < 10) Serial.print(F("0"));
    Serial.print(gps.time.second());
    Serial.print(F("                │"));
  } else {
    Serial.print(F("--:--:--                      │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 日期: "));
  if (gps.date.isValid()) {
    Serial.print(gps.date.year());
    Serial.print(F("-"));
    if (gps.date.month() < 10) Serial.print(F("0"));
    Serial.print(gps.date.month());
    Serial.print(F("-"));
    if (gps.date.day() < 10) Serial.print(F("0"));
    Serial.print(gps.date.day());
    Serial.print(F("               │"));
  } else {
    Serial.print(F("----/--/--                    │"));
  }
  Serial.println(F("");

  Serial.print(F("│ 已处理: "));
  Serial.print(gps.charsProcessed());
  Serial.print(F(" 字符, "));
  Serial.print(gps.failedChecksum());
  Serial.print(F(" 校验错误           │"));
  Serial.println(F("");

  Serial.print(F("└────────────────────────────────────────┘")); Serial.println(F("");
}

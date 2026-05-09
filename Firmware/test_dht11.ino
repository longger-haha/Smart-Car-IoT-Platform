/**
 * ═══════════════════════════════════════════════════════════════
 *  测试2: DHT11 温湿度传感器
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 实时读取温度和湿度, 通过串口输出
 *  接线:
 *    DHT11 VCC   → Arduino 5V
 *    DHT11 GND   → Arduino GND
 *    DHT11 DATA  → Arduino D7
 *
 *  使用方法:
 *    1. 需要安装 DHT sensor library (Adafruit)
 *       工具 → 管理库 → 搜索 "DHT" → 安装 Adafruit DHT
 *    2. 上传到 Arduino Uno
 *    3. 打开串口监视器 (波特率 9600)
 *    4. 观察温湿度读数
 *
 *  预期结果:
 *    温度: 常温范围 15~35°C
 *    湿度: 30~80% RH (视环境而定)
 *    如果显示 NaN: 检查接线或更换传感器
 */

#include <DHT.h>

#define DHT_PIN   7
#define DHT_TYPE  DHT11

DHT dht(DHT_PIN, DHT_TYPE);

void setup() {
  Serial.begin(9600);
  
  dht.begin();

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║   DHT11 温湿度传感器测试             ║"));
  Serial.println(F("║   DATA = D7                          ║"));
  Serial.println(F("╚══════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("等待传感器稳定..."));
  delay(2000);
  Serial.println(F(""));
  Serial.println(F("温度(°C) | 湿度(%) | 体感温度 | 状态提示"));
  Serial.println(F("─────────┼─────────┼──────────┼──────────────"));
}

void loop() {
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  float hic = dht.computeHeatIndex(temp, humi, false);

  if (isnan(temp) || isnan(humi)) {
    Serial.print(F("   --    │   --    │    --    │ ❌ 读取失败!"));
    Serial.println(F(""));
    Serial.println(F("         检查接线: VCC→5V, GND→GND, DATA→D7"));
  } else {
    if (temp < 10) {
      Serial.print(temp, 1);
      Serial.print(F("  │ "));
      Serial.print(humi, 1);
      Serial.print(F("  │ "));
      Serial.print(hic, 1);
      Serial.print(F("   │ 🥶 很冷!"));
    } else if (temp < 20) {
      Serial.print(temp, 1);
      Serial.print(F("  │ "));
      Serial.print(humi, 1);
      Serial.print(F("  │ "));
      Serial.print(hic, 1);
      Serial.print(F("   │ ❄️ 偏冷"));
    } else if (temp < 28) {
      Serial.print(temp, 1);
      Serial.print(F("  │ "));
      Serial.print(humi, 1);
      Serial.print(F("  │ "));
      Serial.print(hic, 1);
      Serial.print(F("   │ ✅ 舒适"));
    } else if (temp < 35) {
      Serial.print(temp, 1);
      Serial.print(F("  │ "));
      Serial.print(humi, 1);
      Serial.print(F("  │ "));
      Serial.print(hic, 1);
      Serial.print(F("   │ 🌡️ 偏热"));
    } else {
      Serial.print(temp, 1);
      Serial.print(F("  │ "));
      Serial.print(humi, 1);
      Serial.print(F("  │ "));
      Serial.print(hic, 1);
      Serial.print(F("   │ 🔥 很热!"));
    }

    if (humi < 30) {
      Serial.print(F(" 干燥"));
    } else if (humi > 70) {
      Serial.print(F(" 潮湿"));
    }
  }

  Serial.println(F(""));
  delay(2000);
}

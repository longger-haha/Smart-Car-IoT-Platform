/**
 * ═══════════════════════════════════════════════════════════════
 *  测试1: HC-SR04 超声波测距传感器
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 实时测量前方障碍物距离, 通过串口输出
 *  接线:
 *    HC-SR04 VCC   → Arduino 5V
 *    HC-SR04 GND   → Arduino GND
 *    HC-SR04 Trig  → Arduino D5
 *    HC-SR04 Echo  → Arduino D2
 *
 *  使用方法:
 *    1. 上传到 Arduino Uno
 *    2. 打开串口监视器 (波特率 9600)
 *    3. 观察距离读数
 *    4. 用手或物体挡住超声波探头测试
 *
 *  预期结果:
 *    正常距离: 2cm ~ 400cm
 *    超出范围: 显示 0 或 999
 */

#define TRIG_PIN  5
#define ECHO_PIN  2

long duration;
float distance_cm;

void setup() {
  Serial.begin(9600);
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════╗"));
  Serial.println(F("║   HC-SR04 超声波测距测试            ║"));
  Serial.println(F"║   Trig = D5, Echo = D2               ║");
  Serial.println(F("╚══════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("距离(cm) | 状态提示"));
  Serial.println(F("─────────┼──────────────"));
}

void loop() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  duration = pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) {
    distance_cm = 999;
    Serial.print(F("   --    │ ⚠️ 超出范围 (>400cm) 或未检测到回波"));
  } else {
    distance_cm = duration * 0.034 / 2;

    if (distance_cm < 2) {
      Serial.print(distance_cm, 1);
      Serial.print(F("   │ ⚠️ 太近了! (<2cm)"));
    } else if (distance_cm < 20) {
      Serial.print(distance_cm, 1);
      Serial.print(F("   │ 🔴 危险! 非常近!"));
    } else if (distance_cm < 50) {
      Serial.print(distance_cm, 1);
      Serial.print(F("   │ 🟡 注意! 较近"));
    } else if (distance_cm < 200) {
      Serial.print(distance_cm, 1);
      Serial.print(F("   │ 🟢 安全距离"));
    } else {
      Serial.print(distance_cm, 1);
      Serial.print(F("   │ ✅ 远处无障碍"));
    }
  }

  Serial.println(F(""));
  delay(500);
}

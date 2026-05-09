/**
 * ═══════════════════════════════════════════════════════════════
 *  测试6: L293D 4WD Motor Control Shield 电机驱动板测试
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 逐个测试M1~M4四个电机通道, 验证接线是否正确
 *
 *  接线 (L293D Shield 堆叠在Arduino Uno上):
 *    L293D Shield 直接插入 Arduino Uno (不需要额外接线!)
 *    
 *    电机连接:
 *      M1端子 → 左前电机
 *      M2端子 → 右前电机
 *      M3端子 → 左后电机
 *      M4端子 → 右后电机
 *    
 *    电源连接:
 *      EXT_PWR(+) → 电池正极 (7~12V)
 *      EXT_PWR(-) → 电池负极 + Arduino GND
 *
 *  ⚠️ 测试前必须接好外部电源!
 *     USB 5V 无法驱动电机! 必须用电池或7~12V电源!
 *
 *  使用方法:
 *    1. 安装 AFMotor 库 (Adafruit Motor Shield)
 *       工具 → 管理库 → 搜索 "AFMotor" 或 "Motor Shield" → 安装
 *    2. 接好 L293D Shield 和4个电机
 *    3. **接通外部电源** (7~12V 到 EXT_PWR)
 *    4. 上传到 Arduino Uno
 *    5. 打开串口监视器 (波特率 9600)
 *    6. 观察每个电机的转动情况
 *
 *  测试原理:
 *    L293D 是 H桥电机驱动芯片, 通过控制方向引脚和PWM引脚实现:
 *    - 正转: DIR=HIGH + PWM调速
 *    - 反转: DIR=LOW  + PWM调速  
 *    - 刹车: RELEASE (自由停止) 或 BRAKE (短接刹车)
 *
 *  预期结果:
 *    M1→左前轮正转2秒→停1秒→反转2秒→停
 *    M2→右前轮正转2秒→停1秒→反转2秒→停
 *    M3→左后轮正转2秒→停1秒→反转2秒→停
 *    M4→右后轮正转2秒→停1秒→反转2秒→停
 *    然后所有电机同时前进→后退→左转→右转→停止
 */

#include <AFMotor.h>

// L293D Shield 的4个电机通道
// M1=左前, M2=右前, M3=左后, M4=右后
AF_DCMotor motor_M1(1);
AF_DCMotor motor_M2(2);
AF_DCMotor motor_M3(3);
AF_DCMotor motor_M4(4);

void setup() {
  Serial.begin(9600);

  Serial.println(F(""));
  Serial.println(F("╔═══════════════════════════════════════════════════╗"));
  Serial.println(F("║   L293D 4WD Motor Shield 电机驱动板测试          ║"));
  Serial.println(F("║   通道: M1=左前, M2=右前, M3=左后, M4=右后      ║"));
  Serial.println(F("╚═══════════════════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("⚠️ 重要提示:"));
  Serial.println(F("   1. 确认已接通外部电源 (7~12V) 到 EXT_PWR!"));
  Serial.println(F("   2. 确认4个电机已接到 M1/M2/M3/M4 端子!"));
  Serial.println(F("   3. 确认车轮已离地 (避免小车跑掉)!"));
  Serial.println(F(""));
  Serial.println(F("━━━ 测试原理说明 ━━━"));
  Serial.println(F(""));
  Serial.println(F("L293D 芯片内部有4个H桥电路, 每个H桥控制1个电机:"));
  Serial.println(F(""));
  Serial.println(F("  H桥原理图:"));
  Serial.println(F("           VCC"));
  Serial.println(F("            │"));
  Serial.println(F("     ┌──────┴──────┐"));
  Serial.println(F("     │             │"));
  Serial.println(F("   Q1(A1)       Q2(A2)"));
  Serial.println(F("     │             │"));
  Serial.println(F("  ───┴── 电机  ───┴──"));
  Serial.println(F("     │             │"));
  Serial.println(F("   Q3(B1)       Q4(B2)"));
  Serial.println(F("     │             │"));
  Serial.println(F("     └──────┬──────┘"));
  Serial.println(F("            │"));
  Serial.println(F("           GND"));
  Serial.println(F(""));
  Serial.println(F("  正转: Q1+Q4导通 (电流从A1→B1流过电机)"));
  Serial.println(F("  反转: Q2+Q3导通 (电流从A2→B2流过电机)"));
  Serial.println(F("  刹车: Q1+Q2导通或Q3+Q4导通 (电机短路制动)"));
  Serial.println(F("  停止: 全部断开 (电机自由滑行)"));
  Serial.println(F(""));
  Serial.println(F("  PWM调速: 快速开关Q1/Q2, 改变平均电压"));
  Serial.println(F(""));
  delay(3000);

  Serial.println(F("━━━ 开始逐个电机测试 ━━━"));
  Serial.println(F(""));
}

void loop() {
  testSingleMotor(&motor_M1, "M1-左前", 1);
  testSingleMotor(&motor_M2, "M2-右前", 2);
  testSingleMotor(&motor_M3, "M3-左后", 3);
  testSingleMotor(&motor_M4, "M4-右后", 4);

  Serial.println(F(""));
  Serial.println(F("━━━ 单独电机测试完成! ━━━"));
  Serial.println(F(""));
  Serial.println(F("━━━ 开始组合动作测试 ━━━"));
  Serial.println(F(""));

  testCombinedMotion();

  Serial.println(F(""));
  Serial.println(F("╔═══════════════════════════════════════════════════╗"));
  Serial.println(F("║   🎉 所有电机测试完成!                          ║"));
  Serial.println(F("║                                                ║"));
  Serial.println(F("║   如果某个电机不转或方向反了:                   ║"));
  Serial.println(F("║   → 交换该电机的两根线 (Mx的+和-互换)          ║"));
  Serial.println(F("║                                                ║"));
  Serial.println(F("║   如果全部正常 → 可以使用 smartrover_uno.ino   ║"));
  Serial.println(F("╚═══════════════════════════════════════════════════╝"));

  while(true) {
    blinkLED();
    delay(2000);
  }
}

void testSingleMotor(AF_DCMotor* motor, String name, int num) {
  int speed = 150;

  Serial.print(F("▶ 测试 "));
  Serial.print(name);
  Serial.print(F(" [PWM="));
  Serial.print(speed);
  Serial.println(F("]"));

  Serial.println(F("  ┌────────────────────────────────────────┐"));
  Serial.print(F("  │ Step 1: "));
  Serial.print(name);
  Serial.println(F(" 正转 (FORWARD)        │"));

  motor->setSpeed(speed);
  motor->run(FORWARD);
  delay(2000);

  motor->run(RELEASE);
  Serial.print(F("  │ Step 2: "));
  Serial.print(name);
  Serial.println(F(" 停止 (RELEASE)         │"));
  delay(1000);

  Serial.print(F("  │ Step 3: "));
  Serial.print(name);
  Serial.println(F(" 反转 (BACKWARD)        │"));

  motor->run(BACKWARD);
  delay(2000);

  motor->run(RELEASE);
  Serial.print(F("  │ Step 4: "));
  Serial.print(name);
  Serial.println(F(" 停止 (RELEASE)         │"));
  Serial.println(F("  └────────────────────────────────────────┘"));
  Serial.println(F(""));
  delay(500);
}

void testCombinedMotion() {
  int speed = 150;

  Serial.println(F("  ┌────────────────────────────────────────┐"));
  Serial.println(F("  │ 组合1: 全部前进 (模拟直行)              │"));

  setAllMotors(FORWARD, speed);
  delay(2500);

  Serial.println(F("  │ 组合2: 全部后退                         │"));

  setAllMotors(BACKWARD, speed);
  delay(2500);

  Serial.println(F("  │ 组合3: 左转 (左侧反转, 右侧正转)        │"));

  turnLeft(speed);
  delay(2000);

  stopAllMotors();
  Serial.println(F("  │         停止                              │"));
  delay(1000);

  Serial.println(F("  │ 组合4: 右转 (左侧正转, 右侧反转)        │"));

  turnRight(speed);
  delay(2000);

  stopAllMotors();
  Serial.println(F("  │         停止                              │"));
  delay(1000);

  Serial.println(F("  │ 组合5: 原地左旋 (顺时针旋转)             │"));

  spinLeft(speed);
  delay(2000);

  stopAllMotors();
  Serial.println(F("  │         停止                              │"));
  delay(1000);

  Serial.println(F("  │ 组合6: 原地右旋 (逆时针旋转)             │"));

  spinRight(speed);
  delay(2000);

  stopAllMotors();
  Serial.println(F("  │         停止                              │"));

  Serial.println(F("  │ 组合7: PWM调速测试 (0%→25%→50%→75%→100%)│"));

  for (int s = 0; s <= 255; s += 64) {
    setAllMotors(FORWARD, s);
    Serial.print(F("  │   PWM="));
    Serial.print(s);
    Serial.print(F("/255 ("));
    Serial.print(s * 100 / 255);
    Serial.println(F("%)                  │"));
    delay(800);
  }

  stopAllMotors();
  Serial.println(F("  │         停止                              │"));
  Serial.println(F("  └────────────────────────────────────────┘"));
  Serial.println(F(""));
}

void setAllMotors(uint8_t direction, int speed) {
  motor_M1.setSpeed(speed);
  motor_M2.setSpeed(speed);
  motor_M3.setSpeed(speed);
  motor_M4.setSpeed(speed);

  motor_M1.run(direction);
  motor_M2.run(direction);
  motor_M3.run(direction);
  motor_M4.run(direction);
}

void stopAllMotors() {
  motor_M1.run(RELEASE);
  motor_M2.run(RELEASE);
  motor_M3.run(RELEASE);
  motor_M4.run(RELEASE);
}

void turnLeft(int speed) {
  motor_M1.setSpeed(speed); motor_M1.run(BACKWARD);
  motor_M3.setSpeed(speed); motor_M3.run(BACKWARD);
  motor_M2.setSpeed(speed); motor_M2.run(FORWARD);
  motor_M4.setSpeed(speed); motor_M4.run(FORWARD);
}

void turnRight(int speed) {
  motor_M1.setSpeed(speed); motor_M1.run(FORWARD);
  motor_M3.setSpeed(speed); motor_M3.run(FORWARD);
  motor_M2.setSpeed(speed); motor_M2.run(BACKWARD);
  motor_M4.setSpeed(speed); motor_M4.run(BACKWARD);
}

void spinLeft(int speed) {
  motor_M1.setSpeed(speed); motor_M1.run(BACKWARD);
  motor_M3.setSpeed(speed); motor_M3.run(BACKWARD);
  motor_M2.setSpeed(speed); motor_M2.run(FORWARD);
  motor_M4.setSpeed(speed); motor_M4.run(FORWARD);
}

void spinRight(int speed) {
  motor_M1.setSpeed(speed); motor_M1.run(FORWARD);
  motor_M3.setSpeed(speed); motor_M3.run(FORWARD);
  motor_M2.setSpeed(speed); motor_M2.run(BACKWARD);
  motor_M4.setSpeed(speed); motor_M4.run(BACKWARD);
}

void blinkLED() {
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH);
  delay(200);
  digitalWrite(LED_BUILTIN, LOW);
  delay(200);
}

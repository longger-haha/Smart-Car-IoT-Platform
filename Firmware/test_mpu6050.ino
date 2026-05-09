/**
 * ═══════════════════════════════════════════════════════════════
 *  测试4: MPU6050 六轴姿态传感器 (加速度计+陀螺仪)
 * ═══════════════════════════════════════════════════════════════
 *
 *  功能: 读取MPU6050的原始数据, 计算角度和温度, 通过串口输出
 *  接线 (I²C总线):
 *    MPU6050 VCC   → Arduino 3.3V (⚠️ 必须是3.3V!)
 *    MPU6050 GND   → Arduino GND
 *    MPU6050 SDA   → Arduino A4
 *    MPU6050 SCL   → Arduino A5
 *    MPU6050 AD0   → GND (I2C地址=0x68) 或悬空
 *
 *  使用方法:
 *    1. 上传到 Arduino Uno
 *    2. 打开串口监视器 (波特率 115200)
 *    3. 观察传感器读数
 *    4. 倾斜/旋转板子观察数值变化
 *
 *  预期结果:
 *    加速度: 静止时 Z轴 ≈ +16384 (1g), XY接近0
 *    陀螺仪: 静止时接近0 (可能有微小漂移)
 *    温度: 常温范围 20~40°C
 *    如果显示 "未检测到": 检查I²C接线 (A4/A5) 和地址
 */

#include <Wire.h>

#define MPU6050_ADDR  0x68

int16_t ax, ay, az;
int16_t gx, gy, gz;
int16_t tempRaw;

float accX, accY, accZ;
float gyroX, gyroY, gyroZ;
float temperature;

float roll, pitch;

bool mpuReady = false;

void setup() {
  Serial.begin(115200);
  
  Wire.begin();
  
  delay(100);

  Serial.println(F(""));
  Serial.println(F("╔══════════════════════════════════════════════╗"));
  Serial.println(F("║   MPU6050 六轴姿态传感器测试               ║"));
  Serial.println(F"║   SDA=A4, SCL=A5 (I2C)                     ║"));
  Serial.println(F("║   地址: 0x68                              ║"));
  Serial.println(F("╚══════════════════════════════════════════════╝"));
  Serial.println(F(""));
  Serial.println(F("初始化 MPU6050..."));

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0x80);
  Wire.endTransmission(true);
  delay(100);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x6B);
  Wire.write(0x03);
  Wire.endTransmission(true);
  delay(10);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1A);
  Wire.write(0x03);
  Wire.endTransmission(true);
  delay(10);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1B);
  Wire.write(0x18);
  Wire.endTransmission(true);
  delay(10);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x1C);
  Wire.write(0x00);
  Wire.endTransmission(true);
  delay(50);

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x75);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 1, true);
  uint8_t whoAmI = Wire.read();

  if (whoAmI == 0x68 || whoAmI == 0x69) {
    mpuReady = true;
    Serial.print(F("✅ MPU6050 检测成功! WHO_AM_I = 0x"));
    Serial.println(whoAmI, HEX);
    
    if (whoAmI == 0x69) {
      Serial.println(F("ℹ️ AD0拉高, 地址为0x69"));
    }
  } else {
    mpuReady = false;
    Serial.print(F("❌ MPU6050 未响应! 读到的值: 0x"));
    Serial.println(whoAmI, HEX);
    Serial.println(F(""));
    Serial.println(F("🔧 排查建议:"));
    Serial.println(F("   1. 检查接线: SDA→A4, SCL→A5"));
    Serial.println(F("   2. 检查电源: VCC必须是3.3V!"));
    Serial.println(F("   3. 确认AD0接地(地址0x68)或悬空"));
    Serial.println(F("   4. 尝试加10K上拉电阻到SDA/SCL"));
  }

  delay(500);
  Serial.println(F(""));
  Serial.println(F("加速度 X│Y│Z    陀螺仪 X│Y│Z    温度     │Roll │Pitch"));
  Serial.println(F("────────┼──┼───   ────────┼──┼───   ─────    │─────│─────"));
}

void loop() {
  if (!mpuReady) {
    Serial.println(F("❌ 传感器未就绪, 无法读取数据"));
    delay(2000);
    return;
  }

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU6050_ADDR, 14, true);

  ax     = (Wire.read() << 8 | Wire.read());
  ay     = (Wire.read() << 8 | Wire.read());
  az     = (Wire.read() << 8 | Wire.read());
  tempRaw= (Wire.read() << 8 | Wire.read());
  gx     = (Wire.read() << 8 | Wire.read());
  gy     = (Wire.read() << 8 | Wire.read());
  gz     = (Wire.read() << 8 | Wire.read());

  accX = ax / 16384.0f;
  accY = ay / 16384.0f;
  accZ = az / 16384.0f;

  gyroX = gx / 131.07f;
  gyroY = gy / 131.07f;
  gyroZ = gz / 131.07f;

  temperature = tempRaw / 340.00f + 36.53f;

  roll  = atan2(accY, accZ) * 180.0 / PI;
  pitch = atan2(-accX, sqrt(accY*accY + accZ*accZ)) * 180.0 / PI;

  char buf[120];
  sprintf(buf, "%6.2f|%6.2f|%6.2f  %7.2f|%7.2f|%7.2f  %6.1f°C  │%5.1f°│%5.1f°",
          accX, accY, accZ,
          gyroX, gyroY, gyroZ,
          temperature,
          roll, pitch);
          
  Serial.println(buf);

  delay(200);
}

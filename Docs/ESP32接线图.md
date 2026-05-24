## Arduino UNO → ESP32 接线对照表

### 一、传感器接线对比

| 传感器 | 功能 | Arduino UNO | ESP32 (你的板子) | 变化 |
|--------|------|-------------|------------------|------|
| **MPU6050** | SDA | A4 | D21 (GPIO21) | 改引脚 |
| | SCL | A5 | D22 (GPIO22) | 改引脚 |
| | VCC | 5V / 3.3V | 3V3 | ⚠️ MPU6050 只能接 3.3V |
| | GND | GND | GND | 不变 |
| **HC-SR04** | TRIG | A0 | D25 (GPIO25) | 改引脚 |
| | ECHO | A1 | D26 (GPIO26) | 改引脚 |
| | VCC | 5V | 5V 或 3V3 | 不变（5V 更稳定）|
| | GND | GND | GND | 不变 |
| **红外左** | OUT | D9 | D27 (GPIO27) | 改引脚 |
| | VCC | 3.3V/5V | 3V3 | 不变 |
| | GND | GND | GND | 不变 |
| **红外右** | OUT | D10 | D14 (GPIO14) | 改引脚 |
| | VCC | 3.3V/5V | 3V3 | 不变 |
| | GND | GND | GND | 不变 |
| **DHT11** | DATA | D13 | D32 (GPIO32) | 改引脚 |
| | VCC | 5V/3.3V | 3V3 | ⚠️ DHT11 用 3.3V 也行，但建议 5V 更稳 |
| | GND | GND | GND | 不变 |

### 二、电机驱动 L293D 扩展板接线（重点！）

#### 2.1 扩展板结构

你用的是 **Adafruit Motor Shield v1 (L293D 扩展板)**，不是裸 L293D 芯片。

扩展板内部结构：
- **2 片 L293D** 芯片（驱动 4 个电机）
- **1 片 74HC595** 移位寄存器（控制所有 L293D 的 IN1/IN2/IN3/IN4）
- 4 个 PWM 通道（控制 4 个 L293D 的 EN 使能）

> ⚠️ **关键**：扩展板不能直接用 GPIO 控制 L293D！必须通过 74HC595 移位寄存器协议。
> 之前直接连 GPIO16/17/18/19 等到扩展板引脚，电机不会转，因为信号没经过移位寄存器。

#### 2.2 扩展板需要的 8 个信号

| 扩展板引脚 | 功能 | ESP32 GPIO | 说明 |
|-----------|------|-----------|------|
| **D12** | LATCH (锁存) | **GPIO5** | 74HC595 锁存信号 |
| **D4** | CLOCK (时钟) | **GPIO18** | 74HC595 移位时钟 |
| **D7** | ENABLE (使能) | **GPIO23** | 74HC595 输出使能，**LOW=启用** |
| **D8** | DATA (数据) | **GPIO13** | 74HC595 串行数据 |
| **D11** | PWM M1 | **GPIO4** | M1 右前 使能/PWM |
| **D3** | PWM M2 | **GPIO16** | M2 左前 使能/PWM |
| **D6** | PWM M3 | **GPIO17** | M3 左后 使能/PWM |
| **D5** | PWM M4 | **GPIO19** | M4 右后 使能/PWM |

> ⚠️ **D7 (ENABLE) 必须接 LOW 才能启用 74HC595 输出！** 如果不接或悬空，所有电机都不会动。

#### 2.3 74HC595 移位寄存器位映射

来自 AFMotor.h 源码确认的位映射：

```
74HC595 位映射 (AFMotor.h 定义):
  M1(右前): MOTOR1_A=Bit2 (正转), MOTOR1_B=Bit3 (反转), PWM=D11
  M2(左前): MOTOR2_A=Bit1 (正转), MOTOR2_B=Bit4 (反转), PWM=D3
  M3(左后): MOTOR3_A=Bit5 (正转), MOTOR3_B=Bit7 (反转), PWM=D6
  M4(右后): MOTOR4_A=Bit0 (正转), MOTOR4_B=Bit6 (反转), PWM=D5
```

#### 2.4 电机控制原理

每个电机需要 2 个信号：
1. **74HC595 位** → 控制方向（正转位=1 或 反转位=1）
2. **PWM** → 控制速度（EN 使能引脚的占空比）

| 动作 | 74HC595 正转位 | 74HC595 反转位 | PWM |
|------|-------------|-------------|-----|
| 正转 | 1 | 0 | 1~255 |
| 反转 | 0 | 1 | 1~255 |
| 停止 | 0 | 0 | 0 |
| 刹车 | 1 | 1 | 0 |

#### 2.5 电机与轮子对应

| 扩展板端口 | 轮子 | 74HC595 正转位 | 74HC595 反转位 | PWM引脚 |
|-----------|------|-------------|-------------|---------|
| M1 | 右前 (RF) | Bit2 (MOTOR1_A) | Bit3 (MOTOR1_B) | D11 → GPIO4 |
| M2 | 左前 (LF) | Bit1 (MOTOR2_A) | Bit4 (MOTOR2_B) | D3 → GPIO16 |
| M3 | 左后 (LB) | Bit5 (MOTOR3_A) | Bit7 (MOTOR3_B) | D6 → GPIO17 |
| M4 | 右后 (RB) | Bit0 (MOTOR4_A) | Bit6 (MOTOR4_B) | D5 → GPIO19 |

#### 2.6 完整接线表（跳线连接）

```
扩展板排针          跳线         ESP32 GPIO
─────────────────────────────────────────
D12 (LATCH)    ──── 跳线 ────  GPIO5
D4  (CLOCK)    ──── 跳线 ────  GPIO18
D7  (ENABLE)   ──── 跳线 ────  GPIO23   ← 必须LOW才启用!
D8  (DATA)     ──── 跳线 ────  GPIO13
D11 (PWM M1右前) ── 跳线 ────  GPIO4
D3  (PWM M2左前) ── 跳线 ────  GPIO16
D6  (PWM M3左后) ── 跳线 ────  GPIO17
D5  (PWM M4右后) ── 跳线 ────  GPIO19
5V             ──── 跳线 ────  5V (或 VIN)
GND            ──── 跳线 ────  GND
```

> ⚠️ 扩展板的电源：5V 和 GND 必须连接！扩展板需要逻辑电源。
> 电机电源：扩展板有独立的电机电源端子（EXT_PWR），接 7.4V 电池。

#### 2.7 常见接线错误

| 错误现象 | 原因 | 解决 |
|---------|------|------|
| 电机完全不动 | 没用 74HC595 协议 | 必须通过 LATCH/CLOCK/DATA 控制，不能直接 GPIO |
| 电机完全不动 | D4/D7/D8 没接 | 这3个引脚控制移位寄存器，必须连接 |
| 电机完全不动 | 扩展板没供逻辑电 | 5V 和 GND 必须连接到 ESP32 |
| 电机嗡嗡响但不转 | 电机电源没接 | EXT_PWR 端子接 7.4V 电池 |
| 电机只能单向转 | 74HC595 位映射错 | 检查正转位和反转位是否正确 |
| 某个电机不动 | 对应 PWM 引脚没接 | 检查 D3/D5/D6/D11 是否连接 |
| ESP32 启动异常 | GPIO12 被使用 | ⚠️ GPIO12 是 strapping pin，不要用 |

### 三、ESP-01S 相关（全部移除！）

| 原来 UNO 接线 | 现在 ESP32 |
|---------------|-----------|
| UNO A2 ← ESP-01S TX | ❌ **不需要了**（ESP32 自带 WiFi）|
| UNO A3 → ESP-01S RX | ❌ **不需要了** |
| ESP-01S 电阻分压电路 | ❌ **不需要了** |
| ESP-01S 3.3V 供电 | ❌ **不需要了** |
| SoftwareSerial 软串口 | ❌ **不需要了** |

### 四、供电对比

| 项目 | UNO 系统 | ESP32 系统 |
|------|----------|-----------|
| 主控供电 | USB 5V 或 Vin 7-12V | USB-C 5V 或 Vin 5V |
| 传感器供电 | UNO 5V/3.3V 引脚 | ESP32 **3V3** 引脚（⚠️ 最大 600mA）|
| 电机供电 | 扩展板 EXT_PWR → 电池 | **扩展板 EXT_PWR → 7.4V 电池**（不要从 ESP32 取电！）|
| 扩展板逻辑电 | UNO 5V | ESP32 **5V** 或 VIN |
| 共地 | 所有 GND 连一起 | 所有 GND 连一起 |

> ⚠️ **关键提醒**：ESP32 的 3V3 引脚最大只能提供约 600mA，**绝对不能用来给电机供电**！
> 电机必须用独立电源（如 18650 电池组），通过扩展板的 EXT_PWR 端子供电，然后和 ESP32 共地即可。

### 五、ESP32 GPIO 使用总览

```
╔══════════════════════════════════════════════════════════════╗
║               ESP32 DevKit GPIO 分配表                      ║
╠══════════════════════════════════════════════════════════════╣
║                                                              ║
║  【电机驱动扩展板 - 74HC595 + PWM】                        ║
║  GPIO5   ──→  LATCH (扩展板 D12, 74HC595 锁存)             ║
║  GPIO18  ──→  CLOCK (扩展板 D4, 74HC595 时钟)              ║
║  GPIO23  ──→  ENABLE (扩展板 D7, LOW=启用!)                ║
║  GPIO13  ──→  DATA  (扩展板 D8, 74HC595 数据)              ║
║  GPIO4   ──→  PWM M1 (扩展板 D11, 右前)                    ║
║  GPIO16  ──→  PWM M2 (扩展板 D3, 左前)                     ║
║  GPIO17  ──→  PWM M3 (扩展板 D6, 左后)                     ║
║  GPIO19  ──→  PWM M4 (扩展板 D5, 右后)                     ║
║                                                              ║
║  【传感器】                                                  ║
║  GPIO21  ──→  MPU6050 SDA                                    ║
║  GPIO22  ──→  MPU6050 SCL                                    ║
║  GPIO25  ──→  HC-SR04 TRIG                                   ║
║  GPIO26  ──→  HC-SR04 ECHO                                   ║
║  GPIO27  ──→  红外左 OUT                                      ║
║  GPIO14  ──→  红外右 OUT                                      ║
║  GPIO32  ──→  DHT11 DATA                                     ║
║                                                              ║
║  【不可用 / 避免使用】                                       ║
║  GPIO0   ──→  ⚠️ Strapping (BOOT按钮)                       ║
║  GPIO2   ──→  ⚠️ 板载LED, Strapping                         ║
║  GPIO5   ──→  ✅ LATCH (扩展板 D12)                        ║
║  GPIO12  ──→  ⚠️ Strapping (Flash电压), 禁用!                ║
║  GPIO15  ──→  ⚠️ Strapping (静默启动信息)                    ║
║  GPIO34-39 ──→ 仅输入, 不可做PWM输出                         ║
║                                                              ║
║  【电源】                                                    ║
║  3V3     ──→  传感器供电 (最大600mA)                         ║
║  5V/VIN  ──→  扩展板逻辑电源 (5V + GND)                     ║
║  GND     ──→  所有设备共地                                    ║
║  7.4V电池 ──→  扩展板 EXT_PWR 端子 (电机电源)               ║
║                                                              ║
╚══════════════════════════════════════════════════════════════╝
```

### 六、实物接线速查卡

```
╔══════════════════════════════════════════════════════╗
║           从 UNO 迁移到 ESP32 接线速查               ║
╠══════════════════════════════════════════════════════╣
║                                                      ║
║  【传感器】          原来UNO    ──→   现在ESP32      ║
║  ───────────────────────────────────────────────     ║
║  MPU6050 SDA        A4         ──→   D21            ║
║  MPU6050 SCL        A5         ──→   D22            ║
║  HC-SR04 TRIG       A0         ──→   D25            ║
║  HC-SR04 ECHO       A1         ──→   D26            ║
║  红外左             D9         ──→   D27            ║
║  红外右             D10        ──→   D14            ║
║  DHT11              D13        ──→   D32            ║
║                                                      ║
║  【电机驱动扩展板 - 8根跳线】                         ║
║  ───────────────────────────────────────────────     ║
║  扩展板 D12 (LATCH)     ──→   D5  (GPIO5)           ║
║  扩展板 D4  (CLOCK)     ──→   D18 (GPIO18)          ║
║  扩展板 D7  (ENABLE)    ──→   D23 (GPIO23) LOW=启用 ║
║  扩展板 D8  (DATA)      ──→   D13 (GPIO13)          ║
║  扩展板 D11 (PWM M1右前) ──→   D4  (GPIO4)          ║
║  扩展板 D3  (PWM M2左前) ──→   D16 (GPIO16)         ║
║  扩展板 D6  (PWM M3左后) ──→   D17 (GPIO17)         ║
║  扩展板 D5  (PWM M4右后) ──→   D19 (GPIO19)         ║
║  扩展板 5V             ──→   ESP32 5V/VIN           ║
║  扩展板 GND            ──→   ESP32 GND              ║
║  扩展板 EXT_PWR        ──→   7.4V 电池 (电机电源)   ║
║                                                      ║
║  【移除】                                            ║
║  ───────────────────────────────────────────────     ║
║  ESP-01S 整个模块    ──→   ❌ 不需要了              ║
║  A2/A3 软串口线      ──→   ❌ 拆掉                  ║
║                                                      ║
║  【新增】                                            ║
║  ───────────────────────────────────────────────     ║
║  USB-C 烧录线        ──→   ✅ 直插电脑               ║
║  无需额外串口模块     ──→   ✅ 板载 CH340/CP2102     ║
║                                                      ║
╚══════════════════════════════════════════════════════╝
```

### 七、代码关键说明

固件使用 **74HC595 移位寄存器协议** 直接控制扩展板，不依赖 AFMotor 库：

```cpp
// 引脚定义 (来自 AFMotor.h 源码确认)
#define MOTOR_LATCH_PIN  5    // D12 → GPIO5  (锁存)
#define MOTOR_CLOCK_PIN  18   // D4  → GPIO18 (时钟)
#define MOTOR_ENABLE_PIN 23   // D7  → GPIO23 (使能, LOW=启用!)
#define MOTOR_DATA_PIN   13   // D8  → GPIO13 (数据)

// 发送数据到 74HC595
void motorShiftOut(uint8_t val) {
  digitalWrite(MOTOR_LATCH_PIN, LOW);
  shiftOut(MOTOR_DATA_PIN, MOTOR_CLOCK_PIN, MSBFIRST, val);
  digitalWrite(MOTOR_LATCH_PIN, HIGH);
}

// 初始化时必须启用 74HC595 输出
void initMotors() {
  pinMode(MOTOR_ENABLE_PIN, OUTPUT);
  digitalWrite(MOTOR_ENABLE_PIN, LOW);  // LOW = 启用! 这步不能漏!
  // ...
}

// 设置单个电机 (位映射来自 AFMotor.h)
void setMotorAF(uint8_t fwdBit, uint8_t bwdBit, int pwmPin, int speed, bool forward) {
  motorShiftReg &= ~((1 << fwdBit) | (1 << bwdBit));
  if (forward) motorShiftReg |= (1 << fwdBit);
  else         motorShiftReg |= (1 << bwdBit);
  motorShiftOut(motorShiftReg);
  ledcWrite(pwmPin, speed);
}
```

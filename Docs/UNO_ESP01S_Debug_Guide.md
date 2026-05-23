# SmartRover UNO+ESP-01S 连接调试指南
> **制定日期**: 2026-05-23

## 一、硬件连接

### 引脚分配

| 模块 | UNO引脚 | 说明 |
|------|---------|------|
| DHT11 | D13 | 温湿度 |
| HC-SR04 TRIG | A0 | 超声波触发 |
| HC-SR04 ECHO | A1 | 超声波回波 |
| ESP-01S TX | A2 | SoftwareSerial RX |
| ESP-01S RX | A3 | SoftwareSerial TX (**需电阻分压!**) |
| MPU6050 SDA | D9 | 手动I2C |
| MPU6050 SCL | D10 | 手动I2C |
| 左红外 | A5 | 低电平触发 |
| 右红外 | A4 | 低电平触发 |
| GPS | D0/D1 | 硬串口 Serial |

### 关键硬件注意事项

1. **ESP-01S 电阻分压**：UNO TX 输出 5V，ESP-01S RX 只能承受 3.3V。必须在 UNO A3 和 ESP-01S RX 之间加 1kΩ+2kΩ 电阻分压（1kΩ 串联，2kΩ 接地），否则 5V 信号会通过 ESP 内部保护二极管倒灌到 3.3V 电源轨，导致电压异常升高（实测 4.2V）。

2. **ESP-01S 供电**：必须用独立 3.3V 供电（峰值电流可达 300mA），不能从 UNO 的 3.3V 引脚取电。

3. **ESP-01S 固件**：必须烧录支持 AT+MQTT 指令的固件（乐鑫官方 ESP8266 AT+MQTT 固件，Bin version 2.2.0 及以上）。可通过 `AT+GMR` 查看固件版本。

---

## 二、踩坑记录与解决方案

### 坑1：`snprintf %f` 在 Arduino UNO 上输出 `?`

**现象**：后端解密成功但 JSON 解析失败，日志显示 `"temperature":?,"humidity":?`

**原因**：Arduino UNO 使用的 avr-libc 的 `snprintf` 不支持 `%f` 浮点格式化，直接输出 `?` 字符。

**解决**：所有浮点值用 `dtostrf()` 转为字符串，再用 `%s` 拼入 JSON：
```cpp
// 错误
snprintf(buf, sizeof(buf), "\"temperature\":%.1f,", temp);

// 正确
char fBuf[12];
dtostrf(temp, 1, 1, fBuf);
snprintf(buf, sizeof(buf), "\"temperature\":%s,", fBuf);
```

### 坑2：AT+MQTTPUB 中 XOR 加密数据破坏 AT 指令格式

**现象**：`[TEL] fail`，后端收不到数据，或 device_id 变成 IP 地址

**原因**：`AT+MQTTPUB=0,"topic","payload",0,0` 中 payload 用双引号包裹，但 XOR+Base64 编码后的数据可能包含 `"` 或 `\` 等特殊字符，破坏 AT 指令的引号配对。

**解决**：改用 `AT+MQTTPUBRAW`，分两步发送：
```
步骤1: AT+MQTTPUBRAW=0,"sensor/DEVICE_ID",len,0,0
步骤2: 等待 ">" 提示符
步骤3: 直接发送原始 XOR 加密数据（不再用引号包裹）
```

### 坑3：XOR 签名验证失败 — 逗号丢失

**现象**：`[MQTT] XOR checksum verify failed`

**原因**：固件计算签名时，JSON 末尾格式是 `"speed_pwm":150,`（有逗号），然后追加 `,"signature":"XXXXXXXX"}`。后端用 `re.sub(r',"signature":"[A-Fa-f0-9]{8}"\}', '', plaintext)` 去掉签名时，把逗号也删了，变成 `"speed_pwm":150`，导致签名不匹配。

**解决**：`re.sub` 替换结果保留逗号：
```python
# 错误 — 逗号被删
re.sub(r',"signature":"[A-Fa-f0-9]{8}"\}', '', plaintext)
# 结果: "speed_pwm":150

# 正确 — 保留逗号
re.sub(r',"signature":"[A-Fa-f0-9]{8}"\}', ',', plaintext)
# 结果: "speed_pwm":150,
```

### 坑4：数据库缺少新增字段

**现象**：`sqlalchemy.exc.OperationalError: (1054, "Unknown column 'telemetry_points.ir_obstacle' in 'field list'")`

**原因**：后端模型新增了 `ir_obstacle`, `imu_heading`, `imu_gyro_z`, `altitude`, `speed_kmh`, `satellites` 字段，但数据库表没有同步更新。

**解决**：手动 ALTER TABLE 添加字段：
```sql
ALTER TABLE telemetry_points
  ADD COLUMN ir_obstacle TINYINT(1) DEFAULT NULL AFTER ultrasonic_cm,
  ADD COLUMN imu_heading FLOAT DEFAULT NULL AFTER ir_obstacle,
  ADD COLUMN imu_gyro_z FLOAT DEFAULT NULL AFTER imu_heading,
  ADD COLUMN altitude FLOAT DEFAULT NULL AFTER speed_pwm,
  ADD COLUMN speed_kmh FLOAT DEFAULT NULL AFTER altitude,
  ADD COLUMN satellites SMALLINT DEFAULT NULL AFTER speed_kmh;
```

### 坑5：Arduino UNO 内存不足 (2KB SRAM 限制)

**现象**：`Global variables use 2535 bytes (123%) of dynamic memory`

**解决**：
- 消除 static 中间缓冲区，采用流式 XOR+Base64
- 合并 AT 响应和命令缓冲区为共享 `workBuf`，互斥复用
- 将 `DEVICE_SECRET`, `XOR_KEY` 等大字符串移入 `PROGMEM`
- 用极简 NMEA 解析替代 TinyGPS++ 库（省 ~120B RAM）
- MPU6050 用手动 I2C bit-bang 替代 Wire 库

### 坑6：ESP-01S AT 指令集选择

**现象**：`AT+CMQTT*` 指令无响应

**原因**：不同厂商的 ESP-01S AT 固件使用不同的 MQTT 指令集。乐鑫官方固件使用 `AT+MQTTUSERCFG`, `AT+MQTTCONN`, `AT+MQTTSUB`, `AT+MQTTPUB` 系列指令。

**解决**：确认固件版本后使用对应的指令集。可通过 `AT+GMR` 查看固件信息。

### 坑7：`+MQTT_SUB_RECV` vs `+MQTTSUBRECV` — 订阅消息格式错误（致命！）

**现象**：前端点击前进/后退，后端返回 200 OK，MQTT Broker 日志显示消息已送达设备且设备回复了 PUBACK，但 Arduino 串口没有任何指令接收日志。

**原因**：ESP-01S 的 AT+MQTT 固件在收到订阅消息时，通过串口输出的通知格式是 **`+MQTTSUBRECV:`**（无下划线），而固件代码中查找的是 **`+MQTT_SUB_RECV:`**（有下划线）。`strstr()` 永远匹配不到，导致指令被静默丢弃。

**解决**：将固件中所有 `+MQTT_SUB_RECV:` 替换为 `+MQTTSUBRECV:`。

**教训**：ESP-01S AT 固件的通知格式与乐鑫官方文档可能不一致，务必通过串口实际抓包确认格式。调试时可在 `readAT()` 中打印所有收到的原始数据来确认。

### 坑8：`pollESP()` 吃掉 AT 响应导致所有 AT 命令失败

**现象**：添加 `pollESP()` 函数主动轮询 ESP 串口后，所有 AT 命令返回空响应，WiFi 连接失败，MQTT 连接失败。

**原因**：`pollESP()` 在 `loop()` 中以最高优先级运行，它会读取 ESP 串口的所有数据到 `workBuf`。当 `sendAT()` 随后调用 `readAT()` 时，ESP 的 AT 响应已经被 `pollESP()` 读走并覆盖，`readAT()` 读到空数据。

**解决**：不要在 `loop()` 中用独立的轮询函数读取 ESP 串口。改为：
1. 在 `readAT()` 内部检测 `+MQTTSUBRECV:` 并暂存到 `pendingCmd`
2. `loop()` 空闲时用非阻塞方式读取（只读当前缓冲区已有字节，不等待）
3. 只在检测到 `+MQTTSUBRECV:` 时才继续等待完整 JSON

### 坑9：`readAT(500)` 在 loop 中干扰 sendAT 响应

**现象**：在 `loop()` 空闲时调用 `readAT(500)` 后，MQTT 连接失败，AT 命令返回空。

**原因**：`readAT(500)` 会等待 500ms 读取数据，在此期间如果有 AT 响应到达，会被它读走。后续 `sendAT()` 调用的 `readAT()` 就读不到响应了。

**解决**：空闲时只用非阻塞方式读取（`while(espSerial.available())` 不带超时），不要调用 `readAT()`。

### 坑10：碰撞预防太激进，淹没手动控制指令

**现象**：点击前进按钮后小车不动，后端日志疯狂刷 `COLLISION WARNING`。

**原因**：后端每次收到遥测数据（5秒一次），如果超声波 < 50cm 就自动发 `stop` 指令。由于小车静止时超声波一直 < 50cm，导致每 5 秒发一次 stop，手动发的 forward 指令被 stop 覆盖。

**解决**：
1. 暂时禁用碰撞预防（`COLLISION_PREVENTION_ENABLED = False`）
2. 后续改为节流机制（每 30 秒最多发一次）或由前端控制开关

### 坑11：ESP-01S 上电未就绪，AT 命令返回 ERROR

**现象**：`[AT-RAW] AT \n ERROR`，ESP 初始化失败。

**原因**：ESP-01S 上电后需要 1-2 秒完成内部初始化，在此期间发送 AT 命令会返回 ERROR。

**解决**：
1. 初始化前 `delay(2000)` 等待 ESP 就绪
2. 清空串口缓冲区（ESP 上电可能输出乱码）
3. AT 命令重试 5 次，每次间隔 1 秒

### 坑12：`AT+MQTTCONNCFG` 可能不被固件支持

**现象**：发送 `AT+MQTTCONNCFG=0,120,0,"","",0,0` 后 MQTT 连接失败。

**原因**：部分 ESP-01S AT+MQTT 固件版本不支持 `AT+MQTTCONNCFG` 命令，发送后会导致连接流程中断。

**解决**：跳过 `AT+MQTTCONNCFG`，直接使用 `AT+MQTTCONN` 连接。keepalive 和 clean_session 使用默认值。

### 坑13：Docker 容器代码修改不生效

**现象**：修改了后端 Python 代码，但 `docker compose restart backend` 后行为没变。

**原因**：后端代码是在 Docker 镜像构建时 COPY 进去的（不是挂载卷），`restart` 只是重启容器，不会重新构建镜像。

**解决**：代码修改后必须重新构建镜像：
```bash
docker compose build backend && docker compose up -d backend
```

### 坑14：指令下发延迟过大（约10秒）

**现象**：前端点击前进后，小车约 10 秒才动。

**原因**：
1. `mqttPubRaw` 有两次 `readAT`（等 `>` 3秒 + 等 `OK` 5秒），超时设置过长
2. `sendTelemetry` 和 `sendHeartbeat` 各调一次 `mqttPubRaw`，如果指令在遥测发送期间到达，要等遥测发完才回到 `loop()` 处理
3. `handleCommand()` 只在 `loop()` 顶部调用一次

**解决**：
1. 缩短 `readAT` 超时：等 `>` 从 3s→2s，等 `OK` 从 5s→3s
2. 在 `sendTelemetry()` 和 `sendHeartbeat()` 之后立即调用 `handleCommand()`
3. `loop()` 末尾 `delay` 从 50ms→30ms
4. `readAT()` 收到 OK/ERROR/`>` 后立即返回，不再等满超时（最大优化点）
5. 遥测频率从 5s→10s，心跳频率从 8s→15s，减少串口占用

### 坑15：`readAT` 提前返回的匹配模式太严格

**现象**：`readAT` 加了提前返回后，ESP 初始化失败，一直 retry。

**原因**：首次匹配用了 `strstr(workBuf, "\r\nOK\r\n")`，要求精确的换行包裹。但 ESP-01S 的响应格式可能是 `OK\r\n`（行首无 `\r\n`），导致匹配不到，等到超时才返回。

**解决**：放宽匹配模式，直接匹配 `"OK"` / `"ERROR"` / `"FAIL"` / `">"` 即可。匹配后再等 50ms 确保后续数据（如 `+MQTTSUBRECV` 可能在 OK 之后到达）也读入。

### 坑16：`checkMQTTMsg()` 的 JSON 提取逻辑偏移量错误

**现象**：`[MQTT-RECV] found SUB_RECV!` 但 `[MQTT-PAYLOAD]` 为空，指令无法执行。

**原因**：`checkMQTTMsg()` 用 `recv + 15` 跳过 `+MQTTSUBRECV:` 前缀，但该字符串只有 13 个字符，偏移多了 2 字节。后续的逗号跳过逻辑也会被 JSON 内部的逗号干扰，导致 payload 提取失败。

**解决**：不要手动计算偏移量跳逗号，直接用 `strchr(recv, '{')` 和 `strrchr(jsonStart, '}')` 提取 JSON，和 `readAT()` 中的逻辑保持一致。

**教训**：解析 AT 指令输出时，不要依赖固定偏移量或逗号计数，直接定位 JSON 的 `{` 和 `}` 最可靠。

---

## 三、调试流程

### 3.1 串口调试

使用 Arduino 串口监视器（9600 baud），关键日志标识：

| 日志前缀 | 含义 |
|----------|------|
| `[ESP]` | ESP-01S 初始化/通信 |
| `[WiFi]` | WiFi 连接状态 |
| `[MQTT]` | MQTT 连接/发布/订阅 |
| `[TEL]` | 遥测数据上报 |
| `[HB]` | 心跳 |
| `[CMD]` | 收到控制指令 |
| `[MEM]` | 剩余 RAM |

### 3.2 正常启动序列

```
SmartRover v3.2-test (DHT+SR04+WiFi)
========================================
[INIT] DHT11+HC-SR04 OK
[INIT] L293D OK
[NET] connecting...
[ESP] init...
[ESP] 9600 OK
[ESP] ready
[WiFi] REDMI K80 Ultra
[WiFi] OK
[WiFi] IP: 10.32.44.228
[MQTT] 10.32.44.189:1883
[MQTT] cfg OK
[MQTT] OK
[MQTT] sub OK
[MEM] RAM: 514B
Ready!
[TEL] OK seq=0
[HB] OK
```

### 3.3 常见故障排查

#### ESP-01S 无响应 `[ESP] no resp`
1. 检查接线：ESP TX → UNO A2, ESP RX → UNO A3（经电阻分压）
2. 检查 ESP-01S 供电：3.3V 独立电源，峰值 300mA
3. 尝试不同波特率：先 9600，再 115200
4. 检查 ESP-01S 是否已烧录 AT 固件

#### WiFi 连接失败 `[WiFi] fail`
1. 确认 SSID 和密码正确
2. 确认 WiFi 为 2.4GHz（ESP8266 不支持 5GHz）
3. 确认 ESP-01S 天线朝向正确

#### MQTT 连接失败 `[MQTT] conn fail`
1. 确认 MQTT Broker 地址和端口正确
2. 确认 Broker 已启动：`docker ps | grep mosquitto`
3. 确认网络连通：ESP-01S 和 Broker 在同一网段

#### 遥测发送失败 `[TEL] fail`
1. 检查 MQTT 连接是否仍然有效
2. 检查 PUBRAW 发送流程：命令头 → 等待 `>` → 发送数据
3. 检查数据长度是否正确：`xorEncodedLen()` 计算值与实际发送是否一致

#### 后端 JSON 解析失败
1. 检查固件中是否使用了 `%f`（UNO 不支持，改用 `dtostrf`）
2. 查看后端日志中的 `plaintext_preview`，确认解密后的 JSON 格式

#### 后端签名验证失败
1. 确认固件和数据库中的 `DEVICE_SECRET` 一致
2. 确认后端 `re.sub` 去签名时保留了逗号
3. 添加调试日志对比 `msg_body` 内容

### 3.4 后端调试

```bash
# 查看后端日志
docker logs smartrover-backend --tail 50

# 查看数据库设备
docker exec smartrover-mysql mysql -usruser -psrpass123 smartrover_db \
  -e "SELECT device_id, device_secret FROM devices;"

# 查看最新遥测数据
docker exec smartrover-mysql mysql -usruser -psrpass123 smartrover_db \
  -e "SELECT * FROM telemetry_points ORDER BY id DESC LIMIT 5;"

# 重新构建后端（代码修改后必须执行）
docker compose build backend && docker compose up -d backend
```

### 3.5 MQTT 调试

```bash
# 订阅所有主题（在宿主机或容器内）
docker exec smartrover-mosquitto mosquitto_sub -h localhost -t "#" -v

# 订阅特定设备
docker exec smartrover-mosquitto mosquitto_sub -h localhost -t "sensor/10.32.44.189" -v

# 手动发布测试指令
docker exec smartrover-mosquitto mosquitto_pub -h localhost \
  -t "cmd/10.32.44.189" -m '{"command":"stop","device_id":"10.32.44.189"}'
```

---

## 四、数据流架构

```
UNO 固件                    MQTT Broker              后端                    前端
  |                            |                      |                      |
  |-- sensor/DEVICE_ID ------> | -- XOR+Base64 -----> |                      |
  |   (AT+MQTTPUBRAW)         |                      | -- XOR解密 --------> |
  |                            |                      | -- JSON解析 -------> |
  |                            |                      | -- 签名验证 -------> |
  |                            |                      | -- 写入DB --------> |
  |                            |                      |                      |
  | <-- cmd/DEVICE_ID -------- | <--- 明文JSON ------ | <--- HTTP POST ----- |
  |   (+MQTT_SUB_RECV)        |                      |                      |
  |                            |                      |                      |
  |-- heartbeat/DEVICE_ID ---> | -- XOR+Base64 -----> | -- 更新在线状态 ---> |
  |                            |                      |                      |
```

---

## 五、配置清单

### 固件配置 (smartrover_uno_test.ino / smartrover_uno.ino)

| 配置项 | 说明 | 示例值 |
|--------|------|--------|
| WIFI_SSID | WiFi名称 | "REDMI K80 Ultra" |
| WIFI_PASS | WiFi密码 | "88888888" |
| MQTT_HOST | Broker地址 | "10.32.44.189" |
| MQTT_PORT | Broker端口 | 1883 |
| DEVICE_ID | 设备ID | "10.32.44.189" |
| DEVICE_SECRET | 设备密钥(PROGMEM) | "72d579dd..." |
| XOR_KEY | XOR加密密钥(PROGMEM) | "SmartRover2026!!" |

### 后端配置 (config.py / 环境变量)

| 配置项 | 说明 | 默认值 |
|--------|------|--------|
| AES_KEY | AES/XOR 密钥(16字节) | b'SmartRover2026!!' |
| MQTT_BROKER_HOST | Broker地址 | "mosquitto" (容器内) |
| MQTT_BROKER_PORT | Broker端口 | 1883 |

**注意**：固件中的 `XOR_KEY` 必须与后端的 `AES_KEY` 一致（后端 XOR_KEY 默认 fallback 到 AES_KEY）。

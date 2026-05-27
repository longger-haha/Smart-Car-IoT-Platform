# SmartRover 🚀

**智能小车物联网控制与安全监控平台**

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Vue](https://img.shields.io/badge/Vue.js-3.0-4FC08D?logo=vue.js)
![Flask](https://img.shields.io/badge/Flask-3.0-000000?logo=flask)
![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?logo=docker)
![MQTT](https://img.shields.io/badge/MQTT-Mosquitto-3C5280)
![ESP32](https://img.shields.io/badge/ESP32-双核-E7352C)

SmartRover 是一个涵盖"**端-云-安全**"架构的综合性物联网（IoT）项目。以 **ESP32 为主控**，结合 Python Flask 后端与 Vue 3 前端，实现智能小车的环境感知、自主避障巡航、远程遥控与实时监控，同时具备完善的网络安全防护能力。

---

## ✨ 核心特性

### 1. 边缘侧车辆控制 (Edge & Hardware)
- **ESP32 本地实时避障（100ms 级响应）**：超声波 + 双红外融合，MPU6050 闭环转向，5 状态有限状态机
- **巡逻式自主巡航**：避障后不恢复原航向，沿新方向继续前进，实现"哪里有路走哪里"
- **多维环境感知**：WiFi 定位、温湿度、超声波测距、红外障碍检测、MPU6050 六轴 IMU
- **安全守护机制**：心跳超时自动停车、电池低压保护、断网降级停车

### 2. 云侧物联网中枢 (Cloud & Platform)
- **高德地图 GIS 联动**：实时显示车辆位置与行驶轨迹，支持轨迹回放
- **实时控制面板**：方向键十字布局远程控制（↑↓←→停），速度三挡可调
- **巡逻式巡航控制**：ESP32 本地自主巡航，后端兜底保护
- **监控仪表盘**：Echarts 动态图表展示传感器历史数据及设备状态
- **传感器数据实时监控**：温湿度、超声波距离、红外状态、IMU 航向、位置坐标

### 3. 全链路安全防护 (Security)
- **零信任设备入网**：设备基于强随机密钥（SecretToken）校验，阻截伪造节点
- **防重放与防篡改**：控制指令附加时间戳与 SHA-256 签名
- **传感数据加密**：AES-128 CBC + HMAC-SHA256
- **RBAC 鉴权拦截**：管理员/普通用户权限划分，JWT 认证
- **安全审计日志**：MQTT 解密失败、签名异常、设备白名单违规全量记录
- **异常流量预警**：频率限制、重放检测、坐标异常跳变检测

---

## 🛠️ 技术栈

| 层级 | 技术 |
|------|------|
| **前端** | Vue 3 (Composition API) + Vite + Element Plus + Echarts + 高德地图 JS API 2.0 |
| **后端** | Python 3.11 + Flask 3.0 + SQLAlchemy 2.0 + Flask-JWT-Extended |
| **数据库** | MySQL 8.0 |
| **消息代理** | Eclipse Mosquitto 2.0 (MQTT) |
| **固件** | C/C++ (Arduino IDE) — `smartrover_esp32.ino` |
| **部署** | Docker + Docker Compose + Nginx Alpine |
| **加密** | AES-128-CBC + HMAC-SHA256 |

---

## 📦 快速部署

本项目使用 Docker Compose 进行一键容器化部署。

### 1. 环境准备
- [Docker](https://docs.docker.com/get-docker/) (>= 20.x)
- [Docker Compose](https://docs.docker.com/compose/install/) (>= 2.x)

### 2. 配置环境变量
```bash
cp .env.example .env
```
编辑 `.env`，填入高德地图 API Key、数据库密码、JWT 密钥等。

### 3. 一键启动
```bash
docker compose up -d --build
```

### 4. 服务端口
| 服务 | 端口 | 说明 |
|------|------|------|
| Web 前端 | `http://localhost:8081` | Vue 3 控制面板 |
| 后端 API | `http://localhost:5000` | Flask REST API |
| MySQL | `localhost:3307` | 数据库 |
| Mosquitto MQTT | `localhost:1883` | MQTT Broker |

### 5. 初始化管理员
```bash
docker exec -it smartrover-backend flask auth init-admin
```
默认账号：`admin` / `admin123`

---

## 📂 项目目录结构

```text
SmartRover/
├── docker-compose.yml              # Docker Compose 服务编排
├── README.md                       # 项目说明
├── .env                            # 环境变量（需自行创建）
│
├── Backend/                        # Python Flask 后端
│   ├── Dockerfile                  # 后端多阶段构建
│   ├── requirements.txt            # Python 依赖
│   ├── app.py                      # WSGI 启动入口
│   ├── config.py                   # 应用配置
│   └── src/
│       ├── models/                 # SQLAlchemy 数据模型
│       ├── routes/                 # API 路由 (auth/vehicle/telemetry/devices/dashboard)
│       ├── services/               # 核心业务模块
│       │   ├── navigation_engine.py      # 导航决策引擎
│       │   ├── pid_controller.py         # PID 航向控制器
│       │   ├── imu_fusion.py             # IMU 互补滤波
│       │   └── obstacle_avoidance.py     # 避障决策引擎
│       └── utils/                  # 工具模块
│           ├── mqtt_client.py            # MQTT 长连接客户端
│           ├── crypto_tool.py            # AES/XOR 加解密
│           ├── cruise_security.py        # 巡航安全检测
│           └── auth_interceptor.py       # JWT 鉴权 & 防重放
│
├── Frontend/                       # Vue 3 前端
│   ├── Dockerfile                  # Nginx Alpine 多阶段构建
│   ├── nginx.conf                  # Nginx 反向代理配置
│   └── src/
│       ├── api/                    # Axios API 封装
│       ├── views/
│       │   ├── dashboard/          # 监控仪表盘
│       │   ├── device_center/      # 设备管理中心
│       │   ├── control_panel/      # 控制面板（手动/巡航）
│       │   └── audit_logs/         # 安全审计日志
│       └── components/             # 公共组件
│
├── Firmware/                       # 硬件固件
│   └── smartrover_esp32/           # ESP32 固件（本地避障+巡逻巡航）
│
├── Deploy_Config/                  # 部署配置
│   └── mosquitto/config/           # Mosquitto MQTT 配置
│
└── Docs/                           # 项目文档
    ├── sql/                        # 数据库初始化与迁移 SQL
    ├── 操作与巡航指南.md             # 小车操作手册
    ├── 混合架构完整方案.md           # 未来架构演进规划
    ├── ESP32接线图.md              # 硬件接线参考
    └── Platform_Project_Structure.md  # 开发规范
```

---

## 🏗️ 系统架构

```
┌─────────────────────────────────────────────────────────────────┐
│                         前端 (Vue 3)                             │
│  控制面板 │ 仪表盘 │ 设备中心 │ 审计日志 │ 高德地图轨迹            │
└──────────────────────────┬──────────────────────────────────────┘
                           │ HTTP (Axios)
┌──────────────────────────▼──────────────────────────────────────┐
│                      后端 (Flask)                                │
│  ┌────────────┐ ┌──────────────┐ ┌────────────────────────────┐ │
│  │ REST API   │ │ MQTT Client  │ │  导航引擎 / 避障决策        │ │
│  │ (JWT鉴权)  │ │ (长连接监听) │ │  PID / IMU / 安全停车       │ │
│  └────────────┘ └──────┬───────┘ └────────────────────────────┘ │
│                        │                │                         │
│                   MySQL 8.0    Mosquitto MQTT Broker            │
└──────────────────────────┼──────────────────────────────────────┘
                           │ WiFi / MQTT
┌──────────────────────────▼──────────────────────────────────────┐
│                   硬件层 (ESP32)                                  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │  ESP32 (双核处理器)                                       │  │
│  │  · Core 0: WiFi/MQTT 通信                                │  │
│  │  · Core 1: 本地实时避障 (100ms, 5状态状态机)              │  │
│  │  · MPU6050 闭环转向                                       │  │
│  │  · WiFi 扫描定位 + AES-128 CBC 加密                      │  │
│  │  · 74HC595 + LEDC PWM 差速驱动                           │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                  │
│  传感器: DHT11 · HC-SR04 · 红外×2 · MPU6050 · ADC(电池)         │
│  电源: 7.4V 电池 · LM2596(→5V) · 3.3V降压模块 · 电池直连电机         │
│  执行器: L293D × 4 直流电机（四轮差速驱动）                      │
└──────────────────────────────────────────────────────────────────┘
```

---

## 🔒 安全机制

### 通信安全
- **设备认证**：设备必须在平台注册并获得唯一 `device_secret`
- **数据加密**：AES-128-CBC + HMAC-SHA256
- **防重放**：控制指令携带时间戳 + Nonce，超时指令自动拒绝
- **频率限制**：每分钟最多下发 10 次路线，防止滥用

### 行车安全（双层避障）
| 层级 | 执行者 | 响应时间 | 触发条件 | 动作 |
|------|--------|----------|----------|------|
| L1 本地 | ESP32 固件 | < 100ms | 超声波 < 紧急距离 | 立即停车 + 后退 |
| L1 本地 | ESP32 固件 | < 100ms | 超声波 < 安全距离 | MPU6050 闭环转向避障 |
| L2 后端 | Flask 导航引擎 | ~2s | 超声波 < 5cm | 下发 stop（兜底） |

---

## 🚗 操作模式

### 手动控制
- 方向键十字布局（↑前 ↓后 ←左转 →右转 中停止）
- 三挡速度可调（低速180 / 中速200 / 高速255 PWM）

### 巡逻式自动巡航
- 无航点，持续直行 + 自动避障绕行
- 避障后沿新方向继续前进，不恢复原航向
- 前端可实时调节避障参数（紧急距离/安全距离/转向角度等）

---

## 📖 相关文档

- [操作与巡航指南](Docs/操作与巡航指南.md) — 小车操作手册
- [ESP32 接线参考](Docs/ESP32接线图.md) — 硬件引脚接线图
- [项目结构规范](Docs/Platform_Project_Structure.md) — 开发规范与目录约定
- [混合架构规划](Docs/混合架构完整方案.md) — 未来架构演进方案

---

## 📜 许可证

本项目采用 [MIT License](LICENSE) 开源协议。

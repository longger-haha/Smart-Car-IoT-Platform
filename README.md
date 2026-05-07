# SmartRover 🚀
**智能小车全栈系统与物联网安全监控平台**

![License](https://img.shields.io/badge/License-MIT-blue.svg)
![Vue](https://img.shields.io/badge/Vue.js-3.0-4FC08D?logo=vue.js)
![Flask](https://img.shields.io/badge/Flask-3.0-000000?logo=flask)
![Docker](https://img.shields.io/badge/Docker-Compose-2496ED?logo=docker)
![MQTT](https://img.shields.io/badge/MQTT-Mosquitto-3C5280)

SmartRover 是一个涵盖“**端-云-安全**”架构的综合性物联网（IoT）项目。它不仅提供对智能小车（基于 Arduino / ESP32）的环境感知、自动驾驶决策与运动执行控制，还通过一套基于 Vue 3 和 Python Flask 的 Web 平台，实现了 GIS 地理联动、多源传感大盘展示，以及严密的网络安全攻防演练与防护机制。

---

## ✨ 核心特性

### 1. 边缘侧车辆控制 (Edge & Hardware)
- **多维环境感知**：集成 GPS定位、温湿度、超声波避障传感器。
- **平顺运动控制**：支持基于 PID 和模糊逻辑的自动避障巡航。
- **底层执行驱动**：通过 PWM 信号精确控制电机转速与转向伺服。

### 2. 云侧物联网中枢 (Cloud & Platform)
- **GIS 地理联动**：集成高德地图 API，实时显示车辆位置并支持**可视化巡航路线规划下发**。
- **实时控制面板**：低延迟的远程方向盘控制（前进、后退、转向、急停）。
- **监控仪表盘**：动态图表（Echarts）展示传感器历史数据及设备状态。

### 3. 全链路安全防护 (Security)
- **零信任设备入网**：设备接入基于强随机密钥（SecretToken）校验，阻截伪造节点。
- **防重放与防篡改**：核心控制指令附加时间戳与 SHA-256 签名，拒绝陈旧与恶意篡改报文。
- **传感数据加密**：敏感位置数据通过 AES 加密传输，后台自带解密审计查验日志。
- **RBAC 鉴权拦截**：完善的管理员/普通用户权限划分，阻止越权调用接口。
- **异常流量预警**：防止 DDoS 和泛洪攻击，保障 MQTT 代理及 API 的稳定性。

---

## 🛠️ 技术栈

* **前端 (Frontend)**：Vue 3 + Vite + Element Plus + Echarts + 高德地图 JS API
* **后端 (Backend)**：Python 3.12 + Flask + SQLAlchemy + Flask-JWT-Extended
* **中间件 & 数据库**：MySQL 8.0 + Eclipse Mosquitto (MQTT)
* **硬件端 (Firmware)**：C/C++ (Arduino IDE) 适配 ESP32 / Arduino 架构
* **部署 (Deployment)**：Docker + Docker Compose

---

## 📦 快速部署指南

本项目推荐使用 Docker Compose 进行一键化容器部署。

### 1. 环境准备
确保您的服务器或本地机器已安装：
* [Docker](https://docs.docker.com/get-docker/) (>= 20.x)
* [Docker Compose](https://docs.docker.com/compose/install/) (>= 2.x)
* [Node.js](https://nodejs.org/) (仅用于编译前端)

### 2. 配置环境变量
克隆项目后，首先配置环境变量：
```bash
# 从模板复制环境变量文件
cp .env.example .env
```
编辑 `.env` 文件，填入高德地图的 API Key 以及各种随机密码（可通过系统自带的随机生成脚本获取）。

### 3. 构建前端资源
因为 `docker-compose` 里的 Nginx 服务会直接使用前端编译好的产物，请先在本地构建：
```bash
cd Frontend
npm install
npm run build
cd ..
```

### 4. 一键启动服务
```bash
docker-compose up -d --build
```
启动完成后，包含的四个服务会运行在以下本地端口：
- **Web 前端页面**: `http://localhost:8081`
- **后端 API 服务**: `http://localhost:5000`
- **MySQL 数据库**: `localhost:3307`
- **MQTT Broker**: `localhost:1883`

### 5. 初始化管理员账号
项目不会在启动时自动创建默认管理员，请使用内置的 CLI 命令初始化：
```bash
docker exec -it smartrover-backend flask auth init-admin
```
初始化成功后，可使用 `admin` / `admin123` 登录平台。

---

## 📂 项目目录结构

```text
SmartRover/
├── Backend/           # Python Flask 后端 (REST API + MQTT 业务逻辑)
├── Frontend/          # Vue 3 前端页面 (控制台、地图监控、数据大盘)
├── Firmware/          # ESP32/Arduino 硬件固件源码
├── Docs/              # 项目详细规划文档与 SQL 初始化脚本
├── Deploy_Config/     # Mosquitto 等中间件服务的部署配置文件
├── Scripts_Mock/      # 模拟设备发包的 Python 脚本 (免硬件调试)
├── docker-compose.yml # 容器化编排文件
└── .env.example       # 环境变量参考模板
```

---

## 📜 许可证

本项目采用 [MIT License](LICENSE) 开源协议。

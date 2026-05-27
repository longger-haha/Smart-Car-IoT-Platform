# SmartRover 平台级开发与项目结构规范文档
> **制定日期**: 2026-04-26
> **说明**: 本文档作为后续 Python、Vue 及联调实施的最终架构约定。前后端各自的开发人员必须严格遵循此目录隔离和模块化标准。

## 1. 整体工程拓扑与技术栈约定

本项目采取典型的**前后端绝对分离**架构。基于《安全开发宪章》，不仅是代码文件上的分离，更是功能职责上的纯净度分离：前端只管美观呈现和调用；后端（网关）收束所有拦截、解密、打签名的脏活累活。

- **前端 (Frontend)**: Vue 3 (Composition API) + Router + element-plus + Axios + Echarts (图表) + amap (高德定位解析)。
- **后端 (Backend)**: Python 3 (Flask / FastAPI) + JWT Token + PyCryptodome (加解密组件) + Flask-SQLAlchemy (MySQL 连接层)。
- **消息代理 (Message Broker)**: Eclipse Mosquitto (需配置 MQTTS 证书以保障安全底线)。
- **底层控制器 (Edge)**: Arduino C++ 或 MicroPython，搭载 MQTT Client 库。

---

## 2. 标准项目根目录结构规划 (Project Tree)

> 最后更新：2026-05-23。当前项目使用 ESP32 作为主控。

请全员将代码严格放置在对应层级：

```text
SmartRover/ (项目根目录)
├── docker-compose.yml               # Docker Compose 服务编排 (mysql/mosquitto/backend/frontend)
├── README.md                        # 项目说明
├── .env                             # 环境变量 (高德Key、数据库密码、JWT密钥等)
│
├── Frontend/                        # Vue 3 前端应用
│   ├── Dockerfile                   # Nginx Alpine 多阶段构建
│   ├── nginx.conf                   # Nginx 反向代理配置 (API proxy_pass + WebSocket)
│   ├── package.json
│   ├── vite.config.js               # Vite 配置 (代理设置解决跨域)
│   └── src/
│       ├── api/                     # Axios API 封装 (auth/device/telemetry/vehicle/audit/dashboard)
│       ├── assets/                  # 静态资源
│       ├── components/              # 共用组件 (Layout 布局)
│       ├── router/                  # 路由表 (含 JWT 鉴权页面拦截)
│       ├── utils/                   # 工具类 (时间格式化等)
│       └── views/
│           ├── dashboard/           # 监控仪表盘 (传感器图表 + 设备状态)
│           ├── device_center/       # 设备管理中心 (注册/列表/详情/遥测趋势)
│           ├── control_panel/       # 控制面板 (手动方向键 + 巡逻巡航 + 避障参数配置)
│           └── audit_logs/          # 安全审计日志展示
│
├── Backend/                         # Python Flask 后端
│   ├── Dockerfile                   # Python 3.11 多阶段构建 + Gunicorn
│   ├── requirements.txt             # Python 依赖清单
│   ├── app.py                       # WSGI 启动主入口
│   ├── config.py                    # 应用配置 (MySQL/MQTT/JWT/导航引擎参数)
│   └── src/
│       ├── models/                  # SQLAlchemy 数据模型
│       │   ├── user.py, device.py, telemetry.py
│       │   ├── navigation_event.py, audit_log.py
│       ├── routes/                  # API 路由 (auth/vehicle/devices/telemetry/dashboard/audit)
│       ├── services/                # 核心业务模块
│       │   ├── navigation_engine.py      # 导航决策引擎 (兜底保护)
│       │   ├── pid_controller.py         # PID 航向控制器
│       │   ├── imu_fusion.py             # IMU 互补滤波
│       │   └── obstacle_avoidance.py     # 避障决策引擎
│       └── utils/                  # 工具模块
│           ├── mqtt_client.py            # MQTT 长连接 (自动识别AES/XOR/PLAIN加密)
│           ├── crypto_tool.py            # AES/XOR 加解密 + HMAC/XOR 签名
│           ├── cruise_security.py        # 巡航安全 (频率限制/重放检测/评分)
│           ├── auth_interceptor.py       # JWT 鉴权 + 设备归属校验
│           └── heartbeat_checker.py      # 设备心跳离线检测
│
├── Firmware/                        # 硬件固件代码
│   └── smartrover_esp32/            # ESP32 固件
│       └── smartrover_esp32.ino     # 双核: 本地避障(100ms) + 巡逻巡航
│
├── Deploy_Config/                   # 部署中间件配置
│   └── mosquitto/config/            # Mosquitto MQTT Broker 配置
│
└── Docs/                            # 项目文档
    ├── sql/                         # SQL 脚本 (init_struct + 迁移脚本)
    ├── 操作与巡航指南.md             # 小车操作手册 (当前主要参考)
    ├── 混合架构完整方案.md           # 未来演进规划 (ESP32 全自主方案)
    ├── ESP32接线图.md               # ESP32 引脚接线参考
    └── Platform_Project_Structure.md  # 本文档
```

## 3. 开发落地顺序与指导 (Execution Order)

1. **库表建构作为起点**：使用上面提到的 `init_struct.sql` 在 MySQL 中建立核心表（用户、设备白名单、告警日志）。这样后端的接口才有持久层可对接。
2. **前后端接口对齐**：使用 `Scripts_Mock/mock_sensor.py` 向前跑通数据流，前端把假数据的大屏及地图坐标展现出来（这时候整个平台雏形就活了）。
3. **注入加解密屏障**：开发 `Backend_Flask/src/utils/crypto_tool.py`，改写 Mock 脚本使其全密文发包。此时开始攻防调测，测试拦截是否有效。
4. **嵌入物理实体车辆**：前置测试皆在掌控内之后，C同学的 Arduino 真机才允许最后介入网络接受指令与返回真实信息。
5. **云端 Docker 自动化集装**：进入部署环节后，使用工程根目录下的 `docker-compose up -d` 即可在 Ubuntu 服务器上以互相通信的容器簇（Frontend / Backend / MQTT / MySQL）形态优雅上线。

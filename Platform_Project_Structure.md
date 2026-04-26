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

请全员将代码严格放置在对应层级：

```text
SmartRover/ (项目根目录)
├── docker-compose.yml               <--- [新增部署编排] 一键拉起云端服务簇
├── .env                             <--- [新增部署编排] 存放机密环境变量与密码
│
├── Frontend/                        <--- B同学：前端大屏与 Web 面板主控区
│   ├── Dockerfile                   <--- [新增容器化] Vue 多阶段构建脚本
│   ├── nginx.conf                   <--- [新增容器化] Nginx 代理与伪静态配置
│   ├── package.json
│   ├── vite.config.js               # 代理设置，彻底解决跨域
│   ├── public/
│   └── src/
│       ├── api/                     # 请求层 (集中管理对 Python 的 Axios 封装 API)
│       ├── assets/                  # 静态资源 (Logo、背景等)
│       ├── components/              # Vue 共用组件 (例如：红色爆闪告警灯组件)
│       ├── router/                  # 路由表 (处理后台鉴权页面拦截)
│       ├── utils/                   # 工具类 (前端哈希处理协助库)
│       └── views/
│           ├── dashboard/           # 大盘首屏 (温湿度图表与定位地图)
│           ├── device_center/       # 资产中心 (入网设备列表与秘钥分发)
│           ├── control_panel/       # 反控重地 (操控摇杆)
│           └── audit_logs/          # 防御全景 (记录阻断日志展示与透明加解密对比视窗)
│
├── Backend/                         <--- A 同学：Python 核心鉴权与路由分法区
│   ├── Dockerfile                   <--- [新增容器化] Python 环境及 Gunicorn 运行栈
│   ├── requirements.txt             # 依赖清单
│   ├── app.py                       # WSGI 启动主入口程序
│   ├── config.py                    # 配置文件 (MySQL 连接字符串、秘钥盐值)
│   ├── src/
│   │   ├── models/                  # SQLAlchemy 数据库映射模型 (模型包含 User, Device, Telemetry, CyberAttackLog)
│   │   ├── routes/                  # 接口控制器 (分发如 /api/auth, /api/devices, /api/telemetry 等视图)
│   │   ├── services/                # 沉重的核心逻辑层 (例如 MQTT 的下行推送动作、日志持久化存入逻辑)
│   │   └── utils/
│   │       ├── crypto_tool.py       # (关键安全库)：用 Python 实现的 AES 和 SHA-256 打签和解签轮子
│   │       ├── mqtt_client.py       # paho-mqtt 连接实例，负责长驻内存监听硬件数据
│   │       └── auth_interceptor.py  # (关键网关库)：基于 JWT 和短窗口的防重放与防 DDos 拦截器
│   │
├── Deploy_Config/                   <--- [新增] 部署挂载重地：服务持久化配置区
│   ├── mosquitto/                   # 映射到宿主机的 MQTT 参数与 SSL 证书
│   └── mysql/                       # 映射 MySQL 数据卷防重启丢失
│
├── Edge_Arduino/                    <--- C 同学：边缘执行器代码集
│   ├── SmartRover.ino               # 核心执行控制逻辑 (包含 PID 和传感器拉取循环)
│   ├── network_layer.h              # 和云端的 MQTT/TLS 通信模块
│   └── security_crypto.h            # 基于 C++ 的软加解密依赖轮子
│
├── Scripts_Mock/                    <--- 测试与演习攻击套件 (宪章强制要求项)
│   ├── mock_sensor.py               # 虚假发包脚本：在无 Arduino 的情况下模拟环境坐标发往后端。
│   ├── replay_attack_sim.py         # 红蓝对抗脚本：专门尝试发送过期数据以触发平台大屏亮起红灯。
│   └── ddos_pam.py                  # Ddos 模拟脚本：高频猛点向后台发数据，打出触发阻断的效果。
│
└── Docs/
    ├── sql/
    │   └── init_struct.sql          # MySQL MySQL 的建库和建表语句
    ├── Platform_Functional_Requirements.md
    └── IoT_Project_Plan.md
```

## 3. 开发落地顺序与指导 (Execution Order)

1. **库表建构作为起点**：使用上面提到的 `init_struct.sql` 在 MySQL 中建立核心表（用户、设备白名单、告警日志）。这样后端的接口才有持久层可对接。
2. **前后端接口对齐**：使用 `Scripts_Mock/mock_sensor.py` 向前跑通数据流，前端把假数据的大屏及地图坐标展现出来（这时候整个平台雏形就活了）。
3. **注入加解密屏障**：开发 `Backend_Flask/src/utils/crypto_tool.py`，改写 Mock 脚本使其全密文发包。此时开始攻防调测，测试拦截是否有效。
4. **嵌入物理实体车辆**：前置测试皆在掌控内之后，C同学的 Arduino 真机才允许最后介入网络接受指令与返回真实信息。
5. **云端 Docker 自动化集装**：进入部署环节后，使用工程根目录下的 `docker-compose up -d` 即可在 Ubuntu 服务器上以互相通信的容器簇（Frontend / Backend / MQTT / MySQL）形态优雅上线。

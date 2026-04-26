<!--
Sync Impact Report:
- Version change: 1.0.0 -> 1.0.1
- Modified principles: 翻译所有宪章内容为中文 (Translated all constitution content to Chinese).
- Added sections: N/A
- Removed sections: N/A
- Templates requiring updates: N/A
- Follow-up TODOs: None
-->

# SmartRover 项目开发宪章 (Constitution)

## 核心原则 (Core Principles)

### I. 安全第一架构 (Security-First Architecture)
边缘车辆节点与云端平台之间的所有通信**必须**经过加密或密码学签名。**严禁**未经身份验证的匿名设备接入 MQTT 服务器。系统**必须**假定网络环境已被攻陷，并通过设备 IAM（身份与访问管理）令牌与带时间戳的签名（防重放机制）来验证每个数据包。

### II. 模拟先于集成 (Simulation Before Integration)
在接入物理 Arduino/ESP32 硬件之前，Web 平台组件与后端 API **必须**先使用软件级模拟脚本（例如：利用 Python 模拟传感器发包）进行完整的开发与测试。这一原则确保了纯软件层面的 Bug 不会与硬件电路/逻辑问题混淆排查。

### III. 边缘失效保护 (Fail-Safe Edge Control)
所有运行在 Arduino 端的实体执行机构（如转向舵机、PWM 电机控制）**必须**内置安全边界机制。本地主控板的本地安全触发器（例如：超声波雷达探测到前方距离障碍物小于 5cm）的优先级**必须高于**任何来自云端的远程指令。必要时，边缘端需能独立执行紧急刹车等避险动作。

### IV. 严格的架构解耦 (Strict Architectural Decoupling)
Vue3 前端（负责展示与地图模块）、Python Flask 后端（负责身份验证、安全审计验证、REST API 接口）、Mosquitto 服务器（负责遥测数据路由传递）以及 Arduino 节点（负责边缘感知计算与 PID 执行）**必须**保持完全解耦。跨系统之间的通信**仅允许**依赖于标准的 MQTT 数据包与 RESTful API 端点。

## 安全与硬件规范 (Security & Hardware Specifications)

SmartRover 项目必须在以下不可妥协的技术栈约束下进行开发：
- **Web 应用前端**: Vue 3 + Element Plus。必须能在前端直观呈现安全攻防事件（如：展示被拦截的数据包、触发限流时的明显提示），以为答辩防御审计提供证明。
- **后端服务端**: Python Flask。负责处理设备级 JWT 令牌生成、频率限制（防 DDoS）以及对有效载荷进行解密/验签。
- **数据库与持久化**: MySQL。作为系统的核心存储底座，严格负责设备鉴权数据（白名单）、历史运动轨迹以及安全攻击防御日志的持久化备份。
- **硬件控制器**: Arduino / ESP 系列开发板。底层代码必须采用 PID 算法来实现平滑的电机控制，并采用模糊逻辑 (Fuzzy Logic) 算法应对动态的避障巡航任务。
- **网络通信**: MQTT over TLS。车辆的定位坐标 (GPS) 与控制下发指令序列在任何情况下都**绝对禁止**明文传输。

## 开发与集成工作流 (Development Workflow)

1. **第一阶段 (基础数据流打通)**: 建立最基础的数据链路。实现“虚假模拟脚本 -> MQTT -> 后端 -> 前端”的闭环。确保 Web 地图的定位更新及遥控指令数据能正确路由。
2. **第二阶段 (实体控制与调优)**: 在 Arduino 硬件上实现并精细调优 PID 算法与 PWM 电机驱动层代码。确保 Web 端的遥控指令能精确映射为真实车轮的物理运动。
3. **第三阶段 (注入安全体系)**: 全面实装“零信任”安全层。加入数据负载加密 (AES)、SHA-256 完整性签名校验，以及基于角色的访问权限控制 (RBAC)。
4. **第四阶段 (对抗验证)**: 通过主动使用 Wireshark 或伪造脚本发起数据包欺骗/重放攻击，检验安全防护体系。必须确保控制大屏能正确记录攻击日志并成功阻断入侵。

## 开发治理机制 (Governance)

本《开发宪章》的优先级高于项目中的任何其他既定实践。任何试图偏离核心技术栈的行为（例如：试图用 Java Spring 替换 Python/Vue，或试图移除 TLS 加密协议）都**必须**通过对本宪章的正式修订案方可执行。
所有功能模块的代码合并请求，都**必须**证明其完全遵守了“安全第一”与“边缘失效保护”两项核心原则，否则不予认定完工。

**版本号 (Version)**: 1.0.1 | **签署日期 (Ratified)**: 2026-04-26 | **最后修订 (Last Amended)**: 2026-04-26

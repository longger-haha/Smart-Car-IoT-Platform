# SmartRover 三人 AI 协作开发指南

> **版本**: v1.0 | **制定日期**: 2026-04-27  
> **适用项目**: SmartRover IoT 安全平台  
> **仓库地址**: https://github.com/longger-haha/Smart-Car-IoT-Platform

---

## 一、角色分工总览

| 代号 | 职责方向 | 主要负责目录 | 不可操作目录 |
|------|---------|------------|------------|
| **A 同学** | 后端 Python (Flask + 安全机制) | `Backend/src/` | `Frontend/src/` |
| **B 同学** | 前端 Vue3 (大屏 + 交互) | `Frontend/src/views/` | `Backend/src/` |
| **C 同学** | 脚本 + 硬件 (Mock 攻防 + Arduino) | `Scripts_Mock/`, `Edge_Arduino/` | 前后端 src |

> **黄金原则**: 按分工领域操作文件，天然无冲突。`api.yaml` 接口契约是 A 和 B 之间唯一的沟通边界。

---

## 二、必读文档速查

开发前，每人都必须浏览一遍以下文档（5 分钟内可读完）：

| 文档 | 位置 | 必读理由 |
|------|------|---------|
| 项目结构规范 | `Platform_Project_Structure.md` | 了解整体目录约定 |
| 技术实施计划 | `specs/dev/plan.md` | 了解技术栈与开发阶段 |
| **任务清单** | `specs/dev/tasks.md` | ⭐ 最重要，每天对照它工作 |
| 数据库设计 | `specs/dev/data-model.md` | A 同学必读，B/C 了解 |
| **接口契约** | `specs/dev/contracts/api.yaml` | A 和 B 都必须精读 |
| 启动指南 | `specs/dev/quickstart.md` | 搭建本地环境时参考 |
| 开发宪章 | `.specify/memory/constitution.md` | 了解不可逾越的技术底线 |

---

## 三、第一次环境搭建（所有人执行一次）

### 3.1 克隆仓库

```bash
git clone https://github.com/longger-haha/Smart-Car-IoT-Platform.git
cd Smart-Car-IoT-Platform
git checkout dev
```

### 3.2 配置环境变量

```bash
# 复制模板为真实配置文件（此文件已被 .gitignore，不会提交）
cp .env.example .env
```

用文本编辑器打开 `.env`，填写以下关键字段：

```env
FLASK_SECRET_KEY=随机字符串（自己取，如：sr2026xK9!mN）
JWT_SECRET_KEY=另一个随机字符串
MYSQL_PASSWORD=srpass123
MQTT_PASSWORD=（暂时留空或与 A 同学确认）
AES_KEY=SmartRover2026!!
AMAP_API_KEY=（向 A 同学索取，或自行在高德官网申请免费 Key）
```

### 3.3 A 同学：启动本地 Docker 基础设施

> B 同学和 C 同学不需要自己运行 Docker，开发期间连 A 同学的数据库和 MQTT 即可。

```bash
# A 同学在自己电脑上执行（需要安装 Docker Desktop）
docker-compose up -d mysql mosquitto

# 验证是否启动成功
docker-compose ps
# 应看到 mysql 和 mosquitto 都是 Up 状态

# 初始化数据库（只需执行一次）
docker exec -i smartrover_mysql mysql -u sruser -psrpass123 smartrover_db < Docs/sql/init_struct.sql
```

### 3.4 各自安装开发依赖

**A 同学（后端）**：
```bash
cd Backend
python -m venv venv
# Windows:
venv\Scripts\activate
# Mac/Linux:
source venv/bin/activate

pip install -r requirements.txt
python app.py
# → 后端运行在 http://localhost:5000/api/health
```

**B 同学（前端）**：
```bash
cd Frontend
npm install
npm run dev
# → 前端运行在 http://localhost:5173
```

**C 同学（脚本）**：
```bash
# 在项目根目录创建脚本虚拟环境
cd Scripts_Mock
python -m venv venv
venv\Scripts\activate
pip install paho-mqtt pycryptodome
```

---

## 四、每日工作流程（所有人遵循）

### Step 1：每天开始前先同步

```bash
git pull origin dev
```

> 如果有冲突，立刻在微信群告知，不要强行 push。

### Step 2：认领任务

打开 `specs/dev/tasks.md`，找到下一个 `- [ ]` 的未认领任务，将其修改为 `- [/]` 并在后面加上你的名字：

```markdown
# 认领前
- [ ] T019 [US1] 创建 Backend/src/routes/auth.py Blueprint

# 认领后（A 同学）
- [/] T019 [US1] [A同学] 创建 Backend/src/routes/auth.py Blueprint
```

然后立即 commit 并 push，告知别人这个任务你在做：

```bash
git add specs/dev/tasks.md
git commit -m "chore: A同学认领 T019"
git push origin dev
```

### Step 3：启动 AI 开始编码

**向 AI 发送的标准提示词模板（直接复制使用）**：

**A 同学模板**：
```
我在做 SmartRover IoT 安全平台的后端开发。
技术栈：Python Flask 3.x，SQLAlchemy，Flask-JWT-Extended。

【数据库结构】（粘贴 specs/dev/data-model.md 内容）

【接口契约】（粘贴 specs/dev/contracts/api.yaml 中对应部分）

当前任务：[粘贴任务描述，比如 "实现 POST /api/auth/login"]
文件路径：Backend/src/routes/auth.py

请帮我实现这个文件，代码需要符合 Flask Blueprint 规范，
JWT 用 Flask-JWT-Extended，数据库操作用 SQLAlchemy ORM。
```

**B 同学模板**：
```
我在做 SmartRover IoT 安全平台的前端开发。
技术栈：Vue3 Composition API，Element Plus，Axios。

【接口说明】
- 登录接口：POST /api/auth/login，请求体 {username, password}，
  返回 {access_token, role}
- Axios 已封装在 src/api/index.js，直接调用 authAPI.login(user,pass)
- 路由守卫已配置，登录后跳转 /dashboard

当前任务：[粘贴任务描述]
文件路径：Frontend/src/views/LoginView.vue

要求：使用 Element Plus 组件，深色主题，简洁现代的设计风格。
```

**C 同学模板**：
```
我在做 SmartRover IoT 平台的模拟测试脚本开发。
MQTT Broker：localhost:1883（账号：smartrover，密码：见.env）
加密方式：AES-128 CBC，密钥 SmartRover2026!!（16字节）
数据格式参考：（粘贴 data-model.md 中 telemetry_points 表结构）

当前任务：[粘贴任务描述]
文件路径：Scripts_Mock/mock_sensor.py

要求：每秒发送一次，使用 paho-mqtt，加密用 pycryptodome。
```

### Step 4：完成后提交

```bash
git add .
git commit -m "feat(T019): 实现登录接口 auth.py - A同学"
git push origin dev
```

**commit 消息规范**：
```
feat(T任务号): 简短描述 - 你的名字
fix(T任务号): 修复描述 - 你的名字
chore: 非代码改动描述
```

### Step 5：在 tasks.md 标记完成

```bash
# 把 [/] 改为 [x]
- [x] T019 [US1] [A同学] 创建 Backend/src/routes/auth.py Blueprint
git add specs/dev/tasks.md
git commit -m "chore: T019 完成"
git push origin dev
```

---

## 五、任务分配详表

### Phase 2：核心基础设施（三人合作，优先完成）

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T009 建库 SQL | **A** | `Docs/sql/init_struct.sql` |
| T010 User 模型 | **A** | `Backend/src/models/user.py` |
| T011 Device 模型 | **A** | `Backend/src/models/device.py` |
| T012 Telemetry 模型 | **A** | `Backend/src/models/telemetry.py` |
| T013 AuditLog 模型 | **A** | `Backend/src/models/audit_log.py` |
| T014 加解密工具 | **A** | `Backend/src/utils/crypto_tool.py` |
| T015 防重放拦截器 | **A** | `Backend/src/utils/auth_interceptor.py` |
| T016 docker-compose | **A** | `docker-compose.yml` |
| T017 Mosquitto 配置 | **C** | `Deploy_Config/mosquitto/config/mosquitto.conf` |
| T018 MQTT 客户端 | **A** | `Backend/src/utils/mqtt_client.py` |

### Phase 3：US1 设备鉴权

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T019 登录接口 | **A** | `Backend/src/routes/auth.py` |
| T020 设备管理接口 | **A** | `Backend/src/routes/devices.py` |
| T021 MQTT 白名单过滤 | **A** | 在 `mqtt_client.py` 里补充 |
| T022 登录页面 | **B** | `Frontend/src/views/LoginView.vue` |
| T023 设备中心页面 | **B** | `Frontend/src/views/device_center/DeviceCenterView.vue` |

### Phase 4：US2 实时监控

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T024 遥测数据接口 | **A** | `Backend/src/routes/telemetry.py` |
| T025 地图 Dashboard | **B** | `Frontend/src/views/dashboard/DashboardView.vue` |
| T026 传感器图表组件 | **B** | `Frontend/src/views/dashboard/SensorCharts.vue` |
| T027 告警阈值逻辑 | **B** | 在 `SensorCharts.vue` 里补充 |
| T028 Mock 传感器脚本 | **C** | `Scripts_Mock/mock_sensor.py` |

### Phase 5：US3 远程反控

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T029 指令下发接口 | **A** | `Backend/src/routes/vehicle.py` |
| T030 指令服务层 | **A** | `Backend/src/services/command_service.py` |
| T031 遥控摇杆页面 | **B** | `Frontend/src/views/control_panel/ControlPanelView.vue` |
| T032 越权弹窗逻辑 | **B** | 在 `ControlPanelView.vue` 里补充 |

### Phase 6：US4 安全审计

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T033 防重放检测完善 | **A** | 完善 `auth_interceptor.py` |
| T034 审计日志接口 | **A** | `Backend/src/routes/audit.py` |
| T035 攻防大屏页面 | **B** | `Frontend/src/views/audit_logs/AuditLogsView.vue` |
| T036 密文审计视窗 | **B** | 在 `AuditLogsView.vue` 里补充 |
| T037 重放攻击脚本 | **C** | `Scripts_Mock/replay_attack_sim.py` |
| T038 DDoS 模拟脚本 | **C** | `Scripts_Mock/ddos_pam.py` |

### Phase 7：Docker 部署（最终阶段，A 同学主导）

| 任务 | 负责人 | 说明 |
|------|--------|------|
| T039 前端 Dockerfile | **B** | `Frontend/Dockerfile` |
| T040 Nginx 配置 | **B** | `Frontend/nginx.conf` |
| T041 后端 Dockerfile | **A** | `Backend/Dockerfile` |
| T042 完善 compose | **A** | `docker-compose.yml` 全部填写 |
| T043 Mosquitto TLS | **C** | `Deploy_Config/mosquitto/config/` |
| T044 全流程验证 | **全员** | 按 quickstart.md 联调 |
| T045 更新 README | **A** | `README.md` |

---

## 六、前后端联调规范

> **核心原则**：A 同学完成一个接口后即可通知 B 同学联调，无需等待全部接口完成。

### 联调步骤

1. **A 同学通知**：在群里发送 "T024 遥测接口已完成，可以联调，测试地址 GET /api/telemetry/latest/device001"
2. **B 同学验证**：用浏览器或 Postman 请求接口，确认返回格式与 api.yaml 一致
3. **B 同学开发**：照着 api.yaml 的格式写前端代码，先 Mock 数据调通界面逻辑
4. **B 同学联调**：把 Mock 数据替换为真实 API 调用，在本地观察效果

### 接口测试快速命令（无需安装任何工具）

```bash
# 登录获取 Token（先运行这个）
curl -X POST http://localhost:5000/api/auth/login \
  -H "Content-Type: application/json" \
  -d "{\"username\":\"admin\",\"password\":\"admin123\"}"

# 用拿到的 Token 请求设备列表（替换 <TOKEN>）
curl http://localhost:5000/api/devices \
  -H "Authorization: Bearer <TOKEN>"
```

---

## 七、冲突处理预案

| 场景 | 解决方案 |
|------|---------|
| 两人同时修改了 `tasks.md` | pull 后手动合并，标记各自认领的任务 |
| A 修改了 API 返回格式 | 必须先更新 `contracts/api.yaml`，并在群里通知 B 同学 |
| B 发现接口与 api.yaml 不符 | 直接找 A 同学，以 api.yaml 为准修复后端 |
| 代码产生 git 冲突 | **不要 `git push --force`！** 在群里协商后手动解决 |

---

## 八、答辩前最终检查清单

```
- [ ] docker-compose up -d 能一键启动所有服务
- [ ] 登录页能正常登录，Admin 和 Guest 两种账号均可用
- [ ] Dashboard 大屏地图有车辆动态更新（运行 mock_sensor.py）
- [ ] 摇杆面板 Admin 可控、Guest 被拦截并显示弹窗
- [ ] Audit Logs 页能实时展示告警
- [ ] replay_attack_sim.py 触发平台红色告警
- [ ] ddos_pam.py 触发限流日志
- [ ] Dashboard 密文视窗展示 AES 加密前后对比
```

---

## 九、紧急求助提示词（遇到 Bug 时发给 AI）

```
我在开发 SmartRover IoT 平台时遇到了问题。

【我的角色】A同学，负责后端 Python Flask
【当前任务】T020 设备管理接口
【文件路径】Backend/src/routes/devices.py
【错误信息】（粘贴完整报错）
【相关代码】（粘贴出问题的代码段）
【已尝试】（描述你已经试过什么）

请帮我分析原因并给出修复方案。
```

---

## 十、推荐的 AI 工具选择

| 工具 | 推荐场景 | 优势 |
|------|---------|------|
| **Cursor IDE** | 写代码（所有人） | 能直接读项目文件，上下文最准 |
| **Claude.ai** | 复杂逻辑设计、调试 | 长文本理解强 |
| **ChatGPT / Gemini** | 快速问答 | 响应快 |
| **GitHub Copilot** | 行级代码补全 | 嵌入 IDE，随写随补 |

> **Cursor 用户速成**：打开项目文件夹，直接在侧边栏 AI 对话框里说"帮我实现 T022"，它会自动读取项目上下文中的 api.yaml 和 plan.md。

---

*本文档由 A 同学在项目启动时维护，如有流程更新请修改本文件并 commit。*

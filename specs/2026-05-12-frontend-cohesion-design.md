# SmartRover 前端统一化重设计

**日期:** 2026-05-12
**类型:** 结构重排（方案 B）

## 目标

消除当前 6 个视图 + 1 个布局组件之间的视觉割裂，建立统一的设计语言。

---

## 第一章：全局设计系统

### 配色（保留现有 11 个 CSS 变量，不变）

| 变量 | 色值 |
|------|------|
| `--bg-primary` | `#0f1419` |
| `--bg-secondary` | `#1a1f2e` |
| `--bg-sidebar` | `#0d1117` |
| `--bg-header` | `#161b25` |
| `--border-color` | `#2a3040` |
| `--text-primary` | `#e0e4ec` |
| `--text-secondary` | `#8892a4` |
| `--accent` | `#3b82f6` |
| `--accent-success` | `#22c55e` |
| `--accent-danger` | `#ef4444` |
| `--accent-warning` | `#f59e0b` |

### ECharts 颜色统一化

| 图表 | 位置 | 当前硬编码 | 改为 |
|------|------|-----------|------|
| 饼图"在线" | DashboardView | `#67c23a` | CSS 变量 `--accent-success` |
| 饼图"离线" | DashboardView | `#f56c6c` | CSS 变量 `--accent-danger` |
| 饼图"碰撞" | DashboardView | `#e6a23c` | CSS 变量 `--accent-warning` |
| 仪表盘轴线 | DashboardView | 三个硬编码 | `--accent-danger/warning/success` |
| 地图轨迹线 | DashboardView | `#f56c6c` | `--accent-danger` |
| 地图标记线 | ControlPanelView | `#409eff`, `#f56c6c` | `--accent`, `--accent-danger` |
| navStateColor | ControlPanelView | 6 个硬编码字符串 | 引用 CSS 变量 |
| 遥测图表线 | DeviceCenterView | `#f56c6c`, `#409eff`, `#67c23a` | `--accent-danger`, `--accent`, `--accent-success` |
| SensorCharts | SensorCharts.vue | 多处硬编码 | 保留原样（组件不删除，暂时不用） |

### 间距体系（新增 CSS 变量）

```
--space-xs: 8px
--space-sm: 12px
--space-md: 16px   ← 默认 gutter
--space-lg: 20px
--space-xl: 24px   ← 内容区内边距
```

### 圆角与阴影（无条件统一）

- 所有 el-card: `border-radius: 4px; box-shadow: none; border: 1px solid var(--border-color)`
- 所有 el-button: `border-radius: 4px`
- 全部使用 `shadow="never"`，不允许 `shadow="hover"` 例外

### 排版

- 页面标题: `18px / 600`，统一在 PageHeader 组件中
- 描述文字: `13px / --text-secondary`
- 表格内容: `13px`
- 统计数值: `24px / 700`

---

## 第二章：布局骨架 (Layout.vue)

### 新增 PageHeader.vue 组件

```vue
<PageHeader title="页面标题" description="描述文字">
  <template #actions>可选的按钮</template>
</PageHeader>
```

所有页面（仪表盘、设备中心、控制面板、审计日志）统一使用。

### 内容区约束

- `.main-content` 添加 `max-width: 1280px; margin: 0 auto`
- 内边距保持 `padding: var(--space-xl)`

### 卡片体系统一

- 全部 `el-card shadow="never"`
- DeviceCenterView 表格包一层卡片（当前裸表格）
- 控制面板卡片去掉 `shadow="hover"`

### 响应式断点统一

```
统计卡片: :xs="12" :sm="12" :md="6"
图表区域: :xs="24" :lg="16" / :xs="24" :lg="8"
控制面板: :xs="24" :sm="6" / :xs="24" :sm="18" / :xs="24" :lg="12"
```

---

## 第三章：仪表盘重设计

### 删除

- SensorCharts 组件渲染区域及相关代码（文件保留不删）

### 新布局

```
PageHeader "平台概览"
4 统计卡片（响应式: xs=12 sm=12 md=6）
左 col(lg=16): 设备状态饼图 + 安全指数仪表盘
右 col(lg=8):  最近告警事件表格（分页、去 ID 列）
全宽: 车辆实时轨迹地图（设备选择器 + 位置信息栏）
```

### 变化清单

| 文件 | 改动 |
|------|------|
| `DashboardView.vue` | 删除 SensorCharts 导入和渲染 |
| `DashboardView.vue` | 图表+表格改为左右两栏布局 |
| `DashboardView.vue` | 统计卡片 shadow="hover" → shadow="never" |
| `DashboardView.vue` | ECharts 颜色全部改用 CSS 变量 |
| `DashboardView.vue` | 告警表格去掉 ID 列 |
| `DashboardView.vue` | 骨架屏保留 |

---

## 第四章：审计日志重设计

### 列调整

| 序号 | 列名 | 宽度 | 变化 |
|------|------|------|------|
| 1 | 事件类型 | 110px | 保留 |
| 2 | 关联用户 | 120px | 保留，v-if="isAdmin"，修复数据流 |
| 3 | 来源 IP | 140px | 保留 |
| 4 | 目标设备 | 160px | 保留（缩短宽度） |
| 5 | 详情 | min-width 260px | 保留 show-overflow-tooltip |
| 6 | 时间 | 170px | 保留 |

**删除**: ID 列、已拦截列（所有记录都是被拦截的攻击，冗余）

### 用户列数据流修复

1. 后端 `to_dict()` 已正确返回 `user_id` + `username`
2. 检查前端 `auditAPI.logs()` 调用参数是否正确传递 `user_id` 筛选
3. 确保表格 `row.username` 绑定的 `v-if` 条件正确渲染

### 筛选栏

保持: 事件类型 radio-group + 管理员 user_id 输入框 + 刷新按钮

---

## 第五章：登录页精简

### 方案 B：保留分栏，收窄品牌面板

- 左品牌面板 35% + 右表单面板 65%
- 去掉 SVG 品牌图标（三条路径的 logo），改为纯文字 "SmartRover"
- 保留深色背景和 CSS 变量
- 左边距/字体微调以适配 35% 宽度
- `border-radius: 6px` 保持不变

---

## 文件变更清单

| 操作 | 文件 |
|------|------|
| 新建 | `Frontend/src/components/PageHeader.vue` |
| 修改 | `Frontend/src/components/Layout.vue` |
| 修改 | `Frontend/src/views/dashboard/DashboardView.vue` |
| 修改 | `Frontend/src/views/audit_logs/AuditLogsView.vue` |
| 修改 | `Frontend/src/views/LoginView.vue` |
| 修改 | `Frontend/src/views/device_center/DeviceCenterView.vue` |
| 修改 | `Frontend/src/views/control_panel/ControlPanelView.vue` |
| 修改 | `Frontend/src/style.css` |
| 修改 | `Frontend/src/api/index.js`（如有需要） |

---

## 验证清单

1. `vite build` 构建成功
2. 所有页面 max-width 约束生效
3. 所有卡片 shadow="never"，无 hover 阴影
4. PageHeader 在所有 4 个内页中正确渲染
5. 仪表盘 SensorCharts 已删除，新布局两栏正常
6. 仪表盘 ECharts 颜色引用 CSS 变量
7. 审计日志无 ID 列、无"已拦截"列、用户列正确显示
8. 登录页左 35%+右 65%，无 SVG 图标
9. 响应式断点在各页面对应一致
10. 控制面板地图颜色改为 CSS 变量引用

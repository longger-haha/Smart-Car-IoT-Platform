<template>
  <div class="dashboard">
    <div class="page-header">
      <h2>平台概览</h2>
      <span class="desc">实时监控智能小车物联网平台运行状态</span>
    </div>

    <el-row :gutter="16" class="stat-cards">
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card card-device">
          <div class="stat-icon"><el-icon :size="32"><Monitor /></el-icon></div>
          <div class="stat-info">
            <p class="stat-value">{{ stats.total_devices ?? '-' }}</p>
            <p class="stat-label">设备总数</p>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card card-online">
          <div class="stat-icon"><el-icon :size="32"><Connection /></el-icon></div>
          <div class="stat-info">
            <p class="stat-value">{{ stats.online_devices ?? '-' }}</p>
            <p class="stat-label">在线设备</p>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card card-alert">
          <div class="stat-icon"><el-icon :size="32"><Warning /></el-icon></div>
          <div class="stat-info">
            <p class="stat-value">{{ stats.critical_alerts ?? '-' }}</p>
            <p class="stat-label">严重告警</p>
          </div>
        </el-card>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <el-card shadow="hover" class="stat-card card-audit">
          <div class="stat-icon"><el-icon :size="32"><Document /></el-icon></div>
          <div class="stat-info">
            <p class="stat-value">{{ stats.total_audit_logs ?? '-' }}</p>
            <p class="stat-label">审计日志</p>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" style="margin-top: 20px;">
      <el-col :xs="24" :lg="16">
        <el-card shadow="never">
          <template #header>
            <span>设备状态分布</span>
          </template>
          <div ref="pieChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
      <el-col :xs="24" :lg="8">
        <el-card shadow="never">
          <template #header>
            <span>安全指标</span>
          </template>
          <div ref="gaugeChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" style="margin-top: 20px;">
      <el-col :span="24">
        <el-card shadow="never">
          <template #header>
            <span>最近告警事件</span>
          </template>
          <el-table :data="stats.recent_alerts || []" stripe size="small" empty-text="暂无告警记录">
            <el-table-column prop="id" label="ID" width="70" />
            <el-table-column prop="event_type" label="事件类型" width="120">
              <template #default="{ row }">
                <el-tag
                  :type="eventTypeTagType(row.event_type)"
                  size="small"
                >
                  {{ eventTypeLabel(row.event_type) }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="source_ip" label="来源IP" width="150" />
            <el-table-column prop="target_device_id" label="目标设备" width="180" />
            <el-table-column prop="detail" label="详情" show-overflow-tooltip />
            <el-table-column prop="is_blocked" label="已拦截" width="90" align="center">
              <template #default="{ row }">
                <el-tag :type="row.is_blocked ? 'danger' : 'info'" size="small">
                  {{ row.is_blocked ? '是' : '否' }}
                </el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="occurred_at" label="时间" width="180">
              <template #default="{ row }">
                {{ formatTime(row.occurred_at) }}
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted, nextTick, watch } from 'vue'
import * as echarts from 'echarts'
import { Monitor, Connection, Warning, Document } from '@element-plus/icons-vue'
import { dashboardAPI } from '@/api'

const stats = reactive({
  total_devices: 0,
  online_devices: 0,
  offline_devices: 0,
  total_audit_logs: 0,
  critical_alerts: 0,
  collision_warnings: 0,
  recent_alerts: [],
})

const pieChartRef = ref(null)
const gaugeChartRef = ref(null)
let pieChart = null
let gaugeChart = null
let timer = null

async function fetchStats() {
  try {
    const data = await dashboardAPI.stats()
    Object.assign(stats, data)
    nextTick(() => {
      renderPieChart()
      renderGaugeChart()
    })
  } catch (e) {
    console.error('获取统计数据失败', e)
  }
}

function renderPieChart() {
  if (!pieChartRef.value) return
  if (!pieChart) pieChart = echarts.init(pieChartRef.value)

  pieChart.setOption({
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { bottom: 0 },
    color: ['#67c23a', '#f56c6c', '#e6a23c'],
    series: [
      {
        type: 'pie',
        radius: ['40%', '65%'],
        center: ['50%', '48%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 8, borderColor: '#fff', borderWidth: 2 },
        label: { show: true, formatter: '{b}\n{c}台' },
        data: [
          { value: stats.online_devices || 0, name: '在线' },
          { value: stats.offline_devices || 0, name: '离线' },
          { value: stats.collision_warnings || 0, name: '碰撞预警' },
        ],
      },
    ],
  })
}

function renderGaugeChart() {
  if (!gaugeChartRef.value) return
  if (!gaugeChart) gaugeChart = echarts.init(gaugeChartRef.value)

  const safeRate =
    stats.total_devices > 0
      ? Math.round(((stats.total_devices - stats.collision_warnings) / stats.total_devices) * 100)
      : 100

  gaugeChart.setOption({
    series: [
      {
        type: 'gauge',
        startAngle: 200,
        endAngle: -20,
        min: 0,
        max: 100,
        splitNumber: 10,
        radius: '85%',
        center: ['50%', '60%'],
        axisLine: {
          lineStyle: {
            width: 16,
            color: [
              [0.3, '#f56c6c'],
              [0.7, '#e6a23c'],
              [1, '#67c23a'],
            ],
          },
        },
        pointer: { icon: 'path://M12.8,0.7l12,40.1H0.7L12.8,0.7z', length: '70%', width: 5 },
        axisTick: { splitNumber: 5, length: 8, lineStyle: { color: '#fff', width: 1 } },
        splitLine: { length: 15, lineStyle: { color: '#fff', width: 2 } },
        axisLabel: { distance: 20, fontSize: 11, color: '#999' },
        title: { fontSize: 14, offsetCenter: [0, '30%'] },
        detail: {
          valueAnimation: true,
          formatter: '{value}%',
          fontSize: 28,
          offsetCenter: [0, '0%'],
          color: 'inherit',
        },
        data: [{ value: safeRate, name: '安全指数' }],
      },
    ],
  })
}

function eventTypeTagType(type) {
  const map = { replay: 'danger', ddos: 'danger', auth_fail: 'warning', rbac_deny: 'warning', sig_invalid: 'info' }
  return map[type] || 'info'
}

function eventTypeLabel(type) {
  const map = { replay: '重放攻击', ddos: 'DDoS', auth_fail: '认证失败', rbac_deny: '权限拒绝', sig_invalid: '签名无效' }
  return map[type] || type
}

function formatTime(timeStr) {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

onMounted(() => {
  fetchStats()
  timer = setInterval(fetchStats, 15000)
})

onUnmounted(() => {
  if (timer) clearInterval(timer)
  pieChart?.dispose()
  gaugeChart?.dispose()
})
</script>

<style scoped>
.dashboard {
  max-width: 1400px;
}

.page-header {
  margin-bottom: 20px;
}

.page-header h2 {
  margin: 0;
  font-size: 22px;
  color: #303133;
}

.page-header .desc {
  font-size: 13px;
  color: #909399;
  margin-top: 4px;
}

.stat-cards .stat-card {
  cursor: default;
}

.stat-card :deep(.el-card__body) {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 20px;
}

.stat-icon {
  width: 56px;
  height: 56px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  color: #fff;
}

.card-device .stat-icon { background: linear-gradient(135deg, #667eea, #764ba2); }
.card-online .stat-icon { background: linear-gradient(135deg, #11998e, #38ef7d); }
.card-alert .stat-icon { background: linear-gradient(135deg, #eb3349, #f45c43); }
.card-audit .stat-icon { background: linear-gradient(135deg, #4facfe, #00f2fe); }

.stat-value {
  font-size: 26px;
  font-weight: 700;
  color: #303133;
  margin: 0;
  line-height: 1.3;
}

.stat-label {
  font-size: 13px;
  color: #909399;
  margin: 2px 0 0;
}

.chart-container {
  width: 100%;
  height: 320px;
}
</style>

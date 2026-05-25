<template>
  <div class="dashboard">
    <div class="page-header">
      <h2>平台概览</h2>
      <span class="desc">实时监控智能小车物联网平台运行状态</span>
    </div>

    <el-row :gutter="16" class="stat-cards">
      <el-col :xs="12" :sm="12" :md="6">
        <router-link to="/devices" class="stat-card card-device clickable">
          <el-card shadow="hover">
            <div class="stat-icon"><el-icon :size="32"><Monitor /></el-icon></div>
            <div class="stat-info">
              <p class="stat-value" v-if="!statsLoading">{{ stats.total_devices ?? '-' }}</p>
              <el-skeleton v-else animated style="width:60px;height:26px;" />
              <p class="stat-label">设备总数</p>
            </div>
          </el-card>
        </router-link>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <router-link to="/devices" class="stat-card card-online clickable">
          <el-card shadow="hover">
            <div class="stat-icon"><el-icon :size="32"><Connection /></el-icon></div>
            <div class="stat-info">
              <p class="stat-value" v-if="!statsLoading">{{ stats.online_devices ?? '-' }}</p>
              <el-skeleton v-else animated style="width:60px;height:26px;" />
              <p class="stat-label">在线设备</p>
            </div>
          </el-card>
        </router-link>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <router-link to="/control" class="stat-card card-alert clickable">
          <el-card shadow="hover">
            <div class="stat-icon"><el-icon :size="32"><Warning /></el-icon></div>
            <div class="stat-info">
              <p class="stat-value" v-if="!statsLoading">{{ stats.critical_alerts ?? '-' }}</p>
              <el-skeleton v-else animated style="width:60px;height:26px;" />
              <p class="stat-label">严重告警</p>
            </div>
          </el-card>
        </router-link>
      </el-col>
      <el-col :xs="12" :sm="12" :md="6">
        <router-link to="/audit" class="stat-card card-audit clickable">
          <el-card shadow="hover">
            <div class="stat-icon"><el-icon :size="32"><Document /></el-icon></div>
            <div class="stat-info">
              <p class="stat-value" v-if="!statsLoading">{{ stats.total_audit_logs ?? '-' }}</p>
              <el-skeleton v-else animated style="width:60px;height:26px;" />
              <p class="stat-label">审计日志</p>
            </div>
          </el-card>
        </router-link>
      </el-col>
    </el-row>

    <el-row :gutter="16" class="section-row">
      <el-col :span="24">
        <SensorCharts :telemetry-data="dashPositionData ?? {}" />
      </el-col>
    </el-row>

    <el-row :gutter="16" class="section-row">
      <el-col :xs="24" :lg="14">
        <el-card shadow="never">
          <template #header>
            <span>设备状态分布</span>
          </template>
          <div ref="pieChartRef" class="chart-container"></div>
        </el-card>
      </el-col>
      <el-col :xs="24" :lg="10">
        <el-card shadow="never">
          <template #header>
            <span>安全指标</span>
          </template>
          <div ref="gaugeChartRef" class="chart-container"></div>
          <div class="safety-detail" v-if="stats.min_ultrasonic_cm != null">
            <div class="safety-row">
              <span class="safety-label">最近距离</span>
              <span class="safety-value" :style="{ color: stats.min_ultrasonic_cm < 30 ? '#ef4444' : stats.min_ultrasonic_cm < 50 ? '#f59e0b' : '#10b981' }">
                {{ stats.min_ultrasonic_cm }} cm
              </span>
            </div>
            <div class="safety-row" v-if="stats.danger_count > 0">
              <span class="safety-label">危险设备</span>
              <span class="safety-value" style="color:#ef4444">{{ stats.danger_count }} 台</span>
            </div>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" class="section-row">
      <el-col :span="24">
        <el-card shadow="never">
          <template #header>
            <div style="display:flex;justify-content:space-between;align-items:center;flex-wrap:wrap;gap:8px;">
              <span>车辆实时轨迹</span>
              <el-select v-model="dashDeviceId" placeholder="选择设备" size="small" style="width:200px;" @change="onDashDeviceChange">
                <el-option
                  v-for="d in deviceList"
                  :key="d.device_id"
                  :label="`${d.name} (${d.status})`"
                  :value="d.device_id"
                />
              </el-select>
            </div>
          </template>

          <div id="dash-trajectory-map" class="chart-container" style="height:360px;"></div>
          <div style="margin-top:10px;display:flex;gap:12px;flex-wrap:wrap;">
            <el-tag size="small" type="success">在线设备: {{ stats.online_devices ?? 0 }}</el-tag>
            <el-tag size="small">总设备数: {{ stats.total_devices ?? 0 }}</el-tag>
            <el-tag size="small" type="warning">告警: {{ stats.critical_alerts ?? 0 }}</el-tag>
            <el-tag v-if="dashPositionData && dashPositionData.device_online === false" size="small" type="danger">设备离线</el-tag>
            <span v-if="dashPositionData" class="dash-pos-bar">
              {{ dashPositionData.lat?.toFixed(5) || '--' }}, {{ dashPositionData.lng?.toFixed(5) || '--' }}
              · PWM {{ dashPositionData.speed_pwm || '--' }}
              · {{ dashPositionData.temperature ? dashPositionData.temperature + '°C' : '' }}
            </span>
          </div>
        </el-card>
      </el-col>
    </el-row>

    <el-row :gutter="16" class="section-row">
      <el-col :span="24">
        <el-card shadow="never">
          <template #header>
            <span>最近告警事件</span>
          </template>
          <el-table :data="paginatedAlerts" stripe size="small" empty-text="暂无告警记录">
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
          <div class="table-footer" v-if="(stats.recent_alerts || []).length > alertPageSize">
            <el-pagination
              v-model:current-page="alertPage"
              v-model:page-size="alertPageSize"
              :total="(stats.recent_alerts || []).length"
              :page-sizes="[10, 20]"
              layout="total, prev, pager, next"
              small
            />
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, onUnmounted, nextTick, watch, computed } from 'vue'
import { useRouter } from 'vue-router'
import * as echarts from 'echarts'
import { Monitor, Connection, Warning, Document } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import { dashboardAPI, vehicleAPI, deviceAPI } from '@/api'
import AMapLoader from '@amap/amap-jsapi-loader'
import { AMAP_KEY, AMAP_VERSION, AMAP_SECURITY_KEY } from '@/config/amap'
import { formatTime, eventTypeTagType, eventTypeLabel } from '@/utils/format'

const router = useRouter()

const isAdmin = computed(() => localStorage.getItem('user_role') === 'admin')

const navigateTo = (path) => router.push(path)
import SensorCharts from './SensorCharts.vue'

const statsLoading = ref(true)
const alertPage = ref(1)
const alertPageSize = ref(10)
const paginatedAlerts = computed(() => {
  const all = stats.recent_alerts || []
  const start = (alertPage.value - 1) * alertPageSize.value
  return all.slice(start, start + alertPageSize.value)
})
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

const deviceList = ref([])
const dashDeviceId = ref('')
const dashMapLoading = ref(false)
const dashPositionData = ref(null)
let dashTrajectoryMap = null
let dashPositionMarker = null
let dashTrajectoryPolyline = null
let dashPosTimer = null
let dashPollInterval = 5000
let dashRequestGen = 0  // 请求代次，防止切换设备时旧请求的响应覆盖新地图

async function fetchStats() {
  try {
    const data = await dashboardAPI.stats()
    Object.assign(stats, data)
    statsLoading.value = false
    nextTick(() => {
      renderPieChart()
      renderGaugeChart()
    })
  } catch (e) {
    console.error('获取统计数据失败', e)
    statsLoading.value = false
  }
}

function renderPieChart() {
  if (!pieChartRef.value) return
  if (!pieChart) pieChart = echarts.init(pieChartRef.value)

  pieChart.setOption({
    tooltip: { trigger: 'item', formatter: '{b}: {c} ({d}%)' },
    legend: { bottom: 0, textStyle: { color: '#64748b' } },
    color: ['#10b981', '#ef4444', '#f59e0b'],
    series: [
      {
        type: 'pie',
        radius: ['45%', '70%'],
        center: ['50%', '45%'],
        avoidLabelOverlap: true,
        itemStyle: { borderRadius: 6, borderColor: '#ffffff', borderWidth: 3 },
        label: { show: true, formatter: '{b}\n{c}台', color: '#0f172a' },
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

  const safeRate = stats.safety_index ?? 100

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
        center: ['50%', '55%'],
        axisLine: {
          lineStyle: {
            width: 14,
            color: [
              [0.3, '#ef4444'],
              [0.7, '#f59e0b'],
              [1, '#10b981'],
            ],
          },
        },
        pointer: { icon: 'path://M12.8,0.7l12,40.1H0.7L12.8,0.7z', length: '70%', width: 5 },
        axisTick: { splitNumber: 5, length: 8, lineStyle: { color: '#cbd5e1', width: 1 } },
        splitLine: { length: 15, lineStyle: { color: '#94a3b8', width: 2 } },
        axisLabel: { distance: 20, fontSize: 11, color: '#64748b' },
        title: { fontSize: 14, offsetCenter: [0, '35%'], color: '#64748b' },
        detail: {
          valueAnimation: true,
          formatter: '{value}%',
          fontSize: 28,
          offsetCenter: [0, '5%'],
          color: '#0f172a',
        },
        data: [{ value: safeRate, name: '安全指数' }],
      },
    ],
  })
}

async function loadDashDevices() {
  try {
    const data = await deviceAPI.list()
    deviceList.value = data.devices || data || []
    if (deviceList.value.length > 0 && !dashDeviceId.value) {
      dashDeviceId.value = deviceList.value[0].device_id
      fetchDashPosition()
      startDashPolling()
    }
  } catch (e) { console.error('获取设备列表失败', e) }
}

async function fetchDashPosition() {
  if (!dashDeviceId.value) return
  try {
    const res = await vehicleAPI.getPosition(dashDeviceId.value)
    dashPositionData.value = res
    // 设备离线时降低轮询频率(30秒)，上线时恢复(5秒)
    if (res.device_online === false) {
      if (dashPosTimer && dashPollInterval !== 30000) {
        stopDashPolling()
        dashPollInterval = 30000
        dashPosTimer = setInterval(() => { fetchDashPosition(); refreshDashTrajectory() }, dashPollInterval)
      }
    } else if (res.device_online === true) {
      if (dashPosTimer && dashPollInterval !== 5000) {
        stopDashPolling()
        dashPollInterval = 5000
        dashPosTimer = setInterval(() => { fetchDashPosition(); refreshDashTrajectory() }, dashPollInterval)
      }
    }
    drawDashPositionMarker(res)
  } catch (e) { console.error('获取设备位置失败', e) }
}

function drawDashPositionMarker(pos) {
  if (!dashTrajectoryMap || !pos.lat || !pos.lng) return
  if (dashPositionMarker) {
    dashPositionMarker.setPosition([pos.lng, pos.lat])
  } else {
    dashPositionMarker = new AMap.Marker({
      position: [pos.lng, pos.lat],
      icon: new AMap.Icon({
        image: '//a.amap.com/jsapi_demos/static/demo-center/icons/poi-marker-red.png',
        size: [25, 34], imageSize: [25, 34],
      }),
    })
    dashPositionMarker.setMap(dashTrajectoryMap)
  }
}

async function refreshDashTrajectory() {
  if (!dashDeviceId.value) return
  const gen = dashRequestGen  // 记录当前代次
  try {
    const res = await vehicleAPI.getTrajectory(dashDeviceId.value, 2, 2000)
    if (gen !== dashRequestGen) return  // 已切换设备，丢弃此响应
    if (res.points && res.points.length > 1) {
      drawDashTrajectory(res.points)
      if (dashTrajectoryMap && dashTrajectoryPolyline) {
        dashTrajectoryMap.setFitView([dashTrajectoryPolyline, dashPositionMarker].filter(Boolean))
      }
    }
  } catch (e) { console.error('刷新轨迹失败', e) }
}

function drawDashTrajectory(points) {
  if (!dashTrajectoryMap || points.length < 2) return
  const path = points.map(p => [p.lng, p.lat])
  if (dashTrajectoryPolyline) dashTrajectoryMap.remove(dashTrajectoryPolyline)
  dashTrajectoryPolyline = new AMap.Polyline({
    path,
    strokeColor: '#f56c6c',
    strokeWeight: 4,
    strokeOpacity: 0.85,
    lineJoin: 'round',
  })
  dashTrajectoryPolyline.setMap(dashTrajectoryMap)
}

function initDashTrajectoryMap() {
  if (!AMapLoader) return
  dashMapLoading.value = true
  if (dashTrajectoryMap) { dashTrajectoryMap.destroy(); dashTrajectoryMap = null }
  if (dashPositionMarker) { dashPositionMarker.setMap(null); dashPositionMarker = null }
  if (dashTrajectoryPolyline) { dashTrajectoryMap?.remove(dashTrajectoryPolyline); dashTrajectoryPolyline = null }

  const el = document.getElementById('dash-trajectory-map')
  if (!el) return
  el.innerHTML = ''

  // 设置安全密钥
  if (AMAP_SECURITY_KEY) {
    window._AMapSecurityConfig = { securityJsCode: AMAP_SECURITY_KEY }
  }

  AMapLoader.load({ key: AMAP_KEY, version: AMAP_VERSION }).then((AMap) => {
    dashTrajectoryMap = new AMap.Map('dash-trajectory-map', {
      zoom: 15, center: [116.397428, 39.90923], viewMode: '2D',
    })
    if (dashDeviceId.value) refreshDashTrajectory()
    if (dashPositionData.value) drawDashPositionMarker(dashPositionData.value)
    dashMapLoading.value = false
  }).catch((e) => {
    console.warn('仪表盘地图加载失败:', e)
    dashMapLoading.value = false
    ElMessage.error('地图组件加载失败')
  })
}

function onDashDeviceChange(deviceId) {
  stopDashPolling()
  dashRequestGen++  // 递增代次，令所有正在飞行的旧请求失效
  dashPositionData.value = null
  if (dashPositionMarker) { dashPositionMarker.setMap(null); dashPositionMarker = null }
  if (dashTrajectoryPolyline) { dashTrajectoryMap?.remove(dashTrajectoryPolyline); dashTrajectoryPolyline = null }
  // 重置地图视角到默认中心
  if (dashTrajectoryMap) dashTrajectoryMap.setZoomAndCenter(15, [116.397428, 39.90923])
  if (deviceId) {
    fetchDashPosition()
    refreshDashTrajectory()
    startDashPolling()
  }
}

function startDashPolling() {
  stopDashPolling()
  dashPollInterval = 5000
  dashPosTimer = setInterval(() => { fetchDashPosition(); refreshDashTrajectory() }, dashPollInterval)
}

function stopDashPolling() {
  if (dashPosTimer) { clearInterval(dashPosTimer); dashPosTimer = null }
}

onMounted(async () => {
  fetchStats()
  await loadDashDevices()
  await nextTick()
  initDashTrajectoryMap()
  timer = setInterval(fetchStats, 15000)
})

onUnmounted(() => {
  if (timer) clearInterval(timer)
  stopDashPolling()
  pieChart?.dispose()
  gaugeChart?.dispose()
})
</script>

<style scoped>
.dashboard {
  max-width: 1400px;
}

.section-row {
  margin-top: 16px;
}

.page-header {
  margin-bottom: 24px;
}

.page-header h2 {
  margin: 0;
  font-size: 20px;
  font-weight: 700;
  color: var(--text-primary);
}

.page-header .desc {
  font-size: 14px;
  color: var(--text-secondary);
  margin-top: 6px;
  display: inline-block;
}

.stat-cards .stat-card {
  cursor: default;
  border-radius: var(--radius-lg);
  transition: transform 0.25s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.25s cubic-bezier(0.4, 0, 0.2, 1);
  display: block;
  text-decoration: none;
}

.stat-cards .stat-card.clickable {
  cursor: pointer;
}

.stat-cards .stat-card :deep(.el-card) {
  border-radius: var(--radius-lg);
  transition: transform 0.25s cubic-bezier(0.4, 0, 0.2, 1), box-shadow 0.25s cubic-bezier(0.4, 0, 0.2, 1);
}

.stat-cards .stat-card:hover :deep(.el-card) {
  transform: translateY(-4px);
  box-shadow: var(--shadow-md) !important;
}

.stat-cards .stat-card.clickable:hover :deep(.el-card) {
  transform: translateY(-4px);
  box-shadow: var(--shadow-md) !important;
}

.stat-card :deep(.el-card__body) {
  display: flex;
  align-items: center;
  gap: 18px;
  padding: 24px;
}

.stat-icon {
  width: 52px;
  height: 52px;
  border-radius: 12px;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-shrink: 0;
  color: #fff;
  font-size: 24px;
  box-shadow: 0 4px 6px rgba(0,0,0,0.1);
}

.card-device .stat-icon { background: linear-gradient(135deg, #3b82f6, #2563eb); }
.card-online .stat-icon { background: linear-gradient(135deg, #34d399, #10b981); }
.card-alert .stat-icon { background: linear-gradient(135deg, #f87171, #ef4444); }
.card-audit .stat-icon { background: linear-gradient(135deg, #fbbf24, #f59e0b); }

.stat-value {
  font-size: 28px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0;
  line-height: 1.2;
}

.stat-label {
  font-size: 14px;
  color: var(--text-secondary);
  margin: 4px 0 0;
  font-weight: 500;
}

.chart-container {
  width: 100%;
  height: 300px;
}

.safety-detail {
  margin-top: 4px;
  padding: 0 8px;
}
.safety-row {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 4px 0;
  font-size: 13px;
}
.safety-label {
  color: #64748b;
}
.safety-value {
  font-weight: 600;
}

#dash-trajectory-map {
  border-radius: var(--radius-md);
  overflow: hidden;
  border: 1px solid var(--border-color-light);
}

.dash-pos-bar {
  font-size: 13px;
  color: var(--text-secondary);
  margin-left: auto;
  font-family: monospace;
  background: var(--bg-primary);
  padding: 4px 8px;
  border-radius: 4px;
}

.table-footer {
  display: flex;
  justify-content: flex-end;
  padding-top: 16px;
  margin-top: 8px;
}
</style>

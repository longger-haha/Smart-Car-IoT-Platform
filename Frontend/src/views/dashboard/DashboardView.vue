<template>
  <div class="dashboard-container">
    <div class="dashboard-header">
      <h2><el-icon><DataLine /></el-icon> 实时监控大屏</h2>
      <div class="header-info">
        <el-tag type="success" effect="dark">系统运行中</el-tag>
        <span class="update-time">最后更新: {{ lastUpdateTime }}</span>
      </div>
    </div>

    <div class="stats-cards">
      <el-row :gutter="20">
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card">
            <el-statistic title="总设备数" :value="dashboardStats.total_devices" />
            <div class="stat-footer">已注册设备总数</div>
          </el-card>
        </el-col>
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card online">
            <el-statistic title="在线设备" :value="dashboardStats.online_devices" />
            <div class="stat-footer">当前在线</div>
          </el-card>
        </el-col>
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card offline">
            <el-statistic title="离线设备" :value="dashboardStats.offline_devices" />
            <div class="stat-footer">当前离线</div>
          </el-card>
        </el-col>
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card warning">
            <el-statistic title="安全告警" :value="dashboardStats.critical_alerts" />
            <div class="stat-footer">replay + ddos 攻击</div>
          </el-card>
        </el-col>
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card danger">
            <el-statistic title="碰撞预警" :value="dashboardStats.collision_warnings" />
            <div class="stat-footer">超声波 &lt; {{ dashboardStats.safe_distance_cm || 50 }}cm</div>
          </el-card>
        </el-col>
        <el-col :span="4">
          <el-card shadow="hover" class="stat-card info">
            <el-statistic title="审计日志" :value="dashboardStats.total_audit_logs" />
            <div class="stat-footer">总记录数</div>
          </el-card>
        </el-col>
      </el-row>
    </div>

    <div class="recent-alerts" v-if="dashboardStats.recent_alerts?.length > 0">
      <el-alert
        v-for="(alert, index) in dashboardStats.recent_alerts"
        :key="index"
        :title="getEventTypeName(alert.event_type) + ' - ' + (alert.detail || alert.source_ip)"
        :type="['replay', 'ddos'].includes(alert.event_type) ? 'error' : 'warning'"
        show-icon
        :closable="false"
        style="margin-bottom: 8px;"
      >
        <template #default>
          <span style="font-size: 12px;">{{ formatTime(alert.occurred_at) }} | IP: {{ alert.source_ip }} | 设备: {{ alert.target_device_id }}</span>
        </template>
      </el-alert>
    </div>

    <div class="dashboard-content">
      <div class="map-section">
        <el-card class="map-card">
          <template #header>
            <div class="card-title">
              <el-icon><Location /></el-icon>
              <span>车辆实时定位</span>
            </div>
          </template>
          <div id="amap-container" ref="mapContainer"></div>
        </el-card>
      </div>

      <div class="charts-section">
        <SensorCharts :telemetry-data="latestTelemetry" />
      </div>
    </div>

    <div class="telemetry-info">
      <el-row :gutter="20">
        <el-col :span="6">
          <el-statistic title="温度 (°C)" :value="latestTelemetry.temperature" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="湿度 (%)" :value="latestTelemetry.humidity" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="超声波距离 (cm)" :value="latestTelemetry.ultrasonic_cm" />
        </el-col>
        <el-col :span="6">
          <el-statistic title="电机 PWM" :value="latestTelemetry.speed_pwm" />
        </el-col>
      </el-row>
    </div>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import AMapLoader from '@amap/amap-jsapi-loader'
import api from '@/api'
import SensorCharts from './SensorCharts.vue'

const mapContainer = ref(null)
let map = null
let marker = null
const pollingTimer = ref(null)
const statsTimer = ref(null)
const latestTelemetry = ref({
  latitude: 39.9042,
  longitude: 116.4074,
  temperature: 0,
  humidity: 0,
  ultrasonic_cm: 0,
  speed_pwm: 0
})
const dashboardStats = ref({
  total_devices: 0,
  online_devices: 0,
  offline_devices: 0,
  total_audit_logs: 0,
  critical_alerts: 0,
  collision_warnings: 0,
  safe_distance_cm: 50,
  recent_alerts: []
})
const lastUpdateTime = ref('-')

window._AMapSecurityConfig = {
  securityJsCode: 'YOUR_AMAP_SECURITY_CODE'
}

const initMap = async () => {
  try {
    const AMap = await AMapLoader.load({
      key: 'YOUR_AMAP_API_KEY',
      version: '2.0',
      plugins: ['AMap.Scale', 'AMap.ToolBar']
    })

    map = new AMap.Map(mapContainer.value, {
      viewMode: '2D',
      zoom: 15,
      center: [116.4074, 39.9042]
    })

    map.addControl(new AMap.Scale())
    map.addControl(new AMap.ToolBar({ position: 'RT' }))

    marker = new AMap.Marker({
      position: [116.4074, 39.9042],
      icon: new AMap.Icon({
        size: new AMap.Size(32, 32),
        image: 'data:image/svg+xml;base64,PHN2ZyB3aWR0aD0iMzIiIGhlaWdodD0iMzIiIHZpZXdCb3g9IjAgMCAzMiAzMiIgZmlsbD0ibm9uZSIgeG1sbnM9Imh0dHA6Ly93d3cudzMub3JnLzIwMDAvc3ZnIj4KPGNpcmNsZSBjeD0iMTYiIGN5PSIxNiIgcj0iMTYiIGZpbGw9IiNGNTNjNDgiLz4KPHBhdGggZD0iTTE2IDhDMTMuNzkwOSA4IDEyIDkuNzkwODYgMTIgMTJDMTIgMTQuMjA5MSAxMy43OTA5IDE2IDE2IDE2QzE4LjIwOTEgMTYgMjAgMTQuMjA5MSAyMCAxMkMyMCA5Ljc5MDg2IDE4LjIwOTEgOCAxNiA4WiIgZmlsbD0id2hpdGUiLz4KPC9zdmc+',
        imageSize: new AMap.Size(32, 32)
      }),
      offset: new AMap.Pixel(-16, -16),
      title: 'SmartRover 智能车',
      animation: 'AMAP_ANIMATION_BOUNCE'
    })

    map.add(marker)
  } catch (error) {
    console.error('Map initialization error:', error)
  }
}

const fetchDashboardStats = async () => {
  try {
    const response = await api.get('/dashboard/stats')
    if (response) {
      dashboardStats.value = response
    }
  } catch (error) {
    console.error('Fetch dashboard stats error:', error)
  }
}

const fetchLatestTelemetry = async () => {
  try {
    const response = await api.get('/telemetry/latest/default-device')
    if (response && response.device_id) {
      latestTelemetry.value = response

      if (map && marker && response.latitude && response.longitude) {
        const position = [response.longitude, response.latitude]
        marker.setPosition(position)
        map.setCenter(position)
      }

      lastUpdateTime.value = new Date().toLocaleTimeString('zh-CN')
    }
  } catch (error) {
    console.error('Fetch telemetry error:', error)
  }
}

const getEventTypeName = (type) => {
  const nameMap = {
    replay: '🔴 重放攻击',
    ddos: '🔴 DDoS攻击',
    auth_fail: '🟡 认证失败',
    rbac_deny: '🟡 越权拦截',
    sig_invalid: '🔵 签名无效'
  }
  return nameMap[type] || type
}

const formatTime = (timeStr) => {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

onMounted(() => {
  initMap()
  fetchDashboardStats()
  fetchLatestTelemetry()

  pollingTimer.value = setInterval(fetchLatestTelemetry, 2000)
  statsTimer.value = setInterval(fetchDashboardStats, 5000)
})

onUnmounted(() => {
  if (pollingTimer.value) clearInterval(pollingTimer.value)
  if (statsTimer.value) clearInterval(statsTimer.value)
  map = null
  marker = null
})
</script>

<style scoped>
.dashboard-container {
  padding: 20px;
  min-height: calc(100vh - 56px);
  overflow-y: auto;
  background-color: #f5f7fa;
}

.dashboard-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 20px;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  color: white;
  padding: 15px 25px;
  border-radius: 8px;
}

.dashboard-header h2 {
  font-size: 22px;
  font-weight: 600;
  margin: 0;
  display: flex;
  align-items: center;
  gap: 10px;
}

.header-info {
  display: flex;
  align-items: center;
  gap: 15px;
}

.update-time {
  font-size: 14px;
  opacity: 0.9;
}

.stats-cards {
  margin-bottom: 20px;
}

.stat-card {
  text-align: center;
  transition: transform 0.2s;
}

.stat-card:hover {
  transform: translateY(-4px);
}

.stat-card.online :deep(.el-statistic__head) {
  color: #67c23a;
}

.stat-card.offline :deep(.el-statistic__head) {
  color: #909399;
}

.stat-card.warning :deep(.el-statistic__head) {
  color: #e6a23c;
}

.stat-card.danger :deep(.el-statistic__head) {
  color: #f56c6c;
}

.stat-card.info :deep(.el-statistic__head) {
  color: #409eff;
}

.stat-footer {
  font-size: 12px;
  color: #909399;
  margin-top: 8px;
}

.recent-alerts {
  margin-bottom: 20px;
}

.dashboard-content {
  display: grid;
  grid-template-columns: 2fr 1fr;
  gap: 20px;
  margin-bottom: 20px;
}

.map-card {
  min-height: 500px;
}

#amap-container {
  width: 100%;
  height: 450px;
  border-radius: 4px;
}

.card-title {
  font-size: 16px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 8px;
}

.telemetry-info {
  background: white;
  padding: 20px;
  border-radius: 8px;
  box-shadow: 0 2px 12px rgba(0, 0, 0, 0.06);
}
</style>

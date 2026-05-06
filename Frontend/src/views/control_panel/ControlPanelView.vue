<template>
  <div class="control-panel">
    <div class="page-header">
      <div>
        <h2>控制面板</h2>
        <span class="desc">远程控制智能小车 & 路线规划</span>
      </div>
      <el-select
        v-model="selectedDeviceId"
        placeholder="请选择设备"
        style="width: 240px;"
        @change="onDeviceChange"
      >
        <el-option
          v-for="d in onlineDevices"
          :key="d.device_id"
          :label="(d.name || d.device_id) + (d.status === 'online' ? ' (在线)' : ' (离线)')"
          :value="d.device_id"
          :disabled="d.status !== 'online'"
        />
      </el-select>
    </div>

    <el-row :gutter="20">
      <!-- 遥控器 -->
      <el-col :xs="24" :lg="10">
        <el-card shadow="never" class="control-card">
          <template #header>
            <div class="card-title">
              <el-icon><Position /></el-icon>
              <span>遥控器</span>
              <el-tag v-if="selectedDeviceId" type="success" size="small" effect="dark">已连接</el-tag>
            </div>
          </template>

          <div class="controller-area">
            <div class="dpad">
              <button
                class="dpad-btn dpad-up"
                :class="{ active: activeBtn === 'forward' }"
                @mousedown="sendCmd('forward')"
                @mouseup="sendCmd('stop')"
                @mouseleave="sendCmd('stop')"
                :disabled="!selectedDeviceId"
              >
                <el-icon :size="28"><Top /></el-icon>
              </button>
              <button
                class="dpad-btn dpad-left"
                :class="{ active: activeBtn === 'left' }"
                @mousedown="sendCmd('left')"
                @mouseup="sendCmd('stop')"
                @mouseleave="sendCmd('stop')"
                :disabled="!selectedDeviceId"
              >
                <el-icon :size="28"><Left /></el-icon>
              </button>
              <button
                class="dpad-btn dpad-center"
                :class="{ active: activeBtn === 'stop' }"
                @click="sendCmd('stop')"
                :disabled="!selectedDeviceId"
              >
                <el-icon :size="24"><VideoPause /></el-icon>
              </button>
              <button
                class="dpad-btn dpad-right"
                :class="{ active: activeBtn === 'right' }"
                @mousedown="sendCmd('right')"
                @mouseup="sendCmd('stop')"
                @mouseleave="sendCmd('stop')"
                :disabled="!selectedDeviceId"
              >
                <el-icon :size="28"><Right /></el-icon>
              </button>
              <button
                class="dpad-btn dpad-down"
                :class="{ active: activeBtn === 'backward' }"
                @mousedown="sendCmd('backward')"
                @mouseup="sendCmd('stop')"
                @mouseleave="sendCmd('stop')"
                :disabled="!selectedDeviceId"
              >
                <el-icon :size="28"><Bottom /></el-icon>
              </button>
            </div>

            <div class="speed-control">
              <label>速度 PWM:</label>
              <el-slider v-model="speedPwm" :min="50" :max="255" :step="5" show-input />
            </div>
          </div>

          <div class="command-log">
            <h4>指令日志</h4>
            <div ref="logContainer" class="log-list">
              <p v-for="(log, i) in commandLogs" :key="i" class="log-item">
                <el-tag :type="log.command === 'stop' ? 'info' : 'primary'" size="small">{{ log.command }}</el-tag>
                <span class="log-time">{{ log.time }}</span>
              </p>
              <p v-if="commandLogs.length === 0" class="log-empty">暂无指令记录</p>
            </div>
          </div>
        </el-card>
      </el-col>

      <!-- 地图路线规划 -->
      <el-col :xs="24" :lg="14">
        <el-card shadow="never" class="map-card">
          <template #header>
            <div class="card-title">
              <el-icon><MapLocation /></el-icon>
              <span>路线规划</span>
              <div style="margin-left: auto; display: flex; gap: 8px;">
                <el-button size="small" @click="clearWaypoints">清空航点</el-button>
                <el-button size="small" type="primary" :loading="routeLoading" :disabled="waypoints.length < 2 || !selectedDeviceId" @click="dispatchRoute">下发路线</el-button>
              </div>
            </div>
          </template>

          <div id="map-container" class="map-container"></div>

          <div class="waypoint-info">
            <el-alert
              :title="`当前航点数: ${waypoints.length} / 50（至少需要2个点）`"
              :type="waypoints.length >= 2 ? 'success' : 'warning'"
              :closable="false"
              show-icon
            />
          </div>

          <div v-if="lastRouteInfo" class="last-route">
            <el-divider content-position="left">最近下发的路线</el-divider>
            <el-descriptions :column="2" border size="small">
              <el-descriptions-item label="设备ID">{{ lastRouteInfo.device_id }}</el-descriptions-item>
              <el-descriptions-item label="航点数量">{{ lastRouteInfo.waypoint_count }}</el-descriptions-item>
              <el-descriptions-item label="下发时间" :span="2">{{ formatTime(lastRouteInfo.dispatched_at) }}</el-descriptions-item>
            </el-descriptions>
          </div>
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, onMounted, onUnmounted, nextTick, watch } from 'vue'
import { Position, MapLocation, Top, Bottom, Left, Right, VideoPause } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import AMapLoader from '@amap/amap-jsapi-loader'
import { deviceAPI, vehicleAPI } from '@/api'

window._AMapSecurityConfig = {
  securityJsCode: '',
}

const selectedDeviceId = ref('')
const devices = ref([])
const onlineDevices = ref([])
const speedPwm = ref(150)
const activeBtn = ref('')
const commandLogs = ref([])
const routeLoading = ref(false)
const waypoints = ref([])
const lastRouteInfo = ref(null)

let map = null
let marker = null
let polyline = null
let markersArray = []

async function fetchDevices() {
  try {
    const res = await deviceAPI.list()
    devices.value = res.devices || []
    onlineDevices.value = devices.value.filter(d => d.status === 'online')
    if (!selectedDeviceId.value && onlineDevices.value.length > 0) {
      selectedDeviceId.value = onlineDevices.value[0].device_id
      onDeviceChange(selectedDeviceId.value)
    }
  } catch (e) {
    console.error(e)
  }
}

function onDeviceChange(deviceId) {
  if (!deviceId) return
  fetchLastRoute(deviceId)
}

function addLog(command) {
  const now = new Date().toLocaleTimeString('zh-CN', { hour12: false })
  commandLogs.value.unshift({ command, time: now })
  if (commandLogs.value.length > 30) commandLogs.value.pop()
}

async function sendCmd(command) {
  if (!selectedDeviceId.value) return

  activeBtn.value = command
  addLog(command)

  try {
    await vehicleAPI.sendCommand(selectedDeviceId.value, command, speedPwm.value)
    if (command !== 'stop') {
      ElMessage.success(`指令 ${command} 已下发`)
    }
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '指令发送失败')
  }

  if (command === 'stop') {
    activeBtn.value = ''
  }
}

async function dispatchRoute() {
  if (waypoints.value.length < 2) {
    ElMessage.warning('请至少选择2个航点')
    return
  }

  routeLoading.value = true
  try {
    const res = await vehicleAPI.dispatchRoute(selectedDeviceId.value, waypoints.value)
    lastRouteInfo.value = res
    ElMessage.success(`巡航路线已下发，共 ${res.waypoint_count} 个航点`)
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '路线下发失败')
  } finally {
    routeLoading.value = false
  }
}

async function fetchLastRoute(deviceId) {
  try {
    const res = await vehicleAPI.getLastRoute(deviceId)
    lastRouteInfo.value = res
  } catch (e) {
    lastRouteInfo.value = null
  }
}

function clearWaypoints() {
  waypoints.value = []
  markersArray.forEach(m => m.setMap(null))
  markersArray = []
  if (polyline) {
    polyline.setMap(null)
    polyline = null
  }
}

function formatTime(ts) {
  if (!ts) return '-'
  return new Date(ts * 1000).toLocaleString('zh-CN')
}

function initMap() {
  AMapLoader.load({
    key: '',
    version: '2.0',
    plugins: ['AMap.Scale'],
  }).then((AMap) => {
    map = new AMap.Map('map-container', {
      zoom: 15,
      center: [116.397428, 39.90923],
      viewMode: '2D',
    })

    map.on('click', (e) => {
      const point = { lat: e.lnglat.getLat(), lng: e.lnglat.getLng() }
      waypoints.value.push(point)

      const m = new AMap.Marker({
        position: [point.lng, point.lat],
        label: {
          content: `<span style="background:#409eff;color:#fff;padding:2px 6px;border-radius:3px;font-size:12px;">${waypoints.value.length}</span>`,
          direction: 'top',
        },
      })
      m.setMap(map)
      markersArray.push(m)

      updatePolyline(AMap)
    })
  }).catch((e) => {
    console.warn('高德地图加载失败，请检查 API Key:', e)
  })
}

function updatePolyline(AMap) {
  if (polyline) polyline.setMap(null)
  if (waypoints.value.length < 2) return

  const path = waypoints.value.map(p => [p.lng, p.lat])
  polyline = new AMap.Polyline({
    path,
    strokeColor: '#409eff',
    strokeWeight: 4,
    strokeOpacity: 0.8,
    lineJoin: 'round',
  })
  polyline.setMap(map)
}

onMounted(() => {
  fetchDevices()
  nextTick(() => initMap())
})

onUnmounted(() => {
  if (map) {
    map.destroy()
    map = null
  }
})
</script>

<style scoped>
.page-header {
  display: flex;
  justify-content: space-between;
  align-items: flex-start;
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

.card-title {
  display: flex;
  align-items: center;
  gap: 8px;
  font-weight: 600;
}

.controller-area {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 24px;
}

.dpad {
  position: relative;
  width: 200px;
  height: 200px;
}

.dpad-btn {
  position: absolute;
  width: 60px;
  height: 60px;
  border: none;
  border-radius: 12px;
  background: #f0f2f5;
  color: #606266;
  cursor: pointer;
  transition: all 0.1s;
  display: flex;
  align-items: center;
  justify-content: center;
  box-shadow: 0 2px 6px rgba(0, 0, 0, 0.08);
}

.dpad-btn:hover:not(:disabled) {
  background: #e4e7ed;
  transform: scale(1.05);
}

.dpad-btn.active:not(:disabled) {
  background: #409eff;
  color: #fff;
  box-shadow: 0 4px 12px rgba(64, 158, 255, 0.4);
}

.dpad-btn:disabled {
  opacity: 0.5;
  cursor: not-allowed;
}

.dpad-up    { top: 0;   left: 50%; transform: translateX(-50%); }
.dpad-down  { bottom: 0; left: 50%; transform: translateX(-50%); }
.dpad-left  { left: 0;  top: 50%; transform: translateY(-50%); }
.dpad-right { right: 0; top: 50%; transform: translateY(-50%); }
.dpad-center {
  top: 50%;
  left: 50%;
  transform: translate(-50%, -50%);
  width: 56px;
  height: 56px;
  background: #fff3e0;
  color: #e6a23c;
  border-radius: 50%;
}

.speed-control {
  width: 100%;
  padding: 0 16px;
}

.speed-control label {
  font-size: 13px;
  color: #606266;
  margin-bottom: 8px;
  display: block;
}

.command-log {
  width: 100%;
}

.command-log h4 {
  margin: 16px 0 8px;
  font-size: 14px;
  color: #606266;
}

.log-list {
  max-height: 160px;
  overflow-y: auto;
  background: #fafafa;
  border-radius: 8px;
  padding: 8px;
}

.log-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 4px 0;
  font-size: 13px;
}

.log-time {
  color: #909399;
  font-size: 11px;
}

.log-empty {
  text-align: center;
  color: #c0c4cc;
  font-size: 13px;
  padding: 20px 0;
  margin: 0;
}

.map-card .map-container {
  width: 100%;
  height: 420px;
  border-radius: 8px;
  overflow: hidden;
  background: #eee;
}

.waypoint-info {
  margin-top: 12px;
}

.last-route {
  margin-top: 12px;
}
</style>

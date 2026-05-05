<template>
  <div class="control-panel-container">
    <el-card class="control-card">
      <template #header>
        <div class="card-header">
          <span><el-icon><SetUp /></el-icon> 远程控制面板</span>
          <el-tag :type="isAdmin ? 'success' : 'warning'">
            {{ isAdmin ? 'Admin 管理员' : 'User 租户' }}
          </el-tag>
        </div>
      </template>

      <el-tabs v-model="activeTab">
        <el-tab-pane label="🎮 遥控摇杆" name="control">
          <div class="device-selector">
            <el-select v-model="selectedDevice" placeholder="选择目标设备" style="width: 100%;">
              <el-option
                v-for="device in devices"
                :key="device.device_id"
                :label="`${device.name} (${device.device_id})`"
                :value="device.device_id"
              />
            </el-select>
          </div>

          <div class="control-pad">
            <div class="button-row">
              <el-button
                type="primary"
                size="large"
                circle
                class="control-btn forward"
                :disabled="!selectedDevice || sendingCommand"
                @click="sendCommand('forward')"
              >
                <el-icon><Top /></el-icon>
              </el-button>
            </div>

            <div class="button-row middle-row">
              <el-button
                type="primary"
                size="large"
                circle
                class="control-btn left"
                :disabled="!selectedDevice || sendingCommand"
                @click="sendCommand('left')"
              >
                <el-icon><Back /></el-icon>
              </el-button>

              <el-button
                type="danger"
                size="large"
                circle
                class="control-btn stop"
                :disabled="!selectedDevice || sendingCommand"
                @click="sendCommand('stop')"
              >
                <el-icon><VideoPause /></el-icon>
              </el-button>

              <el-button
                type="primary"
                size="large"
                circle
                class="control-btn right"
                :disabled="!selectedDevice || sendingCommand"
                @click="sendCommand('right')"
              >
                <el-icon><Right /></el-icon>
              </el-button>
            </div>

            <div class="button-row">
              <el-button
                type="primary"
                size="large"
                circle
                class="control-btn backward"
                :disabled="!selectedDevice || sendingCommand"
                @click="sendCommand('backward')"
              >
                <el-icon><Bottom /></el-icon>
              </el-button>
            </div>
          </div>

          <div class="command-log" v-if="commandHistory.length > 0">
            <h4>指令日志</h4>
            <el-timeline>
              <el-timeline-item
                v-for="(cmd, index) in commandHistory.slice(-10)"
                :key="index"
                :type="cmd.success ? 'success' : 'danger'"
                :timestamp="cmd.time"
              >
                {{ cmd.action }} - {{ cmd.message }}
              </el-timeline-item>
            </el-timeline>
          </div>
        </el-tab-pane>

        <el-tab-pane label="🗺️ 路线规划" name="route">
          <div class="route-section">
            <el-alert
              title="在下方地图上点击打点生成巡航航点，确认后下发给设备执行自动巡航（仅 Admin 或设备 Owner 可操作）"
              type="info"
              show-icon
              :closable="false"
              style="margin-bottom: 15px;"
            />

            <div class="route-device-selector">
              <span>目标设备：</span>
              <el-select v-model="routeDeviceId" placeholder="选择设备" style="flex: 1;">
                <el-option
                  v-for="device in devices"
                  :key="device.device_id"
                  :label="`${device.name} (${device.device_id})`"
                  :value="device.device_id"
                />
              </el-select>
            </div>

            <div id="route-map-container" ref="routeMapContainer"></div>

            <div class="waypoints-info">
              <el-tag type="info">已标记航点: {{ waypoints.length }} 个</el-tag>
              <div class="waypoint-list" v-if="waypoints.length > 0">
                <el-tag
                  v-for="(wp, index) in waypoints"
                  :key="index"
                  closable
                  @close="removeWaypoint(index)"
                  style="margin: 4px;"
                >
                  #{{ index + 1 }} ({{ wp.lat.toFixed(4) }}, {{ wp.lng.toFixed(4) }})
                </el-tag>
              </div>
            </div>

            <div class="route-actions">
              <el-button @click="clearWaypoints">清空航点</el-button>
              <el-button
                type="primary"
                :disabled="waypoints.length < 2 || !routeDeviceId || dispatchingRoute"
                :loading="dispatchingRoute"
                @click="dispatchRoute"
              >
                下发巡航路线 ({{ waypoints.length }} 个航点)
              </el-button>
            </div>

            <el-divider />

            <div class="recent-routes">
              <h4>最近下发的路线</h4>
              <el-button
                link
                type="primary"
                size="small"
                @click="fetchRecentRoute"
                :loading="loadingRoute"
              >
                刷新
              </el-button>
              <div v-if="recentRoute" class="route-detail">
                <p><strong>设备:</strong> {{ recentRoute.device_id }}</p>
                <p><strong>航点数:</strong> {{ recentRoute.waypoint_count }}</p>
                <p><strong>下发时间:</strong> {{ formatTime(recentRoute.dispatched_at) }}</p>
                <div class="route-waypoints-list">
                  <el-tag
                    v-for="(wp, index) in recentRoute.waypoints"
                    :key="index"
                    size="small"
                    style="margin: 2px;"
                  >
                    P{{ index + 1 }}: ({{ wp.lat }}, {{ wp.lng }})
                  </el-tag>
                </div>
              </div>
              <el-empty v-else description="暂无路线记录" :image-size="60" />
            </div>
          </div>
        </el-tab-pane>
      </el-tabs>
    </el-card>

    <el-dialog
      v-model="showPermissionDialog"
      title="⚠️ 权限受限"
      width="400px"
      :close-on-click-modal="false"
    >
      <el-alert
        title="无权限：鉴权受阻"
        description="您的账户无权执行此操作。可能原因：1) 尝试控制他人的设备；2) User 角色无法下发全局指令。"
        type="error"
        show-icon
        :closable="false"
      />
      <p style="margin-top: 15px; color: #909399; font-size: 13px;">
        如需完整权限，请联系管理员将您的角色升级为 Admin。
      </p>

      <template #footer>
        <el-button type="primary" @click="showPermissionDialog = false">我知道了</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import AMapLoader from '@amap/amap-jsapi-loader'
import api from '@/api'

const activeTab = ref('control')
const selectedDevice = ref('')
const routeDeviceId = ref('')
const devices = ref([])
const sendingCommand = ref(false)
const dispatchingRoute = ref(false)
const loadingRoute = ref(false)
const commandHistory = ref([])
const showPermissionDialog = ref(false)

let routeMap = null
const routeMapContainer = ref(null)
const waypoints = ref([])
let routeMarkerList = []
let routePolyline = null
const recentRoute = ref(null)

const isAdmin = computed(() => localStorage.getItem('role') === 'admin')

window._AMapSecurityConfig = {
  securityJsCode: 'YOUR_AMAP_SECURITY_CODE'
}

const fetchDevices = async () => {
  try {
    const response = await api.get('/devices')
    devices.value = response.devices || response

    if (devices.value.length > 0 && !selectedDevice.value) {
      selectedDevice.value = devices.value[0].device_id
      routeDeviceId.value = devices.value[0].device_id
    }
  } catch (error) {
    console.error('Fetch devices error:', error)
  }
}

const sendCommand = async (action) => {
  if (!selectedDevice.value) {
    ElMessage.warning('请先选择目标设备')
    return
  }

  sendingCommand.value = true

  try {
    await api.post('/vehicle/command', {
      device_id: selectedDevice.value,
      command: action
    })

    const time = new Date().toLocaleTimeString('zh-CN')
    commandHistory.value.push({
      action: `CMD:${action.toUpperCase()}`,
      message: '指令发送成功',
      success: true,
      time: time
    })

    ElMessage.success(`指令 ${action.toUpperCase()} 已下发`)
  } catch (error) {
    if (error.response?.status === 403) {
      showPermissionDialog.value = true

      const time = new Date().toLocaleTimeString('zh-CN')
      commandHistory.value.push({
        action: `CMD:${action.toUpperCase()}`,
        message: '权限不足，被 RBAC 拦截',
        success: false,
        time: time
      })
    } else {
      console.error('Send command error:', error)
    }
  } finally {
    sendingCommand.value = false
  }
}

const initRouteMap = async () => {
  await nextTick()
  try {
    const AMap = await AMapLoader.load({
      key: 'YOUR_AMAP_API_KEY',
      version: '2.0',
      plugins: ['AMap.Scale', 'AMap.ToolBar']
    })

    routeMap = new AMap.Map(routeMapContainer.value, {
      viewMode: '2D',
      zoom: 15,
      center: [116.4074, 39.9042]
    })

    routeMap.addControl(new AMap.Scale())
    routeMap.addControl(new AMap.ToolBar({ position: 'RT' }))

    routeMap.on('click', (e) => {
      const point = { lat: e.lnglat.getLat(), lng: e.lnglat.getLng() }
      addWaypoint(point)
    })
  } catch (error) {
    console.error('Init route map error:', error)
  }
}

const addWaypoint = (point) => {
  waypoints.value.push(point)

  const marker = new AMap.Marker({
    position: [point.lng, point.lat],
    label: {
      content: `<div class="waypoint-label">#${waypoints.value.length}</div>`,
      direction: 'top'
    },
    animation: 'AMAP_ANIMATION_DROP'
  })

  routeMap.add(marker)
  routeMarkerList.push(marker)

  updatePolyline()
}

const removeWaypoint = (index) => {
  waypoints.value.splice(index, 1)

  if (routeMarkerList[index]) {
    routeMap.remove(routeMarkerList[index])
    routeMarkerList.splice(index, 1)
  }

  routeMarkerList.forEach((marker, i) => {
    marker.setLabel({ content: `<div class="waypoint-label">#${i + 1}</div>` })
  })

  updatePolyline()
}

const clearWaypoints = () => {
  waypoints.value = []

  routeMarkerList.forEach(marker => routeMap.remove(marker))
  routeMarkerList = []

  if (routePolyline) {
    routeMap.remove(routePolyline)
    routePolyline = null
  }
}

const updatePolyline = () => {
  if (routePolyline) {
    routeMap.remove(routePolyline)
  }

  if (waypoints.value.length >= 2) {
    const path = waypoints.value.map(wp => [wp.lng, wp.lat])
    routePolyline = new AMap.Polyline({
      path: path,
      strokeColor: '#409EFF',
      strokeWeight: 3,
      strokeStyle: 'solid'
    })
    routeMap.add(routePolyline)
  }
}

const dispatchRoute = async () => {
  if (!routeDeviceId.value) {
    ElMessage.warning('请先选择目标设备')
    return
  }

  if (waypoints.value.length < 2) {
    ElMessage.warning('至少需要标记 2 个航点才能下发巡航路线')
    return
  }

  dispatchingRoute.value = true

  try {
    const response = await api.post('/vehicle/route', {
      device_id: routeDeviceId.value,
      waypoints: waypoints.value
    })

    ElMessage.success(`巡航路线已成功下发！共 ${response.waypoint_count} 个航点`)
    clearWaypoints()
    fetchRecentRoute()
  } catch (error) {
    if (error.response?.status === 403) {
      showPermissionDialog.value = true
    } else {
      console.error('Dispatch route error:', error)
    }
  } finally {
    dispatchingRoute.value = false
  }
}

const fetchRecentRoute = async () => {
  if (!routeDeviceId.value) return

  loadingRoute.value = true
  try {
    const response = await api.get(`/vehicle/route/${routeDeviceId.value}`)
    recentRoute.value = response
  } catch (error) {
    if (error.response?.status === 404) {
      recentRoute.value = null
    } else {
      console.error('Fetch route error:', error)
    }
  } finally {
    loadingRoute.value = false
  }
}

const formatTime = (timestamp) => {
  if (!timestamp) return '-'
  return new Date(timestamp * 1000).toLocaleString('zh-CN')
}

onMounted(() => {
  fetchDevices()
  initRouteMap()
})
</script>

<style scoped>
.control-panel-container {
  padding: 20px;
  min-height: calc(100vh - 56px);
  display: flex;
  justify-content: center;
  align-items: flex-start;
  background-color: #f5f7fa;
  overflow-y: auto;
}

.control-card {
  width: 900px;
  margin-top: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 18px;
  font-weight: 600;
}

.device-selector,
.route-device-selector {
  margin-bottom: 25px;
  display: flex;
  align-items: center;
  gap: 12px;
}

.route-device-selector span {
  font-weight: 600;
  white-space: nowrap;
}

.control-pad {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 15px;
  padding: 30px 0;
}

.button-row {
  display: flex;
  justify-content: center;
  gap: 15px;
}

.middle-row {
  gap: 25px;
}

.control-btn {
  width: 80px;
  height: 80px;
  font-size: 28px;
  transition: all 0.2s;
}

.control-btn:hover:not(:disabled) {
  transform: scale(1.1);
  box-shadow: 0 4px 12px rgba(64, 158, 255, 0.4);
}

.control-btn:active:not(:disabled) {
  transform: scale(0.95);
}

.control-btn.stop {
  width: 90px;
  height: 90px;
  font-size: 32px;
}

.command-log {
  margin-top: 30px;
  padding-top: 20px;
  border-top: 1px solid #ebeef5;
}

.command-log h4 {
  font-size: 16px;
  color: #303133;
  margin-bottom: 15px;
}

.route-section {
  width: 100%;
}

#route-map-container {
  width: 100%;
  height: 350px;
  border-radius: 8px;
  border: 2px solid #e4e7ed;
  margin: 15px 0;
  cursor: crosshair;
}

.waypoints-info {
  margin: 15px 0;
}

.waypoint-list {
  margin-top: 8px;
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}

.route-actions {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin: 15px 0;
  padding: 15px;
  background: #f5f7fa;
  border-radius: 8px;
}

.recent-routes h4 {
  font-size: 14px;
  color: #303133;
  margin-bottom: 10px;
  display: inline-block;
}

.recent-routes {
  display: flex;
  flex-direction: column;
  gap: 10px;
}

.route-detail {
  background: white;
  padding: 15px;
  border-radius: 6px;
  border: 1px solid #ebeef5;
  font-size: 13px;
}

.route-detail p {
  margin: 6px 0;
}

.route-detail strong {
  color: #303133;
}

.route-waypoints-list {
  margin-top: 10px;
  display: flex;
  flex-wrap: wrap;
  gap: 4px;
}
</style>

<style>
.waypoint-label {
  background: #409EFF;
  color: white;
  padding: 2px 6px;
  border-radius: 50%;
  font-size: 11px;
  font-weight: bold;
}
</style>

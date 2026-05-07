<template>
  <div class="control-panel">
    <el-row :gutter="16">
      <el-col :span="24">
        <h2 style="margin-bottom: 8px;">🚗 车辆控制台</h2>
      </el-col>

      <el-col :span="24">
        <el-card shadow="hover" class="panel-card" style="margin-bottom:16px;">
          <div style="display:flex;align-items:center;gap:10px;flex-wrap:wrap;">
            <span style="font-size:13px;color:#606266;white-space:nowrap;">🗺️ 高德地图 API Key:</span>
            <el-input
              v-model="amapKeyInput"
              placeholder="请输入高德地图 Web端(JS API) Key"
              :show-password="true"
              size="small"
              style="width:380px;"
              clearable
            />
            <el-button type="primary" size="small" @click="applyAmapKey" :loading="mapLoading">
              应用并加载地图
            </el-button>
            <el-tag v-if="hasValidAmapKey" type="success" size="small" effect="light">✅ 已配置</el-tag>
            <el-tag v-else type="warning" size="small" effect="light">⚠️ 未配置</el-tag>
          </div>
        </el-card>
      </el-col>

      <el-col :span="6">
        <el-card shadow="hover" class="panel-card">
          <template #header><span>📱 设备选择</span></template>
          <el-select
            v-model="selectedDeviceId"
            placeholder="请选择设备"
            style="width:100%"
            @change="onDeviceChange"
          >
            <el-option
              v-for="d in deviceList"
              :key="d.device_id"
              :label="`${d.name} (${d.status})`"
              :value="d.device_id"
            />
          </el-select>
        </el-card>
      </el-col>

      <el-col :span="18">
        <el-card shadow="hover" class="panel-card">
          <template #header><span>🎮 远程指令</span></template>
          <div class="control-buttons">
            <el-button type="primary" size="large" :disabled="!selectedDeviceId" @click="sendCmd('forward')">
              ⬆️ 前进
            </el-button>
            <el-button type="primary" size="large" :disabled="!selectedDeviceId" @click="sendCmd('left')">
              ⬅️ 左转
            </el-button>
            <el-button type="danger" size="large" :disabled="!selectedDeviceId" @click="sendCmd('stop')">
              🛑 停止
            </el-button>
            <el-button type="primary" size="large" :disabled="!selectedDeviceId" @click="sendCmd('right')">
              ➡️ 右转
            </el-button>
            <el-button type="warning" size="large" :disabled="!selectedDeviceId" @click="sendCmd('backward')">
              ⬇️ 后退
            </el-button>
          </div>
        </el-card>
      </el-col>

      <el-col :span="12">
        <el-card shadow="hover" class="panel-card">
          <template #header>
            <span>🗺️ 巡航路线规划</span>
          </template>
          <div id="map-container" class="map-container"></div>
          <div style="margin-top:10px;display:flex;gap:8px;align-items:center;">
            <span style="font-size:13px;color:#666;">已选 {{ waypoints.length }} 个航点</span>
            <el-button size="small" type="danger" plain @click="clearWaypoints">清空</el-button>
            <el-button size="small" type="success" :loading="routeLoading" :disabled="waypoints.length<2 || !selectedDeviceId" @click="dispatchRoute">
              下发巡航路线
            </el-button>
          </div>
          <div v-if="lastRouteInfo" style="margin-top:8px;font-size:13px;color:#67c23a;">
            ✅ 已下发 {{ lastRouteInfo.waypoint_count }} 个航点 ({{ formatTime(lastRouteInfo.dispatched_at) }})
          </div>
        </el-card>
      </el-col>

      <el-col :span="12">
        <el-card shadow="hover" class="panel-card">
          <template #header><span>📍 实时轨迹</span></template>
          <div id="trajectory-map" class="map-container"></div>
          <div style="margin-top:8px;display:flex;justify-content:space-between;align-items:center;">
            <span style="font-size:13px;">
              <el-tag :type="cruiseStateTagType" size="small">{{ cruiseStateLabel }}</el-tag>
              <span v-if="cruiseStatus?.stats" style="margin-left:8px;color:#666;">
                {{ cruiseStatus.stats.total_waypoints_reached || 0 }}/{{ cruiseStatus.stats.wp_total || 0 }} 航点
                · {{ cruiseStatus.stats.distance_traveled_m || 0 }}m
              </span>
            </span>
            <el-button size="small" @click="refreshTrajectory">刷新轨迹</el-button>
          </div>
        </el-card>
      </el-col>

      <el-col :span="24">
        <el-card shadow="hover" class="panel-card">
          <template #header>
            <span>🧭 自动驾驶状态监控</span>
            <el-button size="small" style="float:right;margin-top:-4px;" @click="refreshCruiseStatus">刷新</el-button>
          </template>
          <el-row :gutter="16">
            <el-col :span="6">
              <div class="stat-item">
                <div class="stat-label">导航状态</div>
                <div class="stat-value" :style="{ color: navStateColor }">{{ cruiseStatusLabel }}</div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-item">
                <div class="stat-label">当前航点</div>
                <div class="stat-value">{{ currentWpDisplay }}</div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-item">
                <div class="stat-label">巡航时长</div>
                <div class="stat-value">{{ cruiseDurationDisplay }}</div>
              </div>
            </el-col>
            <el-col :span="6">
              <div class="stat-item">
                <div class="stat-label">行驶距离</div>
                <div class="stat-value">{{ distanceDisplay }}</div>
              </div>
            </el-col>
          </el-row>
          <el-row :gutter="16" style="margin-top:12px;">
            <el-col :span="4">
              <div class="stat-mini">
                <span class="mini-label">纬度</span>
                <span class="mini-value">{{ positionData?.lat ?? '--' }}</span>
              </div>
            </el-col>
            <el-col :span="4">
              <div class="stat-mini">
                <span class="mini-label">经度</span>
                <span class="mini-value">{{ positionData?.lng ?? '--' }}</span>
              </div>
            </el-col>
            <el-col :span="3">
              <div class="stat-mini">
                <span class="mini-label">速度(PWM)</span>
                <span class="mini-value">{{ positionData?.speed_pwm ?? '--' }}</span>
              </div>
            </el-col>
            <el-col :span="3">
              <div class="stat-mini">
                <span class="mini-label">温度</span>
                <span class="mini-value">{{ positionData?.temperature ? positionData.temperature + '°C' : '--' }}</span>
              </div>
            </el-col>
            <el-col :span="3">
              <div class="stat-mini">
                <span class="mini-label">湿度</span>
                <span class="mini-value">{{ positionData?.humidity ? positionData.humidity + '%' : '--' }}</span>
              </div>
            </el-col>
            <el-col :span="4">
              <div class="stat-mini">
                <span class="mini-label">超声波(cm)</span>
                <span class="mini-value">{{ positionData?.ultrasonic_cm ?? '--' }}</span>
              </div>
            </el-col>
            <el-col :span="3">
              <div class="stat-mini">
                <span class="mini-label">卫星数</span>
                <span class="mini-value">{{ positionData?.satellites ?? '--' }}</span>
              </div>
            </el-col>
          </el-row>
        </el-card>
      </el-col>

      <el-col :span="24">
        <el-card shadow="hover" class="panel-card">
          <template #header>
            <span>📋 导航事件时间线</span>
            <el-button size="small" style="float:right;margin-top:-4px;" @click="loadNavEvents">刷新</el-button>
          </template>
          <el-timeline v-if="navEvents.length > 0">
            <el-timeline-item
              v-for="(evt, idx) in navEvents"
              :key="evt.id"
              :timestamp="formatTime(evt.occurred_at)"
              :type="eventTimelineType(evt.event_type)"
              placement="top"
            >
              <div class="nav-event-item">
                <el-tag :type="eventTypeTag(evt.event_type)" size="small">{{ evt.event_type }}</el-tag>
                <span class="evt-detail">{{ evt.detail }}</span>
                <span v-if="evt.lat && evt.lng" class="evt-coords">
                  📍 {{ Number(evt.lat).toFixed(5) }}, {{ Number(evt.lng).toFixed(5) }}
                </span>
                <span v-if="evt.wp_index != null" class="evt-wp">
                  航点 {{ evt.wp_index }}/{{ evt.wp_total }}
                </span>
              </div>
            </el-timeline-item>
          </el-timeline>
          <el-empty v-else description="暂无导航事件，下发巡航路线后将在此显示事件记录" />
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { ElMessage } from 'element-plus'
import AMapLoader from '@amap/amap-jsapi-loader'
import { vehicleAPI, deviceAPI } from '@/api'

const deviceList = ref([])
const selectedDeviceId = ref('')
const commandLoading = ref(false)
const routeLoading = ref(false)
const waypoints = ref([])
const lastRouteInfo = ref(null)
const amapKeyInput = ref(localStorage.getItem('amap_api_key') || import.meta.env.VITE_AMAP_API_KEY || '')
const mapLoading = ref(false)
const hasValidAmapKey = computed(() => !!amapKeyInput.value?.trim())

let map = null
let trajectoryMap = null
let markersArray = []
let polyline = null
let trajectoryPolyline = null
let positionMarker = null

const navEvents = ref([])
const cruiseStatus = ref(null)
const positionData = ref(null)

let posPollTimer = null
let navEventTimer = null

function getAmapKey() {
  return amapKeyInput.value?.trim() || ''
}

function applyAmapKey() {
  const key = amapKeyInput.value?.trim()
  if (!key) {
    ElMessage.warning('请输入有效的 API Key')
    return
  }
  localStorage.setItem('amap_api_key', key)
  mapLoading.value = true
  if (map) { map.destroy(); map = null }
  if (trajectoryMap) { trajectoryMap.destroy(); trajectoryMap = null }
  markersArray = []
  polyline = null
  trajectoryPolyline = null
  if (positionMarker) { positionMarker.setMap(null); positionMarker = null }

  document.getElementById('map-container').innerHTML = ''
  document.getElementById('trajectory-map').innerHTML = ''

  AMapLoader.load({
    key: key,
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

    trajectoryMap = new AMap.Map('trajectory-map', {
      zoom: 15,
      center: [116.397428, 39.90923],
      viewMode: '2D',
    })
    mapLoading.value = false
    ElMessage.success('地图加载成功')
    refreshTrajectory()
  }).catch((e) => {
    console.warn('高德地图加载失败:', e)
    mapLoading.value = false
    ElMessage.error('地图加载失败，请检查 API Key 是否正确')
  })
}

function initMap() {
  if (!AMapLoader) return
  const amapKey = getAmapKey()
  if (!amapKey) {
    document.getElementById('map-container').innerHTML =
      '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#909399;font-size:14px;background:#f5f7fa;border-radius:6px;">⚠️ 请在上方输入高德地图 API Key 并点击"应用"</div>'
    document.getElementById('trajectory-map').innerHTML =
      '<div style="display:flex;align-items:center;justify-content:center;height:100%;color:#909399;font-size:14px;background:#f5f7fa;border-radius:6px;">⚠️ 请在上方输入高德地图 API Key 并点击"应用"</div>'
    return
  }
  applyAmapKey()
}

function updatePolyline(AMap) {
  if (!map || waypoints.value.length < 1) return
  const path = waypoints.value.map(p => [p.lng, p.lat])
  if (polyline) {
    map.remove(polyline)
  }
  polyline = new AMap.Polyline({
    path,
    strokeColor: '#409eff',
    strokeWeight: 3,
    strokeOpacity: 0.7,
  })
  polyline.setMap(map)
}

function clearWaypoints() {
  waypoints.value = []
  lastRouteInfo.value = null
  if (map) {
    markersArray.forEach(m => m.setMap(null))
    markersArray = []
    if (polyline) { map.remove(polyline); polyline = null }
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
    loadNavEvents()
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '路线下发失败')
  } finally {
    routeLoading.value = false
  }
}

async function sendCmd(cmd) {
  commandLoading.value = true
  try {
    await vehicleAPI.sendCommand(selectedDeviceId.value, cmd)
    ElMessage.success(`指令 "${cmd}" 已发送`)
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '指令发送失败')
  } finally {
    commandLoading.value = false
  }
}

async function loadDevices() {
  try {
    const data = await deviceAPI.list()
    deviceList.value = data.devices || data || []
  } catch (err) {
    ElMessage.error('获取设备列表失败')
  }
}

async function refreshCruiseStatus() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getCruiseStatus(selectedDeviceId.value)
    cruiseStatus.value = res
  } catch (err) {
    // ignore
  }
}

async function fetchPosition() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getPosition(selectedDeviceId.value)
    positionData.value = res
    drawPositionMarker(res)
  } catch (err) {
    // ignore - no data yet
  }
}

function drawPositionMarker(pos) {
  if (!trajectoryMap || !pos.lat || !pos.lng) return
  if (positionMarker) {
    positionMarker.setPosition([pos.lng, pos.lat])
  } else {
    positionMarker = new AMap.Marker({
      position: [pos.lng, pos.lat],
      icon: new AMap.Icon({
        image: '//a.amap.com/jsapi_demos/static/demo-center/icons/poi-marker-red.png',
        size: [25, 34],
        imageSize: [25, 34],
      }),
    })
    positionMarker.setMap(trajectoryMap)
  }
}

async function refreshTrajectory() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getTrajectory(selectedDeviceId.value, 2, 2000)
    if (res.points && res.points.length > 0) {
      drawTrajectory(res.points)
      if (res.bounds) {
        trajectoryMap.setBounds(
          [[res.bounds.min_lng, res.bounds.min_lat], [res.bounds.max_lng, res.bounds.max_lat]]
        )
      }
    }
  } catch (err) {
    // ignore
  }
}

function drawTrajectory(points) {
  if (!trajectoryMap || points.length < 2) return
  const path = points.map(p => [p.lng, p.lat])
  if (trajectoryPolyline) {
    trajectoryMap.remove(trajectoryPolyline)
  }
  trajectoryPolyline = new AMap.Polyline({
    path,
    strokeColor: '#f56c6c',
    strokeWeight: 4,
    strokeOpacity: 0.85,
    lineJoin: 'round',
  })
  trajectoryPolyline.setMap(trajectoryMap)
}

async function loadNavEvents() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getNavEvents(selectedDeviceId.value, null, 50)
    navEvents.value = res.events || []
  } catch (err) {
    navEvents.value = []
  }
}

function onDeviceChange(deviceId) {
  clearWaypoints()
  navEvents.value = []
  cruiseStatus.value = null
  positionData.value = null
  if (positionMarker) { positionMarker.setMap(null); positionMarker = null }
  if (trajectoryPolyline) { trajectoryMap.remove(trajectoryPolyline); trajectoryPolyline = null }

  if (deviceId) {
    loadNavEvents()
    refreshCruiseStatus()
    fetchPosition()
    startPolling()
  } else {
    stopPolling()
  }
}

function startPolling() {
  stopPolling()
  posPollTimer = setInterval(() => {
    fetchPosition()
    refreshCruiseStatus()
  }, 3000)
  navEventTimer = setInterval(loadNavEvents, 8000)
}

function stopPolling() {
  if (posPollTimer) { clearInterval(posPollTimer); posPollTimer = null }
  if (navEventTimer) { clearInterval(navEventTimer); navEventTimer = null }
}

function formatTime(ts) {
  if (!ts) return ''
  const d = new Date(ts)
  const pad = n => String(n).padStart(2, '0')
  return `${d.getFullYear()}-${pad(d.getMonth()+1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}:${pad(d.getSeconds())}`
}

function eventTypeTag(type) {
  const map = {
    CRUISE_STARTED: '',
    WAYPOINT_REACHED: 'success',
    ARRIVED: 'success',
    OBSTACLE_DETECTED: 'warning',
    OBSTACLE_CLEARED: 'info',
    EMERGENCY_STOP: 'danger',
    ABORTED: 'danger',
    ONLINE: 'info',
  }
  return map[type] || 'info'
}

function eventTimelineType(type) {
  const map = {
    CRUISE_STARTED: 'primary',
    WAYPOINT_REACHED: 'success',
    ARRIVED: 'success',
    OBSTACLE_DETECTED: 'warning',
    EMERGENCY_STOP: 'danger',
    ABORTED: 'danger',
  }
  return map[type] || 'info'
}

const cruiseStateLabel = computed(() => {
  const s = cruiseStatus.value?.nav_state
  const labels = {
    idle: '空闲', dispatched: '已下发', cruising: '巡航中',
    avoiding: '避障中', arrived: '已到达', aborted: '已中止', unknown: '未知',
  }
  return labels[s] || s || '--'
})
const cruiseStateTagType = computed(() => {
  const s = cruiseStatus.value?.nav_state
  const types = {
    idle: 'info', dispatched: '', cruising: 'success',
    avoiding: 'warning', arrived: 'success', aborted: 'danger', unknown: 'info',
  }
  return types[s] || 'info'
})
const navStateColor = computed(() => {
  const s = cruiseStatus.value?.nav_state
  const colors = {
    idle: '#909399', dispatched: '#409eff', cruising: '#67c23a',
    avoiding: '#e6a23c', arrived: '#67c23a', aborted: '#f56c6c', unknown: '#909399',
  }
  return colors[s] || '#909399'
})
const cruiseStatusLabel = cruiseStateLabel
const currentWpDisplay = computed(() => {
  const cs = cruiseStatus.value
  if (!cs) return '--'
  const idx = cs.position?.wp_index ?? cs.nav_info?.wp_index ?? 0
  const total = cs.position?.wp_total ?? cs.nav_info?.wp_total ?? 0
  return total > 0 ? `${idx + 1} / ${total}` : '--'
})
const cruiseDurationDisplay = computed(() => {
  const d = cruiseStatus.value?.stats?.cruise_duration_s
  if (!d || d <= 0) return '--'
  const m = Math.floor(d / 60)
  const s = d % 60
  return m > 0 ? `${m}分${s}秒` : `${s}秒`
})
const distanceDisplay = computed(() => {
  const d = cruiseStatus.value?.stats?.distance_traveled_m
  return d != null ? `${d} m` : '--'
})

onMounted(async () => {
  await loadDevices()
  await nextTick()
  initMap()
})

onUnmounted(() => {
  stopPolling()
})
</script>

<style scoped>
.control-panel { padding: 20px; }
.panel-card { margin-bottom: 16px; }
.control-buttons {
  display: flex; gap: 12px;
  justify-content: center; align-items: center;
}
.map-container {
  width: 100%; height: 320px; border-radius: 6px;
  background: #f5f7fa;
}
.stat-item {
  text-align: center; padding: 12px 0;
  border-right: 1px solid #ebeef5;
}
.stat-item:last-child { border-right: none; }
.stat-label { font-size: 13px; color: #909399; margin-bottom: 6px; }
.stat-value { font-size: 22px; font-weight: bold; }
.stat-mini {
  text-align: center; padding: 8px 4px;
  border-right: 1px solid #f0f0f0;
}
.stat-mini:last-child { border-right: none; }
.mini-label { display: block; font-size: 11px; color: #b0b0b0; }
.mini-value { display: block; font-size: 14px; font-weight: 600; color: #303133; margin-top: 2px; }
.nav-event-item { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.evt-detail { font-size: 14px; color: #303133; }
.evt-coords { font-size: 12px; color: #909399; }
.evt-wp { font-size: 12px; color: #409eff; font-weight: 500; }
</style>

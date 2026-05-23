<template>
  <div class="control-panel">
    <!-- 顶部栏 -->
    <div class="top-bar">
      <h2 class="page-title">车辆控制台</h2>
      <div class="top-bar-right">
        <el-select v-model="selectedDeviceId" placeholder="选择设备" class="device-select" @change="onDeviceChange">
          <el-option v-for="d in deviceList" :key="d.device_id" :label="`${d.name} (${d.status})`" :value="d.device_id" />
        </el-select>
        <el-radio-group v-model="controlMode" size="default" @change="onModeChange">
          <el-radio-button value="manual">手动控制</el-radio-button>
          <el-radio-button value="cruise">自动巡航</el-radio-button>
        </el-radio-group>
      </div>
    </div>

    <el-row :gutter="16">
      <!-- 左侧：控制 + 传感器 -->
      <el-col :xs="24" :sm="8" :md="6">
        <!-- 手动控制 -->
        <el-card v-if="controlMode === 'manual'" shadow="hover" class="panel-card">
          <template #header><span>方向控制</span></template>
          <div class="dpad-container">
            <div class="dpad-row">
              <div class="dpad-spacer"></div>
              <el-button type="primary" class="dpad-btn" :disabled="!selectedDeviceId" @click="sendCmd('forward')">
                <el-icon><Top /></el-icon>
              </el-button>
              <div class="dpad-spacer"></div>
            </div>
            <div class="dpad-row">
              <el-button type="primary" class="dpad-btn" :disabled="!selectedDeviceId" @click="sendCmd('left')">
                <el-icon><ArrowLeft /></el-icon>
              </el-button>
              <el-button type="danger" class="dpad-btn dpad-stop" :disabled="!selectedDeviceId" @click="sendCmd('stop')">
                <el-icon><VideoPause /></el-icon>
              </el-button>
              <el-button type="primary" class="dpad-btn" :disabled="!selectedDeviceId" @click="sendCmd('right')">
                <el-icon><ArrowRight /></el-icon>
              </el-button>
            </div>
            <div class="dpad-row">
              <div class="dpad-spacer"></div>
              <el-button type="warning" class="dpad-btn" :disabled="!selectedDeviceId" @click="sendCmd('backward')">
                <el-icon><Bottom /></el-icon>
              </el-button>
              <div class="dpad-spacer"></div>
            </div>
          </div>
        </el-card>

        <!-- 自动巡航控制 -->
        <el-card v-else shadow="hover" class="panel-card">
          <template #header>
            <span>巡航控制</span>
            <el-tag v-if="cruiseStatus" :type="cruiseStateTagType" size="small" style="margin-left:8px;">{{ cruiseStateLabel }}</el-tag>
          </template>
          <div class="cruise-actions">
            <el-button type="success" :loading="routeLoading" :disabled="!selectedDeviceId" @click="startCruise" style="flex:1;">启动巡航</el-button>
            <el-button type="danger" :disabled="!selectedDeviceId" @click="stopCruise" style="flex:1;">停止巡航</el-button>
          </div>
          <div v-if="cruiseStatus" class="cruise-detail">
            <div class="cruise-detail-row">
              <span class="detail-label">导航状态</span>
              <span class="detail-value" :style="{ color: navStateColor }">{{ cruiseStateLabel }}</span>
            </div>
            <div class="cruise-detail-row">
              <span class="detail-label">当前航点</span>
              <span class="detail-value">{{ (cruiseStatus.current_wp_index || 0) + 1 }} / {{ cruiseStatus.total_waypoints || 0 }}</span>
            </div>
            <div v-if="cruiseStatus.heading != null" class="cruise-detail-row">
              <span class="detail-label">IMU 航向</span>
              <span class="detail-value">{{ cruiseStatus.heading.toFixed(1) }}°</span>
            </div>
            <div v-if="cruiseStatus.gyro_z != null" class="cruise-detail-row">
              <span class="detail-label">角速度 Z</span>
              <span class="detail-value">{{ cruiseStatus.gyro_z.toFixed(2) }}°/s</span>
            </div>
            <div class="cruise-detail-row">
              <span class="detail-label">巡航时长</span>
              <span class="detail-value">{{ cruiseDurationDisplay }}</span>
            </div>
            <div class="cruise-detail-row">
              <span class="detail-label">行驶距离</span>
              <span class="detail-value">{{ distanceDisplay }}</span>
            </div>
          </div>
          <div v-else class="cruise-hint">选择设备后启动自动巡航</div>
        </el-card>

        <!-- 速度挡位（手动/巡航共用） -->
        <el-card shadow="hover" class="panel-card">
          <template #header><span>速度挡位</span></template>
          <div class="gear-section">
            <div class="gear-options">
              <div
                v-for="g in gearOptions"
                :key="g.value"
                class="gear-item"
                :class="{ 'gear-active': speedGear === g.value }"
                @click="speedGear = g.value; onGearChange(g.value)"
              >
                <div class="gear-icon">{{ g.icon }}</div>
                <div class="gear-name">{{ g.label }}</div>
                <div class="gear-speed">{{ g.pwm }}</div>
              </div>
            </div>
          </div>
        </el-card>

        <!-- 传感器数据 -->
        <el-card shadow="hover" class="panel-card">
          <template #header><span>传感器数据</span></template>
          <div class="sensor-list">
            <div class="sensor-row">
              <span class="sensor-label">超声波</span>
              <span class="sensor-value" :class="{ 'sensor-warn': positionData?.ultrasonic_cm < 50 }">{{ positionData?.ultrasonic_cm ?? '--' }}<small>cm</small></span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">红外</span>
              <span class="sensor-value" :style="{ color: positionData?.ir_obstacle ? '#f56c6c' : '#67c23a' }">{{ positionData?.ir_obstacle ? '障碍' : '安全' }}</span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">航向角</span>
              <span class="sensor-value">{{ positionData?.imu_heading != null ? positionData.imu_heading + '°' : '--' }}</span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">速度</span>
              <span class="sensor-value">{{ positionData?.speed_pwm ?? '--' }}<small>PWM</small></span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">温度</span>
              <span class="sensor-value">{{ positionData?.temperature ? positionData.temperature + '°C' : '--' }}</span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">湿度</span>
              <span class="sensor-value">{{ positionData?.humidity ? positionData.humidity + '%' : '--' }}</span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">卫星数</span>
              <span class="sensor-value">{{ positionData?.satellites ?? '--' }}</span>
            </div>
            <div class="sensor-row">
              <span class="sensor-label">坐标</span>
              <span class="sensor-value sensor-coord">{{ positionData?.lat ? Number(positionData.lat).toFixed(5) : '--' }}, {{ positionData?.lng ? Number(positionData.lng).toFixed(5) : '--' }}</span>
            </div>
          </div>
        </el-card>
      </el-col>

      <!-- 右侧：地图 + 事件 -->
      <el-col :xs="24" :sm="16" :md="18">
        <el-card shadow="hover" class="panel-card">
          <template #header>
            <div class="map-header">
              <span>实时位置与轨迹</span>
              <div class="map-header-actions">
                <el-tag v-if="positionData" :type="positionData.device_online ? 'success' : 'danger'" size="small">
                  {{ positionData.device_online ? '在线' : '离线' }}
                </el-tag>
                <el-button size="small" @click="refreshTrajectory">刷新轨迹</el-button>
              </div>
            </div>
          </template>
          <div id="map-container" class="map-container-large"></div>
          <div class="map-footer">
            <span v-if="positionData?.lat" class="position-info">
              {{ Number(positionData.lat).toFixed(6) }}, {{ Number(positionData.lng).toFixed(6) }}
              <span v-if="positionData.speed_kmh"> · {{ positionData.speed_kmh }} km/h</span>
              <span v-if="positionData.satellites"> · {{ positionData.satellites }} 颗卫星</span>
            </span>
            <span v-else class="position-info">暂无位置数据</span>
            <span v-if="cruiseStatus?.stats" class="trajectory-stats">
              {{ cruiseStatus.stats.total_waypoints_reached || 0 }}/{{ cruiseStatus.stats.wp_total || 0 }} 航点
              · {{ cruiseStatus.stats.distance_traveled_m || 0 }}m
            </span>
          </div>
        </el-card>

        <!-- 导航事件时间线 -->
        <el-card shadow="hover" class="panel-card">
          <template #header>
            <div class="map-header">
              <span>导航事件</span>
              <el-button size="small" @click="loadNavEvents">刷新</el-button>
            </div>
          </template>
          <el-timeline v-if="navEvents.length > 0" class="nav-timeline">
            <el-timeline-item
              v-for="evt in navEvents"
              :key="evt.id"
              :timestamp="formatTime(evt.occurred_at)"
              :type="eventTimelineType(evt.event_type)"
              placement="top"
            >
              <div class="nav-event-item">
                <el-tag :type="eventTypeTag(evt.event_type)" size="small">{{ evt.event_type }}</el-tag>
                <span class="evt-detail">{{ evt.detail }}</span>
                <span v-if="evt.lat && evt.lng" class="evt-coords">{{ Number(evt.lat).toFixed(5) }}, {{ Number(evt.lng).toFixed(5) }}</span>
                <span v-if="evt.wp_index != null" class="evt-wp">航点 {{ evt.wp_index }}/{{ evt.wp_total }}</span>
              </div>
            </el-timeline-item>
          </el-timeline>
          <el-empty v-else description="暂无导航事件" :image-size="60" />
        </el-card>
      </el-col>
    </el-row>
  </div>
</template>

<script setup>
import { ref, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Top, Bottom, ArrowLeft, ArrowRight, VideoPause } from '@element-plus/icons-vue'
import AMapLoader from '@amap/amap-jsapi-loader'
import { AMAP_KEY, AMAP_VERSION, AMAP_SECURITY_KEY } from '@/config/amap'
import { vehicleAPI, deviceAPI } from '@/api'
import { formatTime } from '@/utils/format'

const cruiseStateLabel = computed(() => {
  const s = cruiseStatus.value?.state
  const map = { idle: '空闲', cruising: '巡航中', obstacle_avoid: '避障中', arrived: '已到达', aborted: '已中止' }
  return map[s] || s || '未知'
})
const cruiseStateTagType = computed(() => {
  const s = cruiseStatus.value?.state
  const map = { idle: 'info', cruising: 'success', obstacle_avoid: 'warning', arrived: '', aborted: 'danger' }
  return map[s] || 'info'
})

const deviceList = ref([])
const selectedDeviceId = ref('')
const controlMode = ref('manual')
const speedGear = ref('mid')
const SPEED_MAP = { low: 100, mid: 150, high: 200 }
const gearOptions = [
  { value: 'low', label: '低速', icon: '🐢', pwm: 100 },
  { value: 'mid', label: '中速', icon: '🚗', pwm: 150 },
  { value: 'high', label: '高速', icon: '🏎', pwm: 200 },
]
const commandLoading = ref(false)
const routeLoading = ref(false)
const mapLoading = ref(false)

let map = null
let trajectoryPolyline = null
let positionMarker = null

const navEvents = ref([])
const cruiseStatus = ref(null)
const positionData = ref(null)

let posPollTimer = null
let navEventTimer = null

function initMap() {
  if (!AMapLoader) return
  mapLoading.value = true
  if (map) { map.destroy(); map = null }
  trajectoryPolyline = null
  positionMarker = null

  document.getElementById('map-container').innerHTML = ''

  if (AMAP_SECURITY_KEY) {
    window._AMapSecurityConfig = { securityJsCode: AMAP_SECURITY_KEY }
  }

  AMapLoader.load({
    key: AMAP_KEY,
    version: AMAP_VERSION,
    plugins: ['AMap.Scale'],
  }).then((AMap) => {
    map = new AMap.Map('map-container', {
      zoom: 15,
      center: [116.397428, 39.90923],
      viewMode: '2D',
    })
    mapLoading.value = false
    if (selectedDeviceId.value) refreshTrajectory()
  }).catch((e) => {
    console.warn('高德地图加载失败:', e)
    mapLoading.value = false
    ElMessage.error('地图组件加载失败')
  })
}

function onModeChange(mode) {
  if (mode === 'cruise' && !selectedDeviceId.value) {
    ElMessage.warning('请先选择设备')
    controlMode.value = 'manual'
    return
  }
  if (mode === 'manual' && cruiseStatus.value?.state === 'cruising') {
    ElMessageBox.confirm(
      '当前正在自动巡航中，切换到手动模式将停止巡航，是否继续？',
      '确认切换',
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' }
    ).then(() => { stopCruise() }).catch(() => { controlMode.value = 'cruise' })
  }
}

async function stopCruise() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.stopCruise(selectedDeviceId.value)
    ElMessage.success(res.message || '巡航已停止')
    cruiseStatus.value = null
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '停止巡航失败')
  }
}

async function startCruise() {
  if (!selectedDeviceId.value) return
  try {
    await ElMessageBox.confirm(
      '确认启动自动巡航？导航引擎将在后端接管控制。',
      '确认启动巡航',
      { confirmButtonText: '确认启动', cancelButtonText: '取消', type: 'warning' }
    )
  } catch { return }
  routeLoading.value = true
  try {
    const res = await vehicleAPI.startCruise(selectedDeviceId.value, [], SPEED_MAP[speedGear.value])
    ElMessage.success(res.message || '自动巡航已启动')
    loadNavEvents()
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '巡航启动失败')
  } finally {
    routeLoading.value = false
  }
}

async function sendCmd(cmd) {
  commandLoading.value = true
  try {
    await vehicleAPI.sendCommand(selectedDeviceId.value, cmd, SPEED_MAP[speedGear.value])
    ElMessage.success(`指令 "${cmd}" 已发送 (PWM ${SPEED_MAP[speedGear.value]})`)
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '指令发送失败')
  } finally {
    commandLoading.value = false
  }
}

function onGearChange(gear) {
  ElMessage.success(`切换到${gear === 'low' ? '低速' : gear === 'mid' ? '中速' : '高速'}挡 (PWM ${SPEED_MAP[gear]})`)
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
  } catch (err) { console.error('刷新巡航状态失败', err) }
}

async function fetchPosition() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getPosition(selectedDeviceId.value)
    positionData.value = res
    drawPositionMarker(res)
  } catch (err) { console.error('获取位置失败', err) }
}

function drawPositionMarker(pos) {
  if (!map || !pos.lat || !pos.lng) return
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
    positionMarker.setMap(map)
  }
  map.setCenter([pos.lng, pos.lat])
}

async function refreshTrajectory() {
  if (!selectedDeviceId.value || !map) return
  try {
    const res = await vehicleAPI.getTrajectory(selectedDeviceId.value, 2, 2000)
    if (res.points && res.points.length > 0) {
      drawTrajectory(res.points)
      if (res.bounds) {
        map.setBounds(
          [[res.bounds.min_lng, res.bounds.min_lat], [res.bounds.max_lng, res.bounds.max_lat]]
        )
      }
    }
  } catch (err) { console.error('刷新轨迹失败', err) }
}

function drawTrajectory(points) {
  if (!map || points.length < 2) return
  const path = points.map(p => [p.lng, p.lat])
  if (trajectoryPolyline) { map.remove(trajectoryPolyline) }
  trajectoryPolyline = new AMap.Polyline({
    path,
    strokeColor: '#f56c6c',
    strokeWeight: 4,
    strokeOpacity: 0.85,
    lineJoin: 'round',
  })
  trajectoryPolyline.setMap(map)
}

async function loadNavEvents() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getNavEvents(selectedDeviceId.value, null, 50)
    navEvents.value = res.events || []
  } catch (err) { navEvents.value = [] }
}

function onDeviceChange(deviceId) {
  navEvents.value = []
  cruiseStatus.value = null
  positionData.value = null
  if (positionMarker) { positionMarker.setMap(null); positionMarker = null }
  if (trajectoryPolyline) { map.remove(trajectoryPolyline); trajectoryPolyline = null }

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
  posPollTimer = setInterval(() => { fetchPosition(); refreshCruiseStatus() }, 3000)
  navEventTimer = setInterval(loadNavEvents, 8000)
}

function stopPolling() {
  if (posPollTimer) { clearInterval(posPollTimer); posPollTimer = null }
  if (navEventTimer) { clearInterval(navEventTimer); navEventTimer = null }
}

function eventTypeTag(type) {
  const map = { CRUISE_STARTED: '', WAYPOINT_REACHED: 'success', ARRIVED: 'success', OBSTACLE_DETECTED: 'warning', OBSTACLE_CLEARED: 'info', EMERGENCY_STOP: 'danger', ABORTED: 'danger', ONLINE: 'info' }
  return map[type] || 'info'
}

function eventTimelineType(type) {
  const map = { CRUISE_STARTED: 'primary', WAYPOINT_REACHED: 'success', ARRIVED: 'success', OBSTACLE_DETECTED: 'warning', EMERGENCY_STOP: 'danger', ABORTED: 'danger' }
  return map[type] || 'info'
}

const navStateColor = computed(() => {
  const s = cruiseStatus.value?.state || cruiseStatus.value?.nav_state
  const colors = { idle: '#8892a4', dispatched: '#3b82f6', cruising: '#22c55e', avoiding: '#f59e0b', arrived: '#22c55e', aborted: '#ef4444', unknown: '#8892a4' }
  return colors[s] || '#8892a4'
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

onUnmounted(() => { stopPolling() })
</script>

<style scoped>
/* ── 顶部栏 ── */
.top-bar {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 16px; flex-wrap: wrap; gap: 12px;
}
.page-title { font-size: 20px; font-weight: 600; color: var(--text-primary); margin: 0; }
.top-bar-right { display: flex; align-items: center; gap: 12px; }
.device-select { width: 200px; }

/* ── 卡片 ── */
.panel-card { margin-bottom: 16px; }

/* ── 方向键 ── */
.dpad-container {
  display: flex; flex-direction: column;
  align-items: center; gap: 6px; padding: 4px 0;
}
.dpad-row { display: flex; gap: 6px; justify-content: center; align-items: center; }
.dpad-spacer { width: 64px; }
.dpad-btn {
  width: 64px; height: 52px; font-size: 18px;
  display: inline-flex; align-items: center; justify-content: center;
  border-radius: 10px;
}
.dpad-btn .el-icon { font-size: 20px; }
.dpad-stop { border-radius: 50%; width: 52px; height: 52px; }

/* ── 挡位选择 ── */
.gear-section { width: 100%; }
.gear-options {
  display: flex; gap: 8px; justify-content: center;
}
.gear-item {
  flex: 1; display: flex; flex-direction: column; align-items: center;
  padding: 10px 4px 8px; border-radius: 10px; cursor: pointer;
  border: 2px solid var(--border-color, #dcdfe6);
  transition: all 0.25s ease;
  user-select: none;
}
.gear-item:hover {
  border-color: #409eff; background: #ecf5ff;
}
.gear-active {
  border-color: #409eff !important;
  background: linear-gradient(135deg, #ecf5ff 0%, #d9ecff 100%) !important;
  box-shadow: 0 2px 8px rgba(64, 158, 255, 0.25);
}
.gear-icon { font-size: 22px; line-height: 1; margin-bottom: 4px; }
.gear-name { font-size: 13px; font-weight: 600; color: var(--text-primary); margin-bottom: 2px; }
.gear-speed { font-size: 11px; color: var(--text-secondary); font-family: monospace; }

/* ── 巡航控制 ── */
.cruise-actions { display: flex; gap: 8px; margin-bottom: 12px; }
.cruise-detail { font-size: 13px; }
.cruise-detail-row {
  display: flex; justify-content: space-between; align-items: center;
  padding: 5px 0; border-bottom: 1px solid var(--border-color, #ebeef5);
}
.cruise-detail-row:last-child { border-bottom: none; }
.detail-label { color: var(--text-secondary); }
.detail-value { font-weight: 600; color: var(--text-primary); }
.cruise-hint { font-size: 13px; color: var(--text-secondary); text-align: center; padding: 16px 0; }

/* ── 传感器列表 ── */
.sensor-list { font-size: 13px; }
.sensor-row {
  display: flex; justify-content: space-between; align-items: center;
  padding: 6px 0; border-bottom: 1px solid var(--border-color, #ebeef5);
}
.sensor-row:last-child { border-bottom: none; }
.sensor-label { color: var(--text-secondary); }
.sensor-value { font-weight: 600; color: var(--text-primary); }
.sensor-value small { font-size: 11px; font-weight: 400; opacity: 0.6; margin-left: 2px; }
.sensor-warn { color: #f56c6c !important; }
.sensor-coord { font-size: 12px; }

/* ── 地图 ── */
.map-header { display: flex; justify-content: space-between; align-items: center; width: 100%; }
.map-header-actions { display: flex; align-items: center; gap: 8px; }
.map-container-large {
  width: 100%; height: 420px;
  border: 1px solid var(--border-color, #ebeef5);
  border-radius: 6px; background: var(--bg-primary, #f5f7fa);
}
.map-footer {
  margin-top: 8px; display: flex; justify-content: space-between;
  align-items: center; font-size: 13px; color: var(--text-secondary);
}
.position-info { }
.trajectory-stats { font-size: 12px; }

/* ── 导航事件 ── */
.nav-timeline { max-height: 260px; overflow-y: auto; padding-right: 8px; }
.nav-event-item { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.evt-detail { font-size: 14px; color: var(--text-primary); }
.evt-coords { font-size: 12px; color: var(--text-secondary); }
.evt-wp { font-size: 12px; color: #409eff; font-weight: 500; }
</style>

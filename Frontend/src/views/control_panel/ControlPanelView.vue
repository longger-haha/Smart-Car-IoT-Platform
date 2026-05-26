<template>
  <div class="control-panel">
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
      <el-col :xs="24" :md="8">
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

        <el-card v-else shadow="hover" class="panel-card">
          <template #header>
            <span>巡逻巡航</span>
            <el-tag v-if="cruiseActive" :type="cruiseStateTagType" size="small" style="margin-left:8px;">{{ cruiseStateLabel }}</el-tag>
          </template>
          <div class="cruise-actions">
            <el-button type="success" :loading="routeLoading" :disabled="!selectedDeviceId || cruiseActive" @click="startCruise" style="flex:1;">启动巡航</el-button>
            <el-button type="danger" :disabled="!selectedDeviceId || !cruiseActive" @click="stopCruise" style="flex:1;">停止巡航</el-button>
          </div>
          <div v-if="cruiseActive" class="cruise-detail">
            <div class="cruise-detail-row">
              <span class="detail-label">巡航状态</span>
              <span class="detail-value" :style="{ color: cruiseStateColor }">{{ cruiseStateLabel }}</span>
            </div>
            <div v-if="positionData?.imu_heading != null" class="cruise-detail-row">
              <span class="detail-label">IMU 航向</span>
              <span class="detail-value">{{ positionData.imu_heading.toFixed(1) }}°</span>
            </div>
            <div class="cruise-detail-row">
              <span class="detail-label">避障阶段</span>
              <span class="detail-value">{{ avoidStateLabel }}</span>
            </div>
          </div>
          <div v-else class="cruise-hint">选择设备后启动巡逻巡航，小车将自主避障行驶</div>
        </el-card>

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

        <el-card shadow="hover" class="panel-card">
          <template #header><span>传感器数据</span></template>
          <div class="sensor-grid">
            <div class="sensor-card" :class="{ 'sensor-card-warn': positionData?.ultrasonic_cm > 0 && positionData?.ultrasonic_cm < 50 }">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#3b82f6,#2563eb);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><path d="M12 2L2 22h20L12 2z"/><path d="M12 16v-4"/><path d="M12 12h.01"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">超声波</span>
                <span class="sensor-card-value">{{ positionData?.ultrasonic_cm ?? '--' }}<small>cm</small></span>
              </div>
            </div>
            <div class="sensor-card" :class="positionData?.ir_l ? 'sensor-card-danger' : 'sensor-card-ok'">
              <div class="sensor-card-icon" :style="{ background: positionData?.ir_l ? 'linear-gradient(135deg,#f87171,#ef4444)' : 'linear-gradient(135deg,#34d399,#10b981)' }">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">红外左</span>
                <span class="sensor-card-value">{{ positionData?.ir_l ? '障碍' : '安全' }}</span>
              </div>
            </div>
            <div class="sensor-card" :class="positionData?.ir_r ? 'sensor-card-danger' : 'sensor-card-ok'">
              <div class="sensor-card-icon" :style="{ background: positionData?.ir_r ? 'linear-gradient(135deg,#f87171,#ef4444)' : 'linear-gradient(135deg,#34d399,#10b981)' }">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M12 1v2M12 21v2M4.22 4.22l1.42 1.42M18.36 18.36l1.42 1.42M1 12h2M21 12h2M4.22 19.78l1.42-1.42M18.36 5.64l1.42-1.42"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">红外右</span>
                <span class="sensor-card-value">{{ positionData?.ir_r ? '障碍' : '安全' }}</span>
              </div>
            </div>
            <div class="sensor-card">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#a78bfa,#7c3aed);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><circle cx="12" cy="12" r="10"/><path d="M12 2a15 15 0 0 1 0 20M12 2a15 15 0 0 0 0 20M2 12h20"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">航向角</span>
                <span class="sensor-card-value">{{ positionData?.imu_heading != null ? positionData.imu_heading + '°' : '--' }}</span>
              </div>
            </div>
            <div class="sensor-card">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#fbbf24,#f59e0b);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><polygon points="13 2 3 14 12 14 11 22 21 10 12 10 13 2"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">速度</span>
                <span class="sensor-card-value">{{ positionData?.speed_pwm ?? '--' }}<small>PWM</small></span>
              </div>
            </div>
            <div class="sensor-card">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#f87171,#ef4444);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><path d="M14 14.76V3.5a2.5 2.5 0 0 0-5 0v11.26a4.5 4.5 0 1 0 5 0z"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">温度</span>
                <span class="sensor-card-value">{{ positionData?.temperature ? positionData.temperature + '°C' : '--' }}</span>
              </div>
            </div>
            <div class="sensor-card">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#60a5fa,#3b82f6);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><path d="M12 2.69l5.66 5.66a8 8 0 1 1-11.31 0z"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">湿度</span>
                <span class="sensor-card-value">{{ positionData?.humidity ? positionData.humidity + '%' : '--' }}</span>
              </div>
            </div>
            <div class="sensor-card">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#34d399,#10b981);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><path d="M21 10c0 7-9 13-9 13s-9-6-9-13a9 9 0 0 1 18 0z"/><circle cx="12" cy="10" r="3"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">卫星数</span>
                <span class="sensor-card-value">{{ positionData?.satellites ?? '--' }}</span>
              </div>
            </div>
            <div class="sensor-card sensor-card-wide">
              <div class="sensor-card-icon" style="background:linear-gradient(135deg,#94a3b8,#64748b);">
                <svg viewBox="0 0 24 24" width="20" height="20" fill="none" stroke="#fff" stroke-width="2"><circle cx="12" cy="10" r="3"/><path d="M12 21.7C17.3 17 20 13 20 10a8 8 0 1 0-16 0c0 3 2.7 7 8 11.7z"/></svg>
              </div>
              <div class="sensor-card-body">
                <span class="sensor-card-label">坐标</span>
                <span class="sensor-card-value sensor-coord">{{ positionData?.lat ? Number(positionData.lat).toFixed(5) : '--' }}, {{ positionData?.lng ? Number(positionData.lng).toFixed(5) : '--' }}</span>
              </div>
            </div>
          </div>
        </el-card>

        <el-card shadow="hover" class="panel-card">
          <template #header>
            <div class="config-header">
              <span>参数配置</span>
              <el-button type="primary" size="small" :loading="configLoading" :disabled="!selectedDeviceId" @click="applyConfig">应用</el-button>
            </div>
          </template>
          <el-collapse v-model="configActiveNames" class="config-collapse">
            <el-collapse-item title="避障参数 (P0)" name="p0">
              <div class="config-row">
                <span class="config-label">避障开关</span>
                <el-switch v-model="configForm.avoid_enabled" active-text="开" inactive-text="关" />
              </div>
              <div class="config-row">
                <span class="config-label">紧急停车距离</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.critical_cm" :min="5" :max="20" :step="1" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ configForm.critical_cm }} cm</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">后退避障距离</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.warn_cm" :min="10" :max="40" :step="1" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ configForm.warn_cm }} cm</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">安全距离</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.safe_cm" :min="20" :max="80" :step="1" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ configForm.safe_cm }} cm</span>
                </div>
              </div>
            </el-collapse-item>
            <el-collapse-item title="基础参数 (P1)" name="p1">
              <div class="config-row">
                <span class="config-label">默认速度</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.speed_pwm" :min="150" :max="255" :step="5" />
                  <span class="config-val">{{ configForm.speed_pwm }}</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">遥测间隔</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.telemetry_ms" :min="1000" :max="10000" :step="500" />
                  <span class="config-val">{{ (configForm.telemetry_ms / 1000).toFixed(1) }}s</span>
                </div>
              </div>
            </el-collapse-item>
            <el-collapse-item title="避障参数 (P2)" name="p2">
              <div class="config-row">
                <span class="config-label">后退超时</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.backward_timeout_ms" :min="300" :max="3000" :step="100" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ (configForm.backward_timeout_ms / 1000).toFixed(1) }}s</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">转向角度</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.turn_angle" :min="30" :max="180" :step="5" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ configForm.turn_angle }}°</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">转向超时</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.turn_timeout_ms" :min="500" :max="5000" :step="100" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ (configForm.turn_timeout_ms / 1000).toFixed(1) }}s</span>
                </div>
              </div>
              <div class="config-row">
                <span class="config-label">探路重试次数</span>
                <div class="config-slider-wrap">
                  <el-slider v-model="configForm.max_probe_retries" :min="1" :max="12" :step="1" :disabled="!configForm.avoid_enabled" />
                  <span class="config-val">{{ configForm.max_probe_retries }} 次</span>
                </div>
              </div>
            </el-collapse-item>
          </el-collapse>
        </el-card>
      </el-col>

      <el-col :xs="24" :md="16">
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
            <span v-if="cruiseActive" class="trajectory-stats" style="color:#22c55e;">
              巡航中 · {{ cruiseStateLabel }}
            </span>
          </div>
        </el-card>

        <el-card shadow="hover" class="panel-card">
          <template #header>
            <div class="map-header">
              <span>导航事件</span>
              <el-button size="small" @click="loadNavEvents">刷新</el-button>
            </div>
          </template>
          <el-timeline v-if="navEvents.length > 0" ref="navTimelineRef" class="nav-timeline">
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
import { ref, reactive, computed, onMounted, onUnmounted, nextTick } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { Top, Bottom, ArrowLeft, ArrowRight, VideoPause } from '@element-plus/icons-vue'
import AMapLoader from '@amap/amap-jsapi-loader'
import { AMAP_KEY, AMAP_VERSION, AMAP_SECURITY_KEY } from '@/config/amap'
import { vehicleAPI, deviceAPI } from '@/api'
import { formatTime } from '@/utils/format'

const cruiseActive = computed(() => {
  if (localCruiseActive.value) return true
  if (!positionData.value?.cruise_active) return false
  if (cruiseStartTime.value && Date.now() - cruiseStartTime.value < 8000) return true
  return false
})

const cruiseStateLabel = computed(() => {
  const s = positionData.value?.cruise_state
  const map = { idle: '前进巡航', avoiding: '避障中', stuck: '卡死脱困' }
  return map[s] || (cruiseActive.value ? '巡航中' : '未巡航')
})
const cruiseStateTagType = computed(() => {
  const s = positionData.value?.cruise_state
  const map = { idle: 'success', avoiding: 'warning', stuck: 'danger' }
  return map[s] || 'info'
})
const cruiseStateColor = computed(() => {
  const s = positionData.value?.cruise_state
  const map = { idle: '#22c55e', avoiding: '#f59e0b', stuck: '#ef4444' }
  return map[s] || '#8892a4'
})
const avoidStateLabel = computed(() => {
  const s = positionData.value?.avoid_state
  const map = { 0: '无避障', 1: '紧急停车', 2: '后退', 3: '转向', 4: '探路' }
  return map[s] ?? '--'
})

const deviceList = ref([])
const selectedDeviceId = ref('')
const controlMode = ref('manual')
const speedGear = ref('mid')
const SPEED_MAP = { low: 180, mid: 200, high: 255 }
const gearOptions = [
  { value: 'low', label: '低速', icon: '🐢', pwm: 180 },
  { value: 'mid', label: '中速', icon: '🚗', pwm: 200 },
  { value: 'high', label: '高速', icon: '🏎', pwm: 255 },
]
const routeLoading = ref(false)
const localCruiseActive = ref(false)
const cruiseStartTime = ref(0)
const mapLoading = ref(false)
const configActiveNames = ref(['p0'])

let map = null
let trajectoryPolyline = null
let positionMarker = null

const navEvents = ref([])
const navTimelineRef = ref(null)
const positionData = ref(null)
const configLoading = ref(false)

const configForm = reactive({
  avoid_enabled: true,
  critical_cm: 5,
  warn_cm: 10,
  safe_cm: 20,
  speed_pwm: 200,
  telemetry_ms: 2000,
  backward_timeout_ms: 300,
  turn_angle: 30,
  turn_timeout_ms: 500,
  max_probe_retries: 6,
})

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
  if (mode === 'manual' && cruiseActive.value) {
    ElMessageBox.confirm(
      '当前正在巡逻巡航中，切换到手动模式将停止巡航，是否继续？',
      '确认切换',
      { confirmButtonText: '确认', cancelButtonText: '取消', type: 'warning' }
    ).then(() => { stopCruise() }).catch(() => { controlMode.value = 'cruise' })
  }
}

async function stopCruise() {
  if (!selectedDeviceId.value) return
  localCruiseActive.value = false
  try {
    const res = await vehicleAPI.stopCruise(selectedDeviceId.value)
    ElMessage.success(res.message || '巡航已停止')
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '停止巡航失败')
  }
}

async function startCruise() {
  if (!selectedDeviceId.value) return
  try {
    await ElMessageBox.confirm(
      '确认启动巡逻巡航？小车将自主前进并避障，可通过停止按钮随时中止。',
      '确认启动巡航',
      { confirmButtonText: '确认启动', cancelButtonText: '取消', type: 'warning' }
    )
  } catch { return }
  localCruiseActive.value = true
  cruiseStartTime.value = Date.now()
  routeLoading.value = true
  try {
    const res = await vehicleAPI.startCruise(selectedDeviceId.value, SPEED_MAP[speedGear.value])
    ElMessage.success(res.message || '巡逻巡航已启动')
  } catch (err) {
    localCruiseActive.value = false
    ElMessage.error(err.response?.data?.error || '巡航启动失败')
  } finally {
    routeLoading.value = false
  }
}

async function sendCmd(cmd) {
  try {
    await vehicleAPI.sendCommand(selectedDeviceId.value, cmd, SPEED_MAP[speedGear.value])
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '指令发送失败')
  }
}

function onGearChange(gear) {
  ElMessage.success(`切换到${gear === 'low' ? '低速' : gear === 'mid' ? '中速' : '高速'}挡 (PWM ${SPEED_MAP[gear]})`)
}

async function applyConfig() {
  if (!selectedDeviceId.value) return
  if (configForm.warn_cm <= configForm.critical_cm) {
    ElMessage.warning(`后退距离 (${configForm.warn_cm}cm) 必须大于紧急停车距离 (${configForm.critical_cm}cm)`)
    return
  }
  if (configForm.safe_cm <= configForm.warn_cm) {
    ElMessage.warning(`安全距离 (${configForm.safe_cm}cm) 必须大于后退距离 (${configForm.warn_cm}cm)`)
    return
  }
  configLoading.value = true
  try {
    await vehicleAPI.sendConfig(selectedDeviceId.value, { ...configForm })
    ElMessage.success('配置已下发到设备')
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '配置下发失败')
  } finally {
    configLoading.value = false
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

async function fetchPosition() {
  if (!selectedDeviceId.value) return
  try {
    const res = await vehicleAPI.getPosition(selectedDeviceId.value)
    positionData.value = res
    if (localCruiseActive.value && res.cruise_active === false) {
      if (Date.now() - cruiseStartTime.value > 8000) {
        localCruiseActive.value = false
      }
    }
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
    nextTick(() => {
      const el = document.querySelector('.nav-timeline')
      if (el) el.scrollTop = el.scrollHeight
    })
  } catch (err) { navEvents.value = [] }
}

function onDeviceChange(deviceId) {
  navEvents.value = []
  positionData.value = null
  if (positionMarker) { positionMarker.setMap(null); positionMarker = null }
  if (trajectoryPolyline) { map.remove(trajectoryPolyline); trajectoryPolyline = null }

  if (deviceId) {
    loadNavEvents()
    fetchPosition()
    startPolling()
  } else {
    stopPolling()
  }
}

function startPolling() {
  stopPolling()
  posPollTimer = setInterval(() => { fetchPosition() }, 3000)
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

onMounted(async () => {
  await loadDevices()
  await nextTick()
  initMap()
})

onUnmounted(() => { stopPolling() })
</script>

<style scoped>
.top-bar {
  display: flex; align-items: center; justify-content: space-between;
  margin-bottom: 16px; flex-wrap: wrap; gap: 12px;
}
.page-title { font-size: 20px; font-weight: 600; color: var(--text-primary); margin: 0; }
.top-bar-right { display: flex; align-items: center; gap: 12px; }
.device-select { width: 200px; }

.panel-card { margin-bottom: 16px; }

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

.gear-section { width: 100%; }
.gear-options { display: flex; gap: 8px; justify-content: center; }
.gear-item {
  flex: 1; display: flex; flex-direction: column; align-items: center;
  padding: 10px 4px 8px; border-radius: 10px; cursor: pointer;
  border: 2px solid var(--border-color, #dcdfe6);
  transition: all 0.25s ease; user-select: none;
}
.gear-item:hover { border-color: #409eff; background: #ecf5ff; }
.gear-active {
  border-color: #409eff !important;
  background: linear-gradient(135deg, #ecf5ff 0%, #d9ecff 100%) !important;
  box-shadow: 0 2px 8px rgba(64, 158, 255, 0.25);
}
.gear-icon { font-size: 22px; line-height: 1; margin-bottom: 4px; }
.gear-name { font-size: 13px; font-weight: 600; color: var(--text-primary); margin-bottom: 2px; }
.gear-speed { font-size: 11px; color: var(--text-secondary); font-family: monospace; }

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

.sensor-grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
}
.sensor-card {
  display: flex; align-items: center; gap: 10px;
  padding: 10px 12px; border-radius: 10px;
  background: var(--bg-primary, #f4f6f8);
  border: 1px solid transparent;
  transition: all 0.2s ease;
}
.sensor-card:hover { border-color: var(--border-color, #e2e8f0); }
.sensor-card-warn {
  background: #fef0f0 !important;
  border-color: #fbc4c4 !important;
}
.sensor-card-danger {
  background: #fef0f0 !important;
  border-color: #fbc4c4 !important;
}
.sensor-card-ok {
  background: #f0f9eb !important;
  border-color: #c2e7b0 !important;
}
.sensor-card-wide {
  grid-column: span 3;
}
.sensor-card-icon {
  width: 36px; height: 36px; border-radius: 8px;
  display: flex; align-items: center; justify-content: center;
  flex-shrink: 0;
}
.sensor-card-body {
  display: flex; flex-direction: column; min-width: 0;
}
.sensor-card-label {
  font-size: 11px; color: var(--text-secondary); line-height: 1.2;
}
.sensor-card-value {
  font-size: 14px; font-weight: 700; color: var(--text-primary); line-height: 1.4;
  white-space: nowrap; overflow: hidden; text-overflow: ellipsis;
}
.sensor-card-value small {
  font-size: 10px; font-weight: 400; opacity: 0.6; margin-left: 2px;
}
.sensor-coord { font-size: 12px; font-family: monospace; }

.map-header { display: flex; justify-content: space-between; align-items: center; width: 100%; }
.map-header-actions { display: flex; align-items: center; gap: 8px; }
.map-container-large {
  width: 100%; height: 480px;
  border: 1px solid var(--border-color, #ebeef5);
  border-radius: 6px; background: var(--bg-primary, #f5f7fa);
}
.map-footer {
  margin-top: 8px; display: flex; justify-content: space-between;
  align-items: center; font-size: 13px; color: var(--text-secondary);
}
.trajectory-stats { font-size: 12px; }

.nav-timeline { max-height: 260px; overflow-y: auto; padding-right: 8px; }
.nav-event-item { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }
.evt-detail { font-size: 14px; color: var(--text-primary); }
.evt-coords { font-size: 12px; color: var(--text-secondary); }
.evt-wp { font-size: 12px; color: #409eff; font-weight: 500; }

.config-header { display: flex; justify-content: space-between; align-items: center; width: 100%; }
.config-collapse {
  border: none !important;
}
.config-collapse :deep(.el-collapse-item__header) {
  font-weight: 600; font-size: 13px; color: var(--text-secondary);
  background: transparent; border-bottom: 1px dashed var(--border-color-light, #f1f5f9);
  height: 36px; line-height: 36px;
}
.config-collapse :deep(.el-collapse-item__wrap) {
  background: transparent; border: none;
}
.config-collapse :deep(.el-collapse-item__content) {
  padding-bottom: 4px;
}
.config-row {
  display: flex; align-items: center; justify-content: space-between;
  padding: 6px 0; gap: 8px;
}
.config-label {
  flex-shrink: 0; color: var(--text-secondary); min-width: 90px; font-size: 13px;
}
.config-slider-wrap {
  flex: 1; display: flex; align-items: center; gap: 8px;
}
.config-slider-wrap .el-slider { flex: 1; }
.config-val {
  flex-shrink: 0; min-width: 52px; text-align: right;
  font-weight: 600; color: var(--text-primary); font-family: monospace; font-size: 12px;
}

@media (max-width: 768px) {
  .sensor-grid { grid-template-columns: repeat(2, 1fr); }
  .sensor-card-wide { grid-column: span 2; }
  .map-container-large { height: 320px; }
}
</style>

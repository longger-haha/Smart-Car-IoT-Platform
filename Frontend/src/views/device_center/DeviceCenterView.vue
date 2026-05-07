<template>
  <div class="device-center">
    <div class="page-header">
      <div>
        <h2>设备中心</h2>
        <span class="desc">管理所有已注册的智能小车设备</span>
      </div>
      <el-button type="primary" @click="showRegisterDialog = true">
        <el-icon><Plus /></el-icon> 注册新设备
      </el-button>
    </div>

    <el-table :data="devices" v-loading="loading" stripe border style="width: 100%" empty-text="暂无设备数据">
      <el-table-column prop="id" label="ID" width="60" align="center" />
      <el-table-column label="设备名称" min-width="140">
        <template #default="{ row }">
          <span class="device-name">{{ row.name || '未命名' }}</span>
        </template>
      </el-table-column>
      <el-table-column prop="device_id" label="设备标识 (Device ID)" min-width="200">
        <template #default="{ row }">
          <code class="device-id">{{ row.device_id }}</code>
        </template>
      </el-table-column>
      <el-table-column label="状态" width="100" align="center">
        <template #default="{ row }">
          <el-tag :type="row.status === 'online' ? 'success' : 'info'" effect="dark" size="small">
            {{ row.status === 'online' ? '在线' : '离线' }}
          </el-tag>
        </template>
      </el-table-column>
      <el-table-column label="最后心跳" width="180">
        <template #default="{ row }">
          {{ formatTime(row.last_seen_at) }}
        </template>
      </el-table-column>
      <el-table-column label="注册时间" width="180">
        <template #default="{ row }">
          {{ formatTime(row.registered_at) }}
        </template>
      </el-table-column>
      <el-table-column label="操作" width="200" fixed="right" align="center">
        <template #default="{ row }">
          <el-button type="primary" link size="small" @click="viewDetail(row)">详情</el-button>
          <el-popconfirm title="确定删除该设备?" @confirm="handleDelete(row)" confirm-button-text="确定" cancel-button-text="取消">
            <template #reference>
              <el-button type="danger" link size="small">删除</el-button>
            </template>
          </el-popconfirm>
        </template>
      </el-table-column>
    </el-table>

    <!-- 设备注册弹窗 -->
    <el-dialog
      v-model="showRegisterDialog"
      title="注册新设备"
      width="480px"
      destroy-on-close
    >
      <el-form ref="regFormRef" :model="regForm" :rules="regRules" label-width="100px" label-position="top">
        <el-form-item label="设备唯一标识 (Device ID)" prop="device_id">
          <el-input
            v-model="regForm.device_id"
            placeholder="例如: aa:bb:cc:dd:ee:ff"
            clearable
          />
        </el-form-item>
        <el-form-item label="设备别名（可选）" prop="name">
          <el-input
            v-model="regForm.name"
            placeholder="例如: 01号车"
            clearable
          />
        </el-form-item>
      </el-form>
      <div class="register-tip">
        <el-alert
          title="注册成功后将自动生成 Device_Secret，请妥善保存，仅展示一次！"
          type="warning"
          :closable="false"
          show-icon
        />
      </div>
      <template #footer>
        <el-button @click="showRegisterDialog = false">取消</el-button>
        <el-button type="primary" :loading="regLoading" @click="handleRegister">确认注册</el-button>
      </template>
    </el-dialog>

    <!-- 注册成功 - 显示密钥 -->
    <el-dialog
      v-model="showSecretDialog"
      title="注册成功"
      width="520px"
      :close-on-click-modal="false"
    >
      <el-result icon="success" title="设备注册成功" sub-title="请妥善保存以下密钥信息，关闭后不可再查看">
        <template #extra>
          <el-descriptions :column="1" border size="large">
            <el-descriptions-item label="Device ID">{{ secretInfo.device_id }}</el-descriptions-item>
            <el-descriptions-item label="Device Secret">
              <el-input :model-value="secretInfo.device_secret" readonly size="large">
                <template #append>
                  <el-button @click="copySecret">复制</el-button>
                </template>
              </el-input>
            </el-descriptions-item>
          </el-descriptions>
        </template>
      </el-result>
      <template #footer>
        <el-button type="primary" @click="showSecretDialog = false; fetchDevices()">我知道了</el-button>
      </template>
    </el-dialog>

    <!-- 设备详情抽屉 -->
    <el-drawer
      v-model="detailDrawerVisible"
      :title="'设备详情 - ' + (currentDevice?.name || currentDevice?.device_id)"
      direction="rtl"
      size="500px"
      destroy-on-close
      @opened="handleDrawerOpened"
      @closed="handleDrawerClosed"
    >
      <div v-loading="detailLoading">
        <el-descriptions :column="1" border v-if="currentDevice">
          <el-descriptions-item label="ID">{{ currentDevice.id }}</el-descriptions-item>
          <el-descriptions-item label="设备名称">{{ currentDevice.name || '未命名' }}</el-descriptions-item>
          <el-descriptions-item label="Device ID">
            <code>{{ currentDevice.device_id }}</code>
          </el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="currentDevice.status === 'online' ? 'success' : 'info'" effect="dark">
              {{ currentDevice.status === 'online' ? '在线' : '离线' }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="最后心跳">{{ formatTime(currentDevice.last_seen_at) }}</el-descriptions-item>
          <el-descriptions-item label="注册时间">{{ formatTime(currentDevice.registered_at) }}</el-descriptions-item>
        </el-descriptions>

        <el-divider content-position="left">碰撞预警状态</el-divider>
        <el-alert
          v-if="deviceDetail.collision_warning"
          title="检测到碰撞危险！超声波距离低于安全阈值"
          type="error"
          :closable="false"
          show-icon
          style="margin-bottom: 16px;"
        >
          <template #default>
            安全距离阈值: {{ deviceDetail.safe_distance_cm }} cm | 当前距离: {{ deviceDetail.latest_telemetry?.ultrasonic_cm }} cm
          </template>
        </el-alert>
        <el-alert
          v-else
          title="设备安全状态正常"
          type="success"
          :closable="false"
          show-icon
          style="margin-bottom: 16px;"
        />

        <el-divider content-position="left">最新遥测数据</el-divider>
        <el-descriptions :column="2" border v-if="deviceDetail.latest_telemetry" size="small">
          <el-descriptions-item label="纬度">{{ deviceDetail.latest_telemetry.latitude ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="经度">{{ deviceDetail.latest_telemetry.longitude ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="温度(°C)">{{ deviceDetail.latest_telemetry.temperature ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="湿度(%)">{{ deviceDetail.latest_telemetry.humidity ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="超声波(cm)">{{ deviceDetail.latest_telemetry.ultrasonic_cm ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="速度PWM">{{ deviceDetail.latest_telemetry.speed_pwm ?? '-' }}</el-descriptions-item>
          <el-descriptions-item label="记录时间" :span="2">{{ formatTime(deviceDetail.latest_telemetry.recorded_at) }}</el-descriptions-item>
        </el-descriptions>
        <el-empty v-else description="暂无遥测数据" />

        <el-divider content-position="left">遥测历史趋势</el-divider>
        <div ref="telemetryChartRef" style="width: 100%; height: 280px;"></div>
      </div>
    </el-drawer>
  </div>
</template>

<script setup>
import { ref, reactive, onMounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import { Plus } from '@element-plus/icons-vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import { deviceAPI, telemetryAPI } from '@/api'

const devices = ref([])
const loading = ref(false)
const showRegisterDialog = ref(false)
const regLoading = ref(false)
const regFormRef = ref(null)
const regForm = reactive({ device_id: '', name: '' })
const regRules = {
  device_id: [{ required: true, message: '请输入设备唯一标识', trigger: 'blur' }],
}

const showSecretDialog = ref(false)
const secretInfo = reactive({ device_id: '', device_secret: '' })

const detailDrawerVisible = ref(false)
const detailLoading = ref(false)
const currentDevice = ref(null)
const deviceDetail = reactive({
  latest_telemetry: null,
  collision_warning: false,
  safe_distance_cm: 50,
})
const telemetryChartRef = ref(null)
let telemetryChart = null
let pendingHistoryRecords = []

async function fetchDevices() {
  loading.value = true
  try {
    const res = await deviceAPI.list()
    devices.value = res.devices || []
  } catch (e) {
    console.error(e)
  } finally {
    loading.value = false
  }
}

async function handleRegister() {
  const valid = await regFormRef.value?.validate().catch(() => false)
  if (!valid) return

  regLoading.value = true
  try {
    const res = await deviceAPI.register(regForm)
    secretInfo.device_id = res.device_id
    secretInfo.device_secret = res.device_secret
    showRegisterDialog.value = false
    showSecretDialog.value = true
    regForm.device_id = ''
    regForm.name = ''
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '注册失败')
  } finally {
    regLoading.value = false
  }
}

function copySecret() {
  navigator.clipboard.writeText(secretInfo.device_secret).then(() => {
    ElMessage.success('密钥已复制到剪贴板')
  }).catch(() => {
    ElMessage.warning('复制失败，请手动选择复制')
  })
}

async function handleDelete(row) {
  try {
    await deviceAPI.delete(row.device_id)
    ElMessage.success('设备已删除')
    fetchDevices()
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '删除失败')
  }
}

async function viewDetail(row) {
  currentDevice.value = row
  detailDrawerVisible.value = true
  detailLoading.value = true

  try {
    const res = await deviceAPI.getDetail(row.device_id)
    Object.assign(deviceDetail, {
      latest_telemetry: res.latest_telemetry,
      collision_warning: res.collision_warning,
      safe_distance_cm: res.safe_distance_cm,
    })

    // 获取历史遥测用于图表
    const historyRes = await telemetryAPI.history(row.device_id, 50)
    pendingHistoryRecords = historyRes.records || []
  } catch (e) {
    console.error(e)
  } finally {
    detailLoading.value = false
  }
}

function handleDrawerOpened() {
  if (pendingHistoryRecords.length > 0) {
    renderTelemetryChart(pendingHistoryRecords)
  }
}

function handleDrawerClosed() {
  if (telemetryChart) {
    telemetryChart.dispose()
    telemetryChart = null
  }
  pendingHistoryRecords = []
}

function renderTelemetryChart(records) {
  if (!telemetryChartRef.value) return
  if (!telemetryChart) telemetryChart = echarts.init(telemetryChartRef.value)

  const times = records.slice().reverse().map(r =>
    new Date(r.recorded_at).toLocaleTimeString('zh-CN', { hour12: false })
  )
  const temps = records.slice().reverse().map(r => r.temperature)
  const humids = records.slice().reverse().map(r => r.humidity)
  const ultras = records.slice().reverse().map(r => r.ultrasonic_cm)

  telemetryChart.setOption({
    tooltip: { trigger: 'axis' },
    legend: { bottom: 0, data: ['温度(°C)', '湿度(%)', '超声波(cm)'] },
    grid: { top: 20, right: 20, bottom: 40, left: 50 },
    xAxis: { type: 'category', data: times, axisLabel: { rotate: 30, fontSize: 10 } },
    yAxis: { type: 'value' },
    series: [
      { name: '温度(°C)', type: 'line', data: temps, smooth: true, lineStyle: { color: '#f56c6c' }, itemStyle: { color: '#f56c6c' } },
      { name: '湿度(%)', type: 'line', data: humids, smooth: true, lineStyle: { color: '#409eff' }, itemStyle: { color: '#409eff' } },
      { name: '超声波(cm)', type: 'line', data: ultras, smooth: true, lineStyle: { color: '#67c23a' }, itemStyle: { color: '#67c23a' } },
    ],
  })
}

function formatTime(timeStr) {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

onMounted(() => {
  fetchDevices()
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

.device-name {
  font-weight: 600;
  color: #303133;
}

.device-id {
  background: #f5f7fa;
  padding: 2px 8px;
  border-radius: 4px;
  font-size: 13px;
  color: #606266;
}

.register-tip {
  margin-top: 8px;
}
</style>

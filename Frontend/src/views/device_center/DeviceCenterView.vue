<template>
  <div class="device-center-container">
    <el-card class="page-header">
      <template #header>
        <div class="card-header">
          <span><el-icon><Monitor /></el-icon> 设备管理中心</span>
          <div class="header-actions">
            <el-tag :type="isAdmin ? 'danger' : 'info'" effect="dark">
              {{ isAdmin ? 'Admin 管理员' : 'User 租户' }}
            </el-tag>
            <el-button
              type="primary"
              @click="showRegisterDialog"
            >
              <el-icon><Plus /></el-icon> 添加新设备
            </el-button>
          </div>
        </div>
      </template>

      <el-table :data="devices" v-loading="loading" stripe style="width: 100%">
        <el-table-column prop="id" label="ID" width="80" />
        <el-table-column prop="device_id" label="设备ID" min-width="180" />
        <el-table-column prop="name" label="设备名称" min-width="150" />
        <el-table-column prop="status" label="状态" width="120">
          <template #default="{ row }">
            <el-tag :type="row.status === 'online' ? 'success' : 'info'" effect="dark">
              {{ row.status === 'online' ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="last_seen_at" label="最后心跳" width="180">
          <template #default="{ row }">
            {{ formatTime(row.last_seen_at) }}
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200" fixed="right">
          <template #default="{ row }">
            <el-button type="primary" link size="small" @click="showDeviceDetail(row)">
              详情
            </el-button>
            <el-button
              v-if="isAdmin"
              type="danger"
              link
              size="small"
              @click="handleDeleteDevice(row)"
            >
              删除
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog
      v-model="registerDialogVisible"
      title="注册新设备"
      width="500px"
      :close-on-click-modal="false"
    >
      <el-form
        ref="registerFormRef"
        :model="registerForm"
        :rules="registerRules"
        label-width="100px"
      >
        <el-form-item label="设备ID" prop="device_id">
          <el-input
            v-model="registerForm.device_id"
            placeholder="例如: ESP32-AA:BB:CC:DD"
          />
        </el-form-item>
        <el-form-item label="设备名称" prop="name">
          <el-input
            v-model="registerForm.name"
            placeholder="例如: 01号巡逻车（可选）"
          />
        </el-form-item>
      </el-form>

      <template #footer>
        <el-button @click="registerDialogVisible = false">取消</el-button>
        <el-button type="primary" :loading="registerLoading" @click="handleRegister">
          确认注册
        </el-button>
      </template>
    </el-dialog>

    <el-dialog
      v-model="secretDialogVisible"
      title="⚠️ 设备凭证（请妥善保存）"
      width="600px"
    >
      <el-alert
        title="Device Secret 仅显示一次，请立即复制并烧录至设备！"
        type="warning"
        show-icon
        :closable="false"
        style="margin-bottom: 20px;"
      />

      <el-input
        type="textarea"
        :rows="4"
        :model-value="newDeviceSecret"
        readonly
        style="font-family: monospace; font-size: 14px;"
      />

      <div style="margin-top: 15px; text-align: right;">
        <el-button type="primary" @click="copySecret">复制到剪贴板</el-button>
      </div>
    </el-dialog>

    <el-dialog
      v-model="detailDialogVisible"
      title="📋 设备详细信息"
      width="800px"
    >
      <div v-if="deviceDetail" class="device-detail">
        <el-descriptions :column="2" border>
          <el-descriptions-item label="设备ID">{{ deviceDetail.device?.device_id }}</el-descriptions-item>
          <el-descriptions-item label="设备名称">{{ deviceDetail.device?.name || '-' }}</el-descriptions-item>
          <el-descriptions-item label="状态">
            <el-tag :type="deviceDetail.device?.status === 'online' ? 'success' : 'info'">
              {{ deviceDetail.device?.status === 'online' ? '在线' : '离线' }}
            </el-tag>
          </el-descriptions-item>
          <el-descriptions-item label="注册时间">{{ formatTime(deviceDetail.device?.registered_at) }}</el-descriptions-item>
          <el-descriptions-item label="最后心跳">{{ formatTime(deviceDetail.device?.last_seen_at) || '-' }}</el-descriptions-item>
          <el-descriptions-item label="碰撞预警">
            <el-tag :type="deviceDetail.collision_warning ? 'danger' : 'success'" effect="dark">
              {{ deviceDetail.collision_warning ? '⚠️ 危险距离 (<50cm)' : '✅ 安全' }}
            </el-tag>
          </el-descriptions-item>
        </el-descriptions>

        <h4 style="margin-top: 20px;">最新遥测数据</h4>
        <el-row :gutter="20" v-if="deviceDetail.latest_telemetry">
          <el-col :span="6">
            <el-statistic title="温度 (°C)" :value="deviceDetail.latest_telemetry.temperature" />
          </el-col>
          <el-col :span="6">
            <el-statistic title="湿度 (%)" :value="deviceDetail.latest_telemetry.humidity" />
          </el-col>
          <el-col :span="6">
            <el-statistic title="超声波 (cm)" :value="deviceDetail.latest_telemetry.ultrasonic_cm" />
          </el-col>
          <el-col :span="6">
            <el-statistic title="电机 PWM" :value="deviceDetail.latest_telemetry.speed_pwm" />
          </el-col>
        </el-row>
        <el-empty v-else description="暂无遥测数据" />

        <h4 style="margin-top: 20px;">GPS 坐标</h4>
        <p v-if="deviceDetail.latest_telemetry">
          纬度: {{ deviceDetail.latest_telemetry.latitude }} | 经度: {{ deviceDetail.latest_telemetry.longitude }}
        </p>
        <p v-else>-</p>
      </div>
    </el-dialog>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import { ElMessage, ElMessageBox } from 'element-plus'
import api from '@/api'

const devices = ref([])
const loading = ref(false)
const registerDialogVisible = ref(false)
const secretDialogVisible = ref(false)
const detailDialogVisible = ref(false)
const registerLoading = ref(false)
const registerFormRef = ref(null)
const newDeviceSecret = ref('')
const deviceDetail = ref(null)

const isAdmin = computed(() => localStorage.getItem('role') === 'admin')

const registerForm = ref({
  device_id: '',
  name: ''
})

const registerRules = {
  device_id: [
    { required: true, message: '请输入设备ID', trigger: 'blur' }
  ]
}

const fetchDevices = async () => {
  loading.value = true
  try {
    const response = await api.get('/devices')
    devices.value = response.devices || response
  } catch (error) {
    console.error('Fetch devices error:', error)
  } finally {
    loading.value = false
  }
}

const showRegisterDialog = () => {
  registerForm.value = { device_id: '', name: '' }
  registerDialogVisible.value = true
}

const handleRegister = async () => {
  if (!registerFormRef.value) return

  await registerFormRef.value.validate(async (valid) => {
    if (!valid) return

    registerLoading.value = true
    try {
      const response = await api.post('/devices', registerForm.value)
      newDeviceSecret.value = response.device_secret

      registerDialogVisible.value = false
      secretDialogVisible.value = true

      ElMessage.success('设备注册成功')
      fetchDevices()
    } catch (error) {
      console.error('Register device error:', error)
    } finally {
      registerLoading.value = false
    }
  })
}

const showDeviceDetail = async (row) => {
  try {
    const response = await api.get(`/devices/${row.device_id}`)
    deviceDetail.value = response
    detailDialogVisible.value = true
  } catch (error) {
    console.error('Fetch device detail error:', error)
    if (error.response?.status === 403) {
      ElMessage.error('无权限访问该设备（非本租户设备）')
    }
  }
}

const handleDeleteDevice = async (row) => {
  try {
    await ElMessageBox.confirm(
      `确定要删除设备 "${row.name || row.device_id}" 吗？此操作不可恢复！`,
      '删除确认',
      { confirmButtonText: '确定删除', cancelButtonText: '取消', type: 'warning' }
    )

    await api.delete(`/devices/${row.device_id}`)
    ElMessage.success('设备已删除')
    fetchDevices()
  } catch (error) {
    if (error !== 'cancel') {
      console.error('Delete device error:', error)
    }
  }
}

const copySecret = () => {
  navigator.clipboard.writeText(newDeviceSecret.value)
  ElMessage.success('已复制到剪贴板')
}

const formatTime = (timeStr) => {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

onMounted(() => {
  fetchDevices()
})
</script>

<style scoped>
.device-center-container {
  padding: 20px;
  min-height: calc(100vh - 56px);
  overflow-y: auto;
  background-color: #f5f7fa;
}

.page-header {
  margin-bottom: 20px;
}

.card-header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 18px;
  font-weight: 600;
}

.header-actions {
  display: flex;
  align-items: center;
  gap: 10px;
}

.device-detail h4 {
  font-size: 16px;
  color: #303133;
  margin-bottom: 12px;
}
</style>

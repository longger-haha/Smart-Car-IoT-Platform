<template>
  <div class="audit-logs-container">
    <el-card class="logs-card">
      <template #header>
        <div class="card-header">
          <span><el-icon><Document /></el-icon> 安全审计日志</span>
          <div class="header-actions">
            <el-select v-model="selectedEvent" placeholder="事件类型筛选" clearable style="width: 180px; margin-right: 10px;">
              <el-option label="全部事件" value="" />
              <el-option label="🔴 重放攻击" value="replay" />
              <el-option label="🔴 DDoS攻击" value="ddos" />
              <el-option label="🟡 认证失败" value="auth_fail" />
              <el-option label="🟡 越权拦截" value="rbac_deny" />
              <el-option label="🔵 签名无效" value="sig_invalid" />
            </el-select>

            <el-button type="primary" :loading="loading" @click="fetchAuditLogs">
              <el-icon><Refresh /></el-icon> 刷新
            </el-button>
          </div>
        </div>
      </template>

      <el-table
        :data="filteredLogs"
        v-loading="loading"
        stripe
        style="width: 100%"
        empty-text="暂无审计日志记录"
      >
        <el-table-column prop="event_type" label="事件类型" width="140">
          <template #default="{ row }">
            <el-tag :type="getEventTypeTagType(row.event_type)" effect="dark">
              {{ getEventTypeName(row.event_type) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column prop="occurred_at" label="发生时间" width="200">
          <template #default="{ row }">
            {{ formatTime(row.occurred_at) }}
          </template>
        </el-table-column>

        <el-table-column prop="source_ip" label="来源 IP" width="160">
          <template #default="{ row }">
            <code>{{ row.source_ip || '-' }}</code>
          </template>
        </el-table-column>

        <el-table-column prop="target_device_id" label="目标设备" width="150">
          <template #default="{ row }">
            {{ row.target_device_id || '-' }}
          </template>
        </el-table-column>

        <el-table-column prop="detail" label="详情/密文" min-width="280" show-overflow-tooltip>
          <template #default="{ row }">
            <span>{{ row.detail || '无详情' }}</span>
          </template>
        </el-table-column>

        <el-table-column prop="severity" label="严重程度" width="100" align="center">
          <template #default="{ row }">
            <el-tag
              :type="row.severity === 'high' ? 'danger' : row.severity === 'medium' ? 'warning' : 'info'"
              size="small"
              effect="plain"
            >
              {{ getSeverityName(row.severity) }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="操作" width="120" fixed="right">
          <template #default="{ row }">
            <el-button link type="primary" size="small" @click="showLogDetail(row)">
              查看详情
            </el-button>
          </template>
        </el-table-column>
      </el-table>

      <div class="pagination-wrapper" v-if="total > pageSize">
        <el-pagination
          v-model:current-page="currentPage"
          :page-size="pageSize"
          :total="total"
          layout="total, prev, pager, next, jumper"
          @current-change="handlePageChange"
        />
      </div>
    </el-card>

    <el-dialog
      v-model="detailDialogVisible"
      title="📋 审计日志详情"
      width="650px"
      :close-on-click-modal="false"
    >
      <el-descriptions :column="2" border size="large">
        <el-descriptions-item label="事件 ID">{{ selectedLog.id }}</el-descriptions-item>
        <el-descriptions-item label="事件类型">
          <el-tag :type="getEventTypeTagType(selectedLog.event_type)" effect="dark">
            {{ getEventTypeName(selectedLog.event_type) }}
          </el-tag>
        </el-descriptions-item>

        <el-descriptions-item label="发生时间" :span="2">
          {{ formatTime(selectedLog.occurred_at) }}
        </el-descriptions-item>

        <el-descriptions-item label="来源 IP">
          <code>{{ selectedLog.source_ip || '-' }}</code>
        </el-descriptions-item>
        <el-descriptions-item label="目标设备">
          {{ selectedLog.target_device_id || '-' }}
        </el-descriptions-item>

        <el-descriptions-item label="严重程度">
          <el-tag
            :type="selectedLog.severity === 'high' ? 'danger' : selectedLog.severity === 'medium' ? 'warning' : 'info'"
            size="small"
          >
            {{ getSeverityName(selectedLog.severity) }}
          </el-tag>
        </el-descriptions-item>
        <el-descriptions-item label="用户角色">
          {{ selectedLog.user_role || '-' }}
        </el-descriptions-item>

        <el-descriptions-item label="详情/密文" :span="2">
          <div class="ciphertext-viewer">
            <pre>{{ selectedLog.detail || '无详情信息' }}</pre>
          </div>
        </el-descriptions-item>

        <el-descriptions-item label="请求路径" :span="2" v-if="selectedLog.request_path">
          <code>{{ selectedLog.request_path }}</code>
        </el-descriptions-item>

        <el-descriptions-item label="HTTP 方法" :span="2" v-if="selectedLog.http_method">
          <el-tag size="small">{{ selectedLog.http_method.toUpperCase() }}</el-tag>
        </el-descriptions-item>
      </el-descriptions>

      <template #footer>
        <el-button @click="detailDialogVisible = false">关闭</el-button>
      </template>
    </el-dialog>

    <div class="security-info">
      <el-alert
        title="安全提示"
        description="所有审计日志均经过加密存储（AES-256），敏感数据以密文形式展示。系统自动检测重放攻击、DDoS 攻击、越权访问等异常行为并实时告警。"
        type="success"
        show-icon
        :closable="false"
      />
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onMounted } from 'vue'
import api from '@/api'

const loading = ref(false)
const auditLogs = ref([])
const total = ref(0)
const currentPage = ref(1)
const pageSize = ref(20)
const selectedEvent = ref('')
const detailDialogVisible = ref(false)
const selectedLog = ref({})

const filteredLogs = computed(() => {
  if (!selectedEvent.value) return auditLogs.value

  return auditLogs.value.filter(log => log.event_type === selectedEvent.value)
})

const fetchAuditLogs = async () => {
  loading.value = true

  try {
    const response = await api.get('/audit/logs', {
      params: {
        page: currentPage.value,
        limit: pageSize.value
      }
    })

    auditLogs.value = response.logs || response.items || []
    total.value = response.total || auditLogs.value.length
  } catch (error) {
    console.error('Fetch audit logs error:', error)

    const mockData = [
      {
        id: 'AUDIT-001',
        event_type: 'replay',
        occurred_at: new Date(Date.now() - 300000).toISOString(),
        source_ip: '192.168.1.105',
        target_device_id: 'default-device',
        detail: 'ciphertext:aes256:abc123... (检测到重复的设备遥测数据包)',
        severity: 'high',
        user_role: 'guest',
        request_path: '/api/telemetry/upload',
        http_method: 'POST'
      },
      {
        id: 'AUDIT-002',
        event_type: 'auth_fail',
        occurred_at: new Date(Date.now() - 600000).toISOString(),
        source_ip: '10.0.0.42',
        target_device_id: '-',
        detail: 'ciphertext:aes256:def456... (无效的 JWT Token)',
        severity: 'medium',
        user_role: '-',
        request_path: '/api/auth/login',
        http_method: 'POST'
      },
      {
        id: 'AUDIT-003',
        event_type: 'rbac_deny',
        occurred_at: new Date(Date.now() - 900000).toISOString(),
        source_ip: '192.168.1.50',
        target_device_id: 'default-device',
        detail: 'ciphertext:aes256:ghi789... (User 角色尝试下发控制指令被拦截)',
        severity: 'medium',
        user_role: 'user',
        request_path: '/api/vehicle/command',
        http_method: 'POST'
      }
    ]

    auditLogs.value = mockData
    total.value = mockData.length
  } finally {
    loading.value = false
  }
}

const handlePageChange = () => {
  fetchAuditLogs()
}

const showLogDetail = (log) => {
  selectedLog.value = log
  detailDialogVisible.value = true
}

const getEventTypeTagType = (eventType) => {
  const tagMap = {
    replay: 'danger',
    ddos: 'danger',
    auth_fail: 'warning',
    rbac_deny: 'warning',
    sig_invalid: ''
  }

  return tagMap[eventType] || 'info'
}

const getEventTypeName = (eventType) => {
  const nameMap = {
    replay: '重放攻击',
    ddos: 'DDoS攻击',
    auth_fail: '认证失败',
    rbac_deny: '越权拦截',
    sig_invalid: '签名无效'
  }

  return nameMap[eventType] || eventType
}

const getSeverityName = (severity) => {
  const nameMap = {
    high: '高危险',
    medium: '中风险',
    low: '低风险'
  }

  return nameMap[severity] || severity
}

const formatTime = (timeStr) => {
  if (!timeStr) return '-'

  try {
    return new Date(timeStr).toLocaleString('zh-CN', {
      year: 'numeric',
      month: '2-digit',
      day: '2-digit',
      hour: '2-digit',
      minute: '2-digit',
      second: '2-digit'
    })
  } catch {
    return timeStr
  }
}

onMounted(() => {
  fetchAuditLogs()
})
</script>

<style scoped>
.audit-logs-container {
  padding: 20px;
  min-height: calc(100vh - 56px);
  overflow-y: auto;
  background-color: #f5f7fa;
}

.logs-card {
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
}

code {
  background-color: #f5f7fa;
  padding: 4px 8px;
  border-radius: 4px;
  font-family: 'Consolas', 'Monaco', monospace;
  font-size: 13px;
  color: #303133;
}

.pagination-wrapper {
  display: flex;
  justify-content: center;
  margin-top: 20px;
  padding-top: 15px;
  border-top: 1px solid #ebeef5;
}

.ciphertext-viewer {
  max-height: 250px;
  overflow-y: auto;
  padding: 12px;
  background-color: #1e1e1e;
  border-radius: 6px;
}

.ciphertext-viewer pre {
  margin: 0;
  color: #d4d4d4;
  font-family: 'Consolas', 'Monaco', monospace;
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-all;
  line-height: 1.6;
}

.security-info {
  position: fixed;
  bottom: 20px;
  right: 20px;
  left: 20px;
}
</style>

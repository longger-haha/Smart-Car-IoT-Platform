<template>
  <div class="audit-logs">
    <div class="page-header">
      <div>
        <h2>安全审计日志</h2>
        <span class="desc">查看平台安全事件与攻防记录</span>
      </div>
    </div>

    <el-card shadow="never">
      <div class="filter-bar">
        <div class="filters">
          <span class="filter-label">事件类型:</span>
          <el-radio-group v-model="eventTypeFilter" size="small" @change="fetchLogs">
            <el-radio-button label="">全部</el-radio-button>
            <el-radio-button label="replay">重放攻击</el-radio-button>
            <el-radio-button label="ddos">DDoS</el-radio-button>
            <el-radio-button label="auth_fail">认证失败</el-radio-button>
            <el-radio-button label="rbac_deny">权限拒绝</el-radio-button>
            <el-radio-button label="sig_invalid">签名无效</el-radio-button>
          </el-radio-group>
        </div>
        <div class="actions">
          <el-input
            v-model="limit"
            placeholder="条数"
            style="width: 100px;"
            type="number"
            :min="10"
            :max="500"
            size="small"
            @change="fetchLogs"
          />
          <el-button type="primary" :loading="loading" icon="Refresh" size="small" @click="fetchLogs">刷新</el-button>
        </div>
      </div>

      <el-table
        :data="logs"
        v-loading="loading"
        stripe
        border
        size="default"
        empty-text="暂无审计日志记录"
        style="width: 100%"
      >
        <el-table-column prop="id" label="ID" width="70" align="center" />
        <el-table-column label="事件类型" width="130" align="center">
          <template #default="{ row }">
            <el-tag :type="eventTypeTagType(row.event_type)" effect="dark" size="small">
              {{ eventTypeLabel(row.event_type) }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="source_ip" label="来源 IP" width="150" />
        <el-table-column prop="target_device_id" label="目标设备 ID" min-width="180">
          <template #default="{ row }">
            <code v-if="row.target_device_id">{{ row.target_device_id }}</code>
            <span v-else>-</span>
          </template>
        </el-table-column>
        <el-table-column prop="detail" label="详情描述" min-width="260" show-overflow-tooltip />
        <el-table-column label="是否拦截" width="90" align="center">
          <template #default="{ row }">
            <el-tag :type="row.is_blocked ? 'danger' : 'info'" size="small" effect="plain">
              {{ row.is_blocked ? '已拦截' : '未拦截' }}
            </el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="occurred_at" label="发生时间" width="180">
          <template #default="{ row }">
            {{ formatTime(row.occurred_at) }}
          </template>
        </el-table-column>
      </el-table>

      <div class="table-footer">
        <span class="total-info">共 <strong>{{ total }}</strong> 条记录</span>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, onMounted } from 'vue'
import { auditAPI } from '@/api'

const logs = ref([])
const loading = ref(false)
const total = ref(0)
const eventTypeFilter = ref('')
const limit = ref(50)

async function fetchLogs() {
  loading.value = true
  try {
    const res = await auditAPI.logs(eventTypeFilter.value || null, Number(limit.value))
    logs.value = res.logs || []
    total.value = res.total || 0
  } catch (e) {
    console.error(e)
  } finally {
    loading.value = false
  }
}

function eventTypeTagType(type) {
  const map = { replay: 'danger', ddos: 'danger', auth_fail: 'warning', rbac_deny: 'warning', sig_invalid: 'info' }
  return map[type] || 'info'
}

function eventTypeLabel(type) {
  const map = { replay: '重放攻击', ddos: 'DDoS', auth_fail: '认证失败', rbac_deny: '权限拒绝', sig_invalid: '签名无效' }
  return map[type] || type
}

function formatTime(timeStr) {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

onMounted(() => {
  fetchLogs()
})
</script>

<style scoped>
.page-header {
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

.filter-bar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 16px;
}

.filters {
  display: flex;
  align-items: center;
  gap: 8px;
  flex-wrap: wrap;
}

.filter-label {
  font-size: 13px;
  color: #606266;
  white-space: nowrap;
}

.actions {
  display: flex;
  align-items: center;
  gap: 8px;
}

.table-footer {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding-top: 12px;
  border-top: 1px solid #ebeef5;
  margin-top: 8px;
}

.total-info {
  font-size: 13px;
  color: #909399;
}
</style>

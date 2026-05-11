export function formatTime(timeStr) {
  if (!timeStr) return '-'
  return new Date(timeStr).toLocaleString('zh-CN')
}

export function eventTypeTagType(type) {
  const map = { replay: 'danger', ddos: 'danger', auth_fail: 'warning', rbac_deny: 'warning', sig_invalid: 'info' }
  return map[type] || 'info'
}

export function eventTypeLabel(type) {
  const map = { replay: '重放攻击', ddos: 'DDoS', auth_fail: '认证失败', rbac_deny: '权限拒绝', sig_invalid: '签名无效' }
  return map[type] || type
}

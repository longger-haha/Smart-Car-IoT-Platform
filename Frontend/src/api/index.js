import axios from 'axios'
import router from '@/router'

// 创建 Axios 实例，baseURL 留空由 Vite 代理处理
const request = axios.create({
  baseURL: '/api',
  timeout: 10000,
})

// 请求拦截器：自动注入 JWT Token
request.interceptors.request.use(
  (config) => {
    const token = localStorage.getItem('access_token')
    if (token) {
      config.headers['Authorization'] = `Bearer ${token}`
    }
    return config
  },
  (error) => Promise.reject(error)
)

// 响应拦截器：统一处理 401 自动跳转登录
request.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (error.response?.status === 401) {
      localStorage.removeItem('access_token')
      localStorage.removeItem('user_role')
      router.push('/login')
    }
    return Promise.reject(error)
  }
)

// 封装各模块 API ──────────────────────────────────

// 认证模块
export const authAPI = {
  login: (username, password) =>
    request.post('/auth/login', { username, password }),
}

// 设备管理模块
export const deviceAPI = {
  list: () => request.get('/devices'),
  register: (data) => request.post('/devices', data),
}

// 遥测数据模块
export const telemetryAPI = {
  latest: (deviceId) => request.get(`/telemetry/latest/${deviceId}`),
  history: (deviceId, limit = 100) =>
    request.get(`/telemetry/history/${deviceId}`, { params: { limit } }),
}

// 车辆控制模块
export const vehicleAPI = {
  sendCommand: (deviceId, action, speedPwm = 150) =>
    request.post('/vehicle/command', {
      device_id: deviceId,
      action,
      speed_pwm: speedPwm,
    }),
}

// 安全审计模块
export const auditAPI = {
  logs: (eventType = null, limit = 50) =>
    request.get('/audit/logs', {
      params: { event_type: eventType, limit },
    }),
}

export default request

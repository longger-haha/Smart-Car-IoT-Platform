import axios from 'axios'
import { ElMessage } from 'element-plus'
import router from '@/router'

const request = axios.create({
  baseURL: '/api',
  timeout: 10000,
  headers: {
    'Content-Type': 'application/json'
  }
})

request.interceptors.request.use(
  (config) => {
    const token = localStorage.getItem('access_token')
    if (token) {
      config.headers.Authorization = `Bearer ${token}`
    }
    return config
  },
  (error) => Promise.reject(error)
)

request.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (error.response?.status === 401) {
      localStorage.removeItem('access_token')
      localStorage.removeItem('user_role')
      localStorage.removeItem('username')
      router.push('/login')
    }
    return Promise.reject(error)
  }
)

export const authAPI = {
  login: (username, password) =>
    request.post('/auth/login', { username, password }),
  register: (username, password) =>
    request.post('/auth/register', { username, password }),
}

export const deviceAPI = {
  list: () => request.get('/devices'),
  getDetail: (deviceId) => request.get(`/devices/${deviceId}`),
  register: (data) => request.post('/devices', data),
  delete: (deviceId) => request.delete(`/devices/${deviceId}`),
}

export const telemetryAPI = {
  latest: (deviceId) => request.get(`/telemetry/latest/${deviceId}`),
  history: (deviceId, limit = 100) =>
    request.get(`/telemetry/history/${deviceId}`, { params: { limit } }),
}

export const vehicleAPI = {
  sendCommand: (deviceId, command, speedPwm = 150) =>
    request.post('/vehicle/command', {
      device_id: deviceId,
      command,
      speed_pwm: speedPwm,
    }),
  dispatchRoute: (deviceId, waypoints) =>
    request.post('/vehicle/route', {
      device_id: deviceId,
      waypoints,
    }),
  getLastRoute: (deviceId) => request.get(`/vehicle/route/${deviceId}`),
}

export const auditAPI = {
  logs: (eventType = null, limit = 50) =>
    request.get('/audit/logs', {
      params: { event_type: eventType, limit },
    }),
}

export const dashboardAPI = {
  stats: () => request.get('/dashboard/stats'),
}

export default request

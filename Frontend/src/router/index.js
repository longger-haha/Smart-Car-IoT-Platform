import { createRouter, createWebHistory } from 'vue-router'
import Layout from '@/components/Layout.vue'

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/LoginView.vue'),
    meta: { requiresAuth: false }
  },
  {
    path: '/',
    component: Layout,
    redirect: '/dashboard',
    meta: { requiresAuth: true },
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/dashboard/DashboardView.vue'),
        meta: { title: '仪表盘', requiresAuth: true },
      },
      {
        path: 'devices',
        name: 'DeviceCenter',
        component: () => import('@/views/device_center/DeviceCenterView.vue'),
        meta: { title: '设备中心', requiresAuth: true },
      },
      {
        path: 'control',
        name: 'ControlPanel',
        component: () => import('@/views/control_panel/ControlPanelView.vue'),
        meta: { title: '控制面板', requiresAuth: true, requiresAdmin: true },
      },
      {
        path: 'audit',
        name: 'AuditLogs',
        component: () => import('@/views/audit_logs/AuditLogsView.vue'),
        meta: { title: '审计日志', requiresAuth: true },
      },
    ],
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, _from, next) => {
  const token = localStorage.getItem('access_token')
  if (to.meta.requiresAuth && !token) {
    next({ path: '/login', query: { redirect: to.fullPath } })
  } else if (to.path === '/login' && token) {
    next({ path: '/dashboard' })
  } else if (to.meta.requiresAdmin) {
    const role = localStorage.getItem('user_role')
    if (role !== 'admin') {
      next({ path: '/dashboard' })
    } else {
      next()
    }
  } else {
    next()
  }

  next()
})

export default router

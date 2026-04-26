import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/',
    redirect: '/dashboard',
  },
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/LoginView.vue'),
    meta: { requiresAuth: false },
  },
  {
    path: '/dashboard',
    name: 'Dashboard',
    component: () => import('@/views/dashboard/DashboardView.vue'),
    meta: { requiresAuth: true },
  },
  {
    path: '/devices',
    name: 'DeviceCenter',
    component: () => import('@/views/device_center/DeviceCenterView.vue'),
    meta: { requiresAuth: true },
  },
  {
    path: '/control',
    name: 'ControlPanel',
    component: () => import('@/views/control_panel/ControlPanelView.vue'),
    meta: { requiresAuth: true, requiresAdmin: true },
  },
  {
    path: '/audit',
    name: 'AuditLogs',
    component: () => import('@/views/audit_logs/AuditLogsView.vue'),
    meta: { requiresAuth: true },
  },
]

const router = createRouter({
  history: createWebHistory(),
  routes,
})

// 全局路由守卫：未登录跳转至 /login
router.beforeEach((to, _from, next) => {
  const token = localStorage.getItem('access_token')
  if (to.meta.requiresAuth && !token) {
    next({ path: '/login', query: { redirect: to.fullPath } })
  } else if (to.path === '/login' && token) {
    // 已登录时访问 /login 直接跳转 Dashboard
    next({ path: '/dashboard' })
  } else {
    next()
  }
})

export default router

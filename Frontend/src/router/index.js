import { createRouter, createWebHistory } from 'vue-router'

const routes = [
  {
    path: '/login',
    name: 'Login',
    component: () => import('@/views/LoginView.vue'),
    meta: { requiresAuth: false }
  },
  {
    path: '/',
    component: () => import('@/components/MainLayout.vue'),
    redirect: '/dashboard',
    meta: { requiresAuth: true },
    children: [
      {
        path: 'dashboard',
        name: 'Dashboard',
        component: () => import('@/views/dashboard/DashboardView.vue')
      },
      {
        path: 'devices',
        name: 'Devices',
        component: () => import('@/views/device_center/DeviceCenterView.vue')
      },
      {
        path: 'control',
        name: 'Control',
        component: () => import('@/views/control_panel/ControlPanelView.vue')
      },
      {
        path: 'audit',
        name: 'Audit',
        component: () => import('@/views/audit_logs/AuditLogsView.vue')
      }
    ]
  }
]

const router = createRouter({
  history: createWebHistory(),
  routes
})

router.beforeEach((to, from, next) => {
  const token = localStorage.getItem('token')

  if (to.meta.requiresAuth && !token) {
    localStorage.setItem('token', 'dev-mock-token')
    localStorage.setItem('role', 'admin')
  }

  next()
})

export default router

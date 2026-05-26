<template>
  <el-container class="layout-container">
    <el-aside :width="isCollapse ? '64px' : '240px'" class="sidebar">
      <div class="logo-area">
        <img src="@/assets/logo.svg" alt="Logo" class="logo-img" />
        <span v-show="!isCollapse" class="logo-text">SmartRover IoT</span>
      </div>
      <el-menu
        :default-active="activeMenu"
        :collapse="isCollapse"
        router
        class="sidebar-menu"
      >
        <el-menu-item index="/dashboard">
          <el-icon><DataAnalysis /></el-icon>
          <template #title>仪表盘</template>
        </el-menu-item>
        <el-menu-item index="/control">
          <el-icon><Position /></el-icon>
          <template #title>控制面板</template>
        </el-menu-item>
        <el-menu-item index="/devices">
          <el-icon><Monitor /></el-icon>
          <template #title>设备中心</template>
        </el-menu-item>
        <el-menu-item index="/audit">
          <el-icon><Document /></el-icon>
          <template #title>审计日志</template>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container>
      <el-header class="header">
        <div class="header-left">
          <el-icon
            class="collapse-btn"
            @click="isCollapse = !isCollapse"
            :size="20"
          >
            <Fold v-if="!isCollapse" />
            <Expand v-else />
          </el-icon>
          <el-breadcrumb separator="/">
            <el-breadcrumb-item :to="{ path: '/' }">首页</el-breadcrumb-item>
            <el-breadcrumb-item>{{ currentRouteName }}</el-breadcrumb-item>
          </el-breadcrumb>
        </div>
        <div class="header-right">
          <el-tag :type="isAdmin ? 'danger' : 'info'" size="small" effect="light" class="role-tag">
            {{ isAdmin ? '管理员' : '普通用户' }}
          </el-tag>
          <el-dropdown trigger="click" @command="handleCommand">
            <span class="user-info">
              <el-avatar :size="32" icon="UserFilled" style="background: var(--accent); color: white;" />
              <span class="username">{{ username }}</span>
              <el-icon><ArrowDown /></el-icon>
            </span>
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="logout">退出登录</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <el-main class="main-content">
        <router-view v-slot="{ Component }">
          <Transition name="fade" mode="out-in">
            <component :is="Component" />
          </Transition>
        </router-view>
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import {
  DataAnalysis,
  Monitor,
  Position,
  Document,
  Fold,
  Expand,
  ArrowDown,
} from '@element-plus/icons-vue'

const route = useRoute()
const router = useRouter()

const isCollapse = ref(false)
const username = ref(localStorage.getItem('username') || 'User')
const isAdmin = computed(() => localStorage.getItem('user_role') === 'admin')

const activeMenu = computed(() => route.path)
const currentRouteName = computed(() => route.meta?.title || route.name || '')

function handleCommand(command) {
  if (command === 'logout') {
    localStorage.removeItem('access_token')
    localStorage.removeItem('user_role')
    localStorage.removeItem('username')
    router.push('/login')
  }
}
</script>

<style scoped>
.layout-container {
  height: 100vh;
  overflow: hidden;
  background-color: var(--bg-primary);
}

.sidebar {
  background-color: #0f172a; /* 深色侧边栏 */
  transition: width 0.3s cubic-bezier(0.4, 0, 0.2, 1);
  overflow: hidden;
  z-index: 10;
  box-shadow: 4px 0 10px rgba(0,0,0,0.05);
}

.logo-area {
  height: 64px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  background-color: #0b1120; /* 略深的Logo区域背景 */
  padding: 0 16px;
}

.logo-img {
  width: 32px;
  height: 32px;
  filter: drop-shadow(0 2px 4px rgba(37,99,235,0.4));
}

.logo-text {
  color: #ffffff;
  font-size: 18px;
  font-weight: 700;
  white-space: nowrap;
  letter-spacing: 0.5px;
}

.sidebar-menu {
  border-right: none;
  height: calc(100vh - 64px);
  padding: 16px 12px;
  background: transparent;
}

/* Customizing Element Plus Menu for Dark Sidebar */
:deep(.el-menu-item) {
  border-radius: 8px;
  margin-bottom: 6px;
  height: 48px;
  line-height: 48px;
  color: #94a3b8; /* text-slate-400 */
  font-weight: 500;
  transition: all 0.2s;
}

:deep(.el-menu-item:hover) {
  background-color: rgba(255, 255, 255, 0.05);
  color: #f8fafc;
}

:deep(.el-menu-item.is-active) {
  background-color: var(--accent);
  color: #ffffff;
  font-weight: 600;
  box-shadow: 0 4px 6px -1px rgba(37,99,235, 0.4);
}

:deep(.el-menu-item .el-icon) {
  margin-right: 12px;
  font-size: 18px;
}

.header {
  background: #ffffff;
  border-bottom: 1px solid var(--border-color);
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 24px;
  height: 64px;
  z-index: 9;
  box-shadow: 0 1px 3px rgba(0,0,0,0.02);
}

.header-left {
  display: flex;
  align-items: center;
  gap: 20px;
}

.collapse-btn {
  cursor: pointer;
  color: var(--text-secondary);
  transition: color 0.2s;
  padding: 6px;
  border-radius: 6px;
}

.collapse-btn:hover {
  color: var(--accent);
  background-color: var(--bg-primary);
}

.header-right {
  display: flex;
  align-items: center;
  gap: 16px;
}

.role-tag {
  border-radius: 4px;
  font-weight: 500;
  border: none;
}

.user-info {
  display: flex;
  align-items: center;
  gap: 10px;
  cursor: pointer;
  color: var(--text-primary);
  padding: 4px 8px;
  border-radius: 20px;
  transition: background 0.2s;
}

.user-info:hover {
  background: var(--bg-primary);
}

.username {
  font-size: 14px;
  font-weight: 600;
}

.main-content {
  background-color: var(--bg-primary);
  overflow-y: auto;
  padding: 24px;
}
</style>

<template>
  <el-container class="main-layout">
    <el-aside :width="isCollapse ? '64px' : '220px'" class="sidebar">
      <div class="logo-area" :class="{ collapsed: isCollapse }">
        <el-icon :size="28" color="#409EFF"><Monitor /></el-icon>
        <span v-show="!isCollapse" class="logo-text">SmartRover</span>
      </div>

      <el-menu
        :default-active="activeMenu"
        :collapse="isCollapse"
        router
        background-color="#1d1e2c"
        text-color="#bfcbd9"
        active-text-color="#409EFF"
      >
        <el-menu-item index="/dashboard">
          <el-icon><DataLine /></el-icon>
          <template #title>实时监控</template>
        </el-menu-item>

        <el-menu-item index="/devices">
          <el-icon><Van /></el-icon>
          <template #title>设备中心</template>
        </el-menu-item>

        <el-menu-item index="/control">
          <el-icon><SetUp /></el-icon>
          <template #title>远程控制</template>
        </el-menu-item>

        <el-menu-item index="/audit">
          <el-icon><Document /></el-icon>
          <template #title>审计日志</template>
        </el-menu-item>
      </el-menu>
    </el-aside>

    <el-container>
      <el-header class="header" height="56px">
        <div class="header-left">
          <el-icon
            class="collapse-btn"
            :size="20"
            @click="toggleCollapse"
          >
            <Fold v-if="!isCollapse" />
            <Expand v-else />
          </el-icon>

          <el-breadcrumb separator="/">
            <el-breadcrumb-item :to="{ path: '/dashboard' }">首页</el-breadcrumb-item>
            <el-breadcrumb-item>{{ currentRouteName }}</el-breadcrumb-item>
          </el-breadcrumb>
        </div>

        <div class="header-right">
          <el-tag :type="isAdmin ? 'success' : 'warning'" size="small" effect="dark">
            {{ isAdmin ? 'Admin' : 'User' }}
          </el-tag>
          <span class="username">{{ username }}</span>
          <el-dropdown trigger="click" @command="handleCommand">
            <el-avatar :size="32" icon="UserFilled" />
            <template #dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="profile">个人信息</el-dropdown-item>
                <el-dropdown-item command="logout" divided>退出登录</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>
        </div>
      </el-header>

      <el-main class="main-content">
        <router-view />
      </el-main>
    </el-container>
  </el-container>
</template>

<script setup>
import { ref, computed } from 'vue'
import { useRoute, useRouter } from 'vue-router'

const route = useRoute()
const router = useRouter()
const isCollapse = ref(false)

const username = computed(() => localStorage.getItem('username') || 'admin')
const isAdmin = computed(() => localStorage.getItem('role') === 'admin')
const activeMenu = computed(() => route.path)

const currentRouteName = computed(() => {
  const nameMap = {
    '/dashboard': '实时监控',
    '/devices': '设备中心',
    '/control': '远程控制',
    '/audit': '审计日志'
  }
  return nameMap[route.path] || '页面'
})

const toggleCollapse = () => {
  isCollapse.value = !isCollapse.value
}

const handleCommand = (command) => {
  if (command === 'logout') {
    localStorage.removeItem('token')
    localStorage.removeItem('username')
    localStorage.removeItem('role')
    router.push('/login')
  }
}
</script>

<style scoped>
.main-layout {
  height: 100vh;
  overflow: hidden;
}

.sidebar {
  background-color: #1d1e2c;
  transition: width 0.3s;
  overflow: hidden;
  box-shadow: 2px 0 8px rgba(0, 0, 0, 0.15);
}

.logo-area {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  height: 56px;
  border-bottom: 1px solid #2a2b3d;
  padding: 0 16px;
  transition: all 0.3s;
}

.logo-area.collapsed {
  justify-content: center;
  padding: 0;
}

.logo-text {
  font-size: 18px;
  font-weight: 700;
  color: white;
  letter-spacing: 1px;
  white-space: nowrap;
}

.el-menu {
  border-right: none !important;
}

.el-menu:not(.el-menu--collapse) {
  width: 220px;
}

.header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  background: white;
  box-shadow: 0 1px 4px rgba(0, 0, 0, 0.08);
  padding: 0 20px;
  z-index: 10;
}

.header-left {
  display: flex;
  align-items: center;
  gap: 16px;
}

.collapse-btn {
  cursor: pointer;
  color: #606266;
  transition: color 0.2s;
}

.collapse-btn:hover {
  color: #409EFF;
}

.header-right {
  display: flex;
  align-items: center;
  gap: 12px;
}

.username {
  font-size: 14px;
  color: #303133;
  font-weight: 500;
}

.main-content {
  background-color: #f5f7fa;
  overflow-y: auto;
  padding: 0;
}
</style>

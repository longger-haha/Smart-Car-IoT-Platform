<template>
  <div class="login-page">
    <div class="login-wrapper">
      <!-- Left: branding panel -->
      <div class="brand-panel">
        <div class="brand-content">
          <div class="brand-icon-ring">
            <svg viewBox="0 0 64 64" fill="none" class="brand-logo">
              <rect width="64" height="64" rx="14" fill="rgba(255,255,255,0.1)" />
              <path d="M16 44V20l16 12-16 12z" fill="#60a5fa" />
              <path d="M32 44V20l16 12-16 12z" fill="#34d399" opacity="0.85" />
              <circle cx="46" cy="18" r="5" fill="#fbbf24" />
            </svg>
          </div>
          <h1 class="brand-title">SmartRover IoT</h1>
          <p class="brand-desc">智能小车物联网安全控制平台</p>
        </div>
      </div>

      <!-- Right: login form card -->
      <div class="form-panel">
        <div class="form-inner">
          <div class="form-header">
            <h2>{{ activeTab === 'login' ? '欢迎回来' : '创建账户' }}</h2>
            <p>{{ activeTab === 'login' ? '登录以管理您的物联网设备' : '注册一个新账户以开始使用' }}</p>
          </div>

          <div class="tab-switch">
            <button
              :class="['tab-btn', activeTab === 'login' && 'active']"
              @click="activeTab = 'login'"
            >登录</button>
            <button
              :class="['tab-btn', activeTab === 'register' && 'active']"
              @click="activeTab = 'register'"
            >注册</button>
            <div class="tab-indicator" :style="{ transform: activeTab === 'register' ? 'translateX(100%)' : '' }"></div>
          </div>

          <!-- Login Form -->
          <el-form
            v-show="activeTab === 'login'"
            ref="loginFormRef"
            :model="loginForm"
            :rules="loginRules"
            size="large"
            @keyup.enter="handleLogin"
            class="auth-form"
          >
            <el-form-item prop="username">
              <el-input
                v-model="loginForm.username"
                placeholder="用户名"
                prefix-icon="User"
                clearable
              />
            </el-form-item>
            <el-form-item prop="password">
              <el-input
                v-model="loginForm.password"
                type="password"
                placeholder="密码"
                prefix-icon="Lock"
                show-password
              />
            </el-form-item>
            <el-form-item>
              <el-button
                type="primary"
                :loading="loginLoading"
                style="width: 100%"
                class="submit-btn"
                @click="handleLogin"
              >
                登 录
              </el-button>
            </el-form-item>
          </el-form>

          <!-- Register Form -->
          <el-form
            v-show="activeTab === 'register'"
            ref="registerFormRef"
            :model="registerForm"
            :rules="registerRules"
            size="large"
            @keyup.enter="handleRegister"
            class="auth-form"
          >
            <el-form-item prop="username">
              <el-input
                v-model="registerForm.username"
                placeholder="用户名"
                prefix-icon="User"
                clearable
              />
            </el-form-item>
            <el-form-item prop="password">
              <el-input
                v-model="registerForm.password"
                type="password"
                placeholder="密码（至少6位）"
                prefix-icon="Lock"
                show-password
              />
            </el-form-item>
            <el-form-item prop="confirmPassword">
              <el-input
                v-model="registerForm.confirmPassword"
                type="password"
                placeholder="确认密码"
                prefix-icon="Lock"
                show-password
              />
            </el-form-item>
            <el-form-item>
              <el-button
                type="primary"
                :loading="registerLoading"
                style="width: 100%"
                class="submit-btn"
                @click="handleRegister"
              >
                注 册
              </el-button>
            </el-form-item>
          </el-form>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, reactive } from 'vue'
import { useRouter, useRoute } from 'vue-router'
import { ElMessage } from 'element-plus'
import { authAPI } from '@/api'

const router = useRouter()
const route = useRoute()

const activeTab = ref('login')
const loginLoading = ref(false)
const registerLoading = ref(false)
const loginFormRef = ref(null)
const registerFormRef = ref(null)

const loginForm = reactive({
  username: '',
  password: '',
})

const registerForm = reactive({
  username: '',
  password: '',
  confirmPassword: '',
})

const validateConfirmPass = (rule, value, callback) => {
  if (value !== registerForm.password) {
    callback(new Error('两次输入的密码不一致'))
  } else {
    callback()
  }
}

const loginRules = {
  username: [{ required: true, message: '请输入用户名', trigger: 'blur' }],
  password: [{ required: true, message: '请输入密码', trigger: 'blur' }],
}

const registerRules = {
  username: [
    { required: true, message: '请输入用户名', trigger: 'blur' },
    { min: 2, max: 20, message: '用户名长度为 2-20 个字符', trigger: 'blur' },
  ],
  password: [
    { required: true, message: '请输入密码', trigger: 'blur' },
    { min: 6, message: '密码至少6位', trigger: 'blur' },
  ],
  confirmPassword: [
    { required: true, message: '请确认密码', trigger: 'blur' },
    { validator: validateConfirmPass, trigger: 'blur' },
  ],
}

async function handleLogin() {
  const valid = await loginFormRef.value?.validate().catch(() => false)
  if (!valid) return

  loginLoading.value = true
  try {
    const res = await authAPI.login(loginForm.username, loginForm.password)
    localStorage.setItem('access_token', res.access_token)
    localStorage.setItem('user_role', res.role)
    localStorage.setItem('username', res.username)
    ElMessage.success(`欢迎回来, ${res.username}`)
    const redirect = route.query.redirect || '/dashboard'
    router.push(redirect)
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '登录失败')
  } finally {
    loginLoading.value = false
  }
}

async function handleRegister() {
  const valid = await registerFormRef.value?.validate().catch(() => false)
  if (!valid) return

  registerLoading.value = true
  try {
    const res = await authAPI.register(registerForm.username, registerForm.password)
    ElMessage.success(`注册成功! 请登录`)
    activeTab.value = 'login'
    loginForm.username = registerForm.username
    loginForm.password = ''
    registerForm.username = ''
    registerForm.password = ''
    registerForm.confirmPassword = ''
  } catch (err) {
    ElMessage.error(err.response?.data?.error || '注册失败')
  } finally {
    registerLoading.value = false
  }
}
</script>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: var(--bg-primary);
  position: relative;
}

.login-wrapper {
  display: flex;
  width: 880px;
  max-width: 96vw;
  min-height: 520px;
  border: 1px solid var(--border-color);
  border-radius: 6px;
  overflow: hidden;
}

.brand-panel {
  flex: 1;
  background: var(--bg-secondary);
  padding: 48px 36px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  border-right: 1px solid var(--border-color);
}

.brand-icon-ring {
  width: 56px; height: 56px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 20px;
  background: var(--bg-sidebar);
}

.brand-logo {
  width: 40px; height: 40px;
}

.brand-title {
  font-size: 26px;
  font-weight: 700;
  color: var(--text-primary);
  margin: 0 0 6px;
}

.brand-desc {
  color: var(--text-secondary);
  font-size: 14px;
  margin: 0;
}

.form-panel {
  flex: 1;
  background: var(--bg-secondary);
  padding: 48px 40px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.form-inner {
  width: 100%;
  max-width: 340px;
}

.form-header {
  margin-bottom: 24px;
}

.form-header h2 {
  color: var(--text-primary);
  font-size: 22px;
  font-weight: 600;
  margin: 0 0 6px;
}

.form-header p {
  color: var(--text-secondary);
  font-size: 13px;
  margin: 0;
}

.tab-switch {
  display: flex;
  position: relative;
  background: rgba(255, 255, 255, 0.04);
  border-radius: 6px;
  padding: 3px;
  margin-bottom: 24px;
  border: 1px solid var(--border-color);
}

.tab-btn {
  flex: 1;
  padding: 8px 0;
  font-size: 14px;
  font-weight: 500;
  border: none;
  background: none;
  color: var(--text-secondary);
  cursor: pointer;
  position: relative;
  z-index: 1;
  transition: color 0.2s;
  border-radius: 4px;
}

.tab-btn.active {
  color: var(--text-primary);
}

.tab-indicator {
  position: absolute;
  top: 3px; left: 3px;
  width: calc(50% - 3px);
  height: calc(100% - 6px);
  background: rgba(59, 130, 246, 0.15);
  border-radius: 4px;
  transition: transform 0.2s;
}

.auth-form :deep(.el-input__wrapper) {
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid var(--border-color);
  border-radius: 6px;
  box-shadow: none;
  padding: 4px 12px;
}

.auth-form :deep(.el-input__wrapper:hover) {
  border-color: rgba(59, 130, 246, 0.3);
}

.auth-form :deep(.el-input__wrapper.is-focus) {
  border-color: var(--accent);
}

.auth-form :deep(.el-input__inner) {
  color: var(--text-primary);
  font-size: 14px;
}

.auth-form :deep(.el-input__inner::placeholder) {
  color: var(--text-secondary);
}

.auth-form :deep(.el-input__prefix .el-icon) {
  color: var(--text-secondary);
}

.submit-btn {
  height: 44px;
  font-size: 15px;
  font-weight: 600;
  border-radius: 6px;
  border: none;
  background: var(--accent) !important;
  letter-spacing: 2px;
}

.submit-btn:hover {
  opacity: 0.9;
}

@media (max-width: 768px) {
  .login-wrapper {
    flex-direction: column;
    min-height: auto;
  }
  .brand-panel {
    padding: 32px 28px;
    border-right: none;
    border-bottom: 1px solid var(--border-color);
  }
  .form-panel {
    padding: 32px 28px;
  }
}
</style>

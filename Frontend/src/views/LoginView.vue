<template>
  <div class="login-page">
    <!-- Animated Background Shapes -->
    <div class="bg-shape bg-shape-1"></div>
    <div class="bg-shape bg-shape-2"></div>
    <div class="bg-shape bg-shape-3"></div>

    <div class="login-wrapper">
      <!-- Left: branding panel -->
      <div class="brand-panel">
        <div class="brand-content">
          <div class="brand-icon-ring">
            <svg viewBox="0 0 64 64" fill="none" class="brand-logo">
              <rect width="64" height="64" rx="16" fill="url(#logo-grad)" />
              <defs>
                <linearGradient id="logo-grad" x1="0" y1="0" x2="64" y2="64">
                  <stop offset="0%" stop-color="#3b82f6" />
                  <stop offset="100%" stop-color="#2563eb" />
                </linearGradient>
              </defs>
              <path d="M16 44V20l16 12-16 12z" fill="#ffffff" />
              <path d="M32 44V20l16 12-16 12z" fill="#93c5fd" opacity="0.9" />
              <circle cx="46" cy="18" r="5" fill="#fcd34d" />
            </svg>
          </div>
          <h1 class="brand-title">SmartRover IoT</h1>
          <p class="brand-desc">智能化物联网安全控制与审计平台</p>
          
          <div class="feature-list">
            <div class="feature-item">
              <el-icon><Monitor /></el-icon>
              <span>实时车辆监控与三维可视化</span>
            </div>
            <div class="feature-item">
              <el-icon><Lock /></el-icon>
              <span>毫秒级安全审计与威胁拦截</span>
            </div>
            <div class="feature-item">
              <el-icon><Cpu /></el-icon>
              <span>云端自动驾驶航点规划</span>
            </div>
          </div>
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
import { Monitor, Lock, Cpu, User } from '@element-plus/icons-vue'
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
  background: #f8fafc;
  position: relative;
  overflow: hidden;
}

/* Beautiful animated background orbs */
.bg-shape {
  position: absolute;
  border-radius: 50%;
  filter: blur(80px);
  opacity: 0.6;
  animation: float 20s infinite ease-in-out;
  z-index: 0;
}

.bg-shape-1 {
  width: 600px;
  height: 600px;
  background: rgba(59, 130, 246, 0.4);
  top: -100px;
  left: -100px;
  animation-delay: 0s;
}

.bg-shape-2 {
  width: 500px;
  height: 500px;
  background: rgba(16, 185, 129, 0.3);
  bottom: -50px;
  right: -50px;
  animation-delay: -5s;
}

.bg-shape-3 {
  width: 400px;
  height: 400px;
  background: rgba(139, 92, 246, 0.3);
  top: 40%;
  left: 30%;
  animation-delay: -10s;
}

@keyframes float {
  0%, 100% { transform: translate(0, 0) scale(1); }
  33% { transform: translate(30px, -50px) scale(1.1); }
  66% { transform: translate(-20px, 20px) scale(0.9); }
}

.login-wrapper {
  display: flex;
  width: 1000px;
  max-width: 96vw;
  min-height: 580px;
  background: rgba(255, 255, 255, 0.75);
  backdrop-filter: blur(24px);
  -webkit-backdrop-filter: blur(24px);
  border: 1px solid rgba(255, 255, 255, 0.6);
  border-radius: 24px;
  box-shadow: 0 20px 40px rgba(0, 0, 0, 0.08), 0 1px 3px rgba(0, 0, 0, 0.05);
  overflow: hidden;
  position: relative;
  z-index: 10;
}

.brand-panel {
  flex: 1.1;
  padding: 60px 50px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  position: relative;
  background: linear-gradient(135deg, rgba(255,255,255,0.4) 0%, rgba(255,255,255,0.1) 100%);
  border-right: 1px solid rgba(255, 255, 255, 0.5);
}

.brand-icon-ring {
  width: 72px; height: 72px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 32px;
  border-radius: 20px;
  background: #ffffff;
  box-shadow: 0 10px 25px rgba(37, 99, 235, 0.15);
}

.brand-logo {
  width: 48px; height: 48px;
}

.brand-title {
  font-size: 32px;
  font-weight: 800;
  color: #0f172a;
  margin: 0 0 12px;
  letter-spacing: -0.8px;
}

.brand-desc {
  color: #64748b;
  font-size: 16px;
  margin: 0 0 40px;
  line-height: 1.6;
}

.feature-list {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.feature-item {
  display: flex;
  align-items: center;
  gap: 12px;
  color: #475569;
  font-size: 15px;
  font-weight: 500;
}

.feature-item .el-icon {
  font-size: 20px;
  color: var(--accent);
  background: rgba(37, 99, 235, 0.1);
  padding: 8px;
  border-radius: 10px;
}

.form-panel {
  flex: 0.9;
  background: rgba(255, 255, 255, 0.4);
  padding: 60px 50px;
  display: flex;
  align-items: center;
  justify-content: center;
}

.form-inner {
  width: 100%;
  max-width: 340px;
}

.form-header {
  margin-bottom: 32px;
  text-align: center;
}

.form-header h2 {
  color: #0f172a;
  font-size: 26px;
  font-weight: 800;
  margin: 0 0 8px;
  letter-spacing: -0.5px;
}

.form-header p {
  color: #64748b;
  font-size: 14px;
  margin: 0;
}

.tab-switch {
  display: flex;
  position: relative;
  background: rgba(241, 245, 249, 0.8);
  border-radius: 10px;
  padding: 4px;
  margin-bottom: 32px;
  border: 1px solid rgba(255, 255, 255, 0.5);
  box-shadow: inset 0 2px 4px rgba(0,0,0,0.02);
}

.tab-btn {
  flex: 1;
  padding: 10px 0;
  font-size: 14px;
  font-weight: 600;
  border: none;
  background: none;
  color: #64748b;
  cursor: pointer;
  position: relative;
  z-index: 1;
  transition: all 0.3s ease;
  border-radius: 8px;
}

.tab-btn.active {
  color: var(--accent);
}

.tab-indicator {
  position: absolute;
  top: 4px; left: 4px;
  width: calc(50% - 4px);
  height: calc(100% - 8px);
  background: #ffffff;
  border-radius: 8px;
  box-shadow: 0 2px 8px rgba(0,0,0,0.06);
  transition: transform 0.3s cubic-bezier(0.4, 0, 0.2, 1);
}

.auth-form :deep(.el-input__wrapper) {
  background: rgba(255, 255, 255, 0.8);
  border: 1px solid rgba(226, 232, 240, 0.8);
  border-radius: 10px;
  box-shadow: 0 2px 4px rgba(0,0,0,0.01);
  padding: 10px 14px;
  transition: all 0.2s;
}

.auth-form :deep(.el-input__wrapper:hover) {
  border-color: var(--accent);
  background: #ffffff;
}

.auth-form :deep(.el-input__wrapper.is-focus) {
  border-color: var(--accent);
  background: #ffffff;
  box-shadow: 0 0 0 3px rgba(37, 99, 235, 0.15) !important;
}

.auth-form :deep(.el-input__inner) {
  color: #0f172a;
  font-size: 15px;
  font-weight: 500;
}

.auth-form :deep(.el-input__inner::placeholder) {
  color: #94a3b8;
  font-weight: 400;
}

.auth-form :deep(.el-input__prefix .el-icon) {
  color: #64748b;
  font-size: 18px;
}

.submit-btn {
  height: 50px;
  font-size: 16px;
  font-weight: 600;
  border-radius: 10px;
  border: none;
  background: var(--accent) !important;
  color: #fff;
  letter-spacing: 1px;
  margin-top: 12px;
  box-shadow: 0 8px 16px rgba(37, 99, 235, 0.25);
  transition: all 0.3s ease;
}

.submit-btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 12px 20px rgba(37, 99, 235, 0.3);
  background: #1d4ed8 !important;
}

@media (max-width: 768px) {
  .login-wrapper {
    flex-direction: column;
    min-height: auto;
    width: 100%;
    margin: 20px;
    border-radius: 20px;
  }
  .brand-panel {
    padding: 40px 30px;
    border-right: none;
    border-bottom: 1px solid rgba(255, 255, 255, 0.5);
  }
  .form-panel {
    padding: 40px 30px;
  }
}
</style>

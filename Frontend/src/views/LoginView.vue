<template>
  <div class="login-page">
    <!-- Animated background -->
    <div class="bg-grid"></div>
    <div class="bg-glow bg-glow-1"></div>
    <div class="bg-glow bg-glow-2"></div>
    <div class="bg-glow bg-glow-3"></div>

    <!-- Floating particles -->
    <div class="particle" v-for="i in 20" :key="i" :style="particleStyle(i)"></div>

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

function particleStyle(i) {
  const size = 2 + Math.random() * 4
  return {
    width: size + 'px',
    height: size + 'px',
    left: Math.random() * 100 + '%',
    top: Math.random() * 100 + '%',
    animationDelay: Math.random() * 6 + 's',
    animationDuration: 4 + Math.random() * 8 + 's',
  }
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
@import url('https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700;800&display=swap');

.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: #0a0e1a;
  font-family: 'Inter', -apple-system, BlinkMacSystemFont, sans-serif;
  position: relative;
  overflow: hidden;
}

/* ── Animated grid background ── */
.bg-grid {
  position: absolute;
  inset: 0;
  background-image:
    linear-gradient(rgba(56, 189, 248, 0.03) 1px, transparent 1px),
    linear-gradient(90deg, rgba(56, 189, 248, 0.03) 1px, transparent 1px);
  background-size: 48px 48px;
  animation: gridPulse 8s ease-in-out infinite;
}

@keyframes gridPulse {
  0%, 100% { opacity: 0.4; }
  50% { opacity: 0.8; }
}

/* ── Glow orbs ── */
.bg-glow {
  position: absolute;
  border-radius: 50%;
  filter: blur(100px);
  pointer-events: none;
}
.bg-glow-1 {
  width: 500px; height: 500px;
  background: radial-gradient(circle, rgba(96, 165, 250, 0.15), transparent);
  top: -100px; left: -100px;
  animation: glowFloat1 12s ease-in-out infinite;
}
.bg-glow-2 {
  width: 400px; height: 400px;
  background: radial-gradient(circle, rgba(52, 211, 153, 0.12), transparent);
  bottom: -80px; right: -80px;
  animation: glowFloat2 10s ease-in-out infinite;
}
.bg-glow-3 {
  width: 300px; height: 300px;
  background: radial-gradient(circle, rgba(167, 139, 250, 0.1), transparent);
  top: 50%; left: 50%;
  transform: translate(-50%, -50%);
  animation: glowFloat3 14s ease-in-out infinite;
}

@keyframes glowFloat1 {
  0%, 100% { transform: translate(0, 0); }
  50% { transform: translate(40px, 60px); }
}
@keyframes glowFloat2 {
  0%, 100% { transform: translate(0, 0); }
  50% { transform: translate(-50px, -40px); }
}
@keyframes glowFloat3 {
  0%, 100% { transform: translate(-50%, -50%) scale(1); }
  50% { transform: translate(-50%, -50%) scale(1.3); }
}

/* ── Floating particles ── */
.particle {
  position: absolute;
  border-radius: 50%;
  background: rgba(96, 165, 250, 0.5);
  pointer-events: none;
  animation: particleDrift linear infinite;
}

@keyframes particleDrift {
  0% { transform: translateY(0) scale(1); opacity: 0; }
  10% { opacity: 1; }
  90% { opacity: 1; }
  100% { transform: translateY(-120px) scale(0.3); opacity: 0; }
}

/* ── Main wrapper ── */
.login-wrapper {
  display: flex;
  width: 900px;
  max-width: 96vw;
  min-height: 560px;
  border-radius: 20px;
  overflow: hidden;
  box-shadow:
    0 0 0 1px rgba(255, 255, 255, 0.06),
    0 40px 80px -20px rgba(0, 0, 0, 0.6),
    0 0 80px rgba(96, 165, 250, 0.08);
  position: relative;
  z-index: 1;
}

/* ── Brand panel (left) ── */
.brand-panel {
  flex: 1;
  background: linear-gradient(160deg, #0f172a 0%, #1e293b 50%, #0f172a 100%);
  padding: 48px 36px;
  display: flex;
  flex-direction: column;
  justify-content: center;
  position: relative;
  overflow: hidden;
  border-right: 1px solid rgba(255, 255, 255, 0.06);
}

.brand-panel::before {
  content: '';
  position: absolute;
  top: 0; left: 0; right: 0; bottom: 0;
  background:
    radial-gradient(ellipse at 20% 20%, rgba(96, 165, 250, 0.08), transparent 60%),
    radial-gradient(ellipse at 80% 80%, rgba(52, 211, 153, 0.06), transparent 60%);
  pointer-events: none;
}

.brand-content {
  position: relative;
  z-index: 1;
}

.brand-icon-ring {
  width: 72px; height: 72px;
  border-radius: 18px;
  display: flex;
  align-items: center;
  justify-content: center;
  margin-bottom: 24px;
  background: linear-gradient(135deg, rgba(96, 165, 250, 0.15), rgba(52, 211, 153, 0.1));
  border: 1px solid rgba(96, 165, 250, 0.2);
  box-shadow: 0 0 30px rgba(96, 165, 250, 0.1);
}

.brand-logo {
  width: 48px; height: 48px;
}

.brand-title {
  font-size: 28px;
  font-weight: 800;
  background: linear-gradient(135deg, #60a5fa, #34d399);
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  background-clip: text;
  margin: 0 0 8px;
  letter-spacing: -0.5px;
}

.brand-desc {
  color: rgba(148, 163, 184, 0.9);
  font-size: 14px;
  margin: 0;
  font-weight: 400;
}

/* ── Form panel (right) ── */
.form-panel {
  flex: 1;
  background: linear-gradient(180deg, #111827 0%, #0f172a 100%);
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
  margin-bottom: 28px;
}

.form-header h2 {
  color: #f1f5f9;
  font-size: 24px;
  font-weight: 700;
  margin: 0 0 6px;
  letter-spacing: -0.3px;
}

.form-header p {
  color: rgba(148, 163, 184, 0.8);
  font-size: 13px;
  margin: 0;
}

/* ── Tab switch ── */
.tab-switch {
  display: flex;
  position: relative;
  background: rgba(255, 255, 255, 0.04);
  border-radius: 10px;
  padding: 3px;
  margin-bottom: 28px;
  border: 1px solid rgba(255, 255, 255, 0.06);
}

.tab-btn {
  flex: 1;
  padding: 10px 0;
  font-size: 14px;
  font-weight: 600;
  border: none;
  background: none;
  color: rgba(148, 163, 184, 0.7);
  cursor: pointer;
  position: relative;
  z-index: 1;
  transition: color 0.3s;
  border-radius: 8px;
  font-family: inherit;
}

.tab-btn.active {
  color: #f1f5f9;
}

.tab-indicator {
  position: absolute;
  top: 3px; left: 3px;
  width: calc(50% - 3px);
  height: calc(100% - 6px);
  background: linear-gradient(135deg, rgba(96, 165, 250, 0.2), rgba(52, 211, 153, 0.15));
  border-radius: 8px;
  border: 1px solid rgba(96, 165, 250, 0.25);
  transition: transform 0.35s cubic-bezier(0.4, 0, 0.2, 1);
}

/* ── Form styles ── */
.auth-form {
  animation: formFadeIn 0.3s ease;
}

@keyframes formFadeIn {
  from { opacity: 0; transform: translateY(8px); }
  to { opacity: 1; transform: translateY(0); }
}

.auth-form :deep(.el-input__wrapper) {
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  box-shadow: none;
  padding: 4px 12px;
  transition: all 0.3s;
}

.auth-form :deep(.el-input__wrapper:hover) {
  border-color: rgba(96, 165, 250, 0.3);
}

.auth-form :deep(.el-input__wrapper.is-focus) {
  border-color: rgba(96, 165, 250, 0.5);
  box-shadow: 0 0 0 3px rgba(96, 165, 250, 0.08);
}

.auth-form :deep(.el-input__inner) {
  color: #e2e8f0;
  font-size: 14px;
}

.auth-form :deep(.el-input__inner::placeholder) {
  color: rgba(148, 163, 184, 0.5);
}

.auth-form :deep(.el-input__prefix .el-icon) {
  color: rgba(148, 163, 184, 0.5);
}

.auth-form :deep(.el-form-item__error) {
  font-size: 12px;
}

/* ── Submit button ── */
.submit-btn {
  height: 46px;
  font-size: 15px;
  font-weight: 600;
  border-radius: 10px;
  border: none;
  background: linear-gradient(135deg, #3b82f6, #2563eb) !important;
  letter-spacing: 4px;
  transition: all 0.3s;
  box-shadow: 0 4px 16px rgba(59, 130, 246, 0.3);
}

.submit-btn:hover {
  background: linear-gradient(135deg, #60a5fa, #3b82f6) !important;
  box-shadow: 0 6px 24px rgba(59, 130, 246, 0.4);
  transform: translateY(-1px);
}

.submit-btn:active {
  transform: translateY(0);
}

/* ── Footer ── */
.form-footer {
  text-align: center;
  margin-top: 24px;
  padding-top: 20px;
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}

.form-footer span {
  color: rgba(100, 116, 139, 0.6);
  font-size: 12px;
}

/* ── Responsive ── */
@media (max-width: 768px) {
  .login-wrapper {
    flex-direction: column;
    min-height: auto;
    max-height: 96vh;
    overflow-y: auto;
  }
  .brand-panel {
    padding: 32px 28px;
    border-right: none;
    border-bottom: 1px solid rgba(255, 255, 255, 0.06);
  }
  .brand-features {
    display: none;
  }
  .form-panel {
    padding: 32px 28px;
  }
}
</style>

<template>
  <div class="sensor-charts">
    <el-card class="chart-card">
      <template #header>
        <div class="card-title">
          <el-icon><TrendCharts /></el-icon>
          <span>传感器实时数据</span>
        </div>
      </template>

      <div v-if="showAlert" class="alert-banner">
        <el-alert
          :title="alertMessage"
          type="error"
          show-icon
          effect="dark"
          :closable="false"
        />
      </div>

      <div class="charts-row">
        <div class="chart-col" ref="tempChartRef"></div>
        <div class="chart-col" ref="humidityChartRef"></div>
        <div class="chart-col" ref="ultrasonicChartRef"></div>
      </div>
    </el-card>
  </div>
</template>

<script setup>
import { ref, watch, onMounted, onUnmounted, nextTick } from 'vue'
import * as echarts from 'echarts'
import { TrendCharts } from '@element-plus/icons-vue'

const props = defineProps({
  telemetryData: {
    type: Object,
    default: () => ({})
  }
})

const tempChartRef = ref(null)
const humidityChartRef = ref(null)
const ultrasonicChartRef = ref(null)

let tempChart = null
let humidityChart = null
let ultrasonicChart = null

const showAlert = ref(false)
const alertMessage = ref('')

const temperatureHistory = ref([])
const humidityHistory = ref([])
const ultrasonicHistory = ref([])
const timeLabels = ref([])

const MAX_DATA_POINTS = 30

const initCharts = () => {
  if (tempChartRef.value) {
    tempChart = echarts.init(tempChartRef.value)
  }

  if (humidityChartRef.value) {
    humidityChart = echarts.init(humidityChartRef.value)
  }

  if (ultrasonicChartRef.value) {
    ultrasonicChart = echarts.init(ultrasonicChartRef.value)
  }

  updateCharts()
}

const updateCharts = () => {
  const now = new Date().toLocaleTimeString('zh-CN')

  timeLabels.value.push(now)
  if (timeLabels.value.length > MAX_DATA_POINTS) {
    timeLabels.value.shift()
  }

  if (props.telemetryData.temperature !== undefined) {
    temperatureHistory.value.push(props.telemetryData.temperature)
    if (temperatureHistory.value.length > MAX_DATA_POINTS) {
      temperatureHistory.value.shift()
    }
  }

  if (props.telemetryData.humidity !== undefined) {
    humidityHistory.value.push(props.telemetryData.humidity)
    if (humidityHistory.value.length > MAX_DATA_POINTS) {
      humidityHistory.value.shift()
    }
  }

  if (props.telemetryData.ultrasonic_cm !== undefined) {
    ultrasonicHistory.value.push(props.telemetryData.ultrasonic_cm)

    if (props.telemetryData.ultrasonic_cm < 50) {
        showAlert.value = true
        alertMessage.value = `⚠️ 碰撞预警！超声波距离过低: ${props.telemetryData.ultrasonic_cm}cm（安全阈值: 50cm，后端已自动下发紧急停车指令）`
      } else {
      showAlert.value = false
    }

    if (ultrasonicHistory.value.length > MAX_DATA_POINTS) {
      ultrasonicHistory.value.shift()
    }
  }

  const sharedGrid = { left: '12%', right: '5%', bottom: '22%', top: '24%' }
  const sharedXAxis = {
    type: 'category',
    data: timeLabels.value,
    axisLabel: { fontSize: 9, rotate: 30 }
  }

  const tempOption = {
    title: {
      text: '温度 (°C)',
      left: 'center',
      textStyle: { fontSize: 13, fontWeight: 600 }
    },
    tooltip: { trigger: 'axis' },
    xAxis: sharedXAxis,
    yAxis: {
      type: 'value',
      min: function(value) {
        return Math.floor(value.min - 5)
      }
    },
    series: [{
      data: temperatureHistory.value,
      type: 'line',
      smooth: true,
      lineStyle: { color: '#F56C6C', width: 2 },
      itemStyle: { color: '#F56C6C' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(245, 108, 108, 0.3)' },
          { offset: 1, color: 'rgba(245, 108, 108, 0.05)' }
        ])
      }
    }],
    grid: sharedGrid
  }

  const humidityOption = {
    title: {
      text: '湿度 (%)',
      left: 'center',
      textStyle: { fontSize: 13, fontWeight: 600 }
    },
    tooltip: { trigger: 'axis' },
    xAxis: sharedXAxis,
    yAxis: {
      type: 'value',
      min: 0,
      max: 100
    },
    series: [{
      data: humidityHistory.value,
      type: 'line',
      smooth: true,
      lineStyle: { color: '#409EFF', width: 2 },
      itemStyle: { color: '#409EFF' },
      areaStyle: {
        color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
          { offset: 0, color: 'rgba(64, 158, 255, 0.3)' },
          { offset: 1, color: 'rgba(64, 158, 255, 0.05)' }
        ])
      }
    }],
    grid: sharedGrid
  }

  const ultrasonicOption = {
    title: {
      text: '超声波 (cm)',
      left: 'center',
      textStyle: { fontSize: 13, fontWeight: 600 }
    },
    tooltip: { trigger: 'axis' },
    xAxis: sharedXAxis,
    yAxis: {
      type: 'value'
    },
    visualMap: {
      top: 0,
      right: 0,
      pieces: [
        { lte: 5, color: '#F56C6C' },
        { gt: 5, lte: 50, color: '#67C23A' },
        { gt: 50, color: '#E6A23C' }
      ],
      outOfRange: { color: '#999' }
    },
    series: [{
      data: ultrasonicHistory.value,
      type: 'line',
      smooth: true,
      lineStyle: { width: 2 },
      markLine: {
        data: [{ yAxis: 50, name: '碰撞预警阈值', label: { formatter: '安全距离 50cm' } }],
        lineStyle: { color: '#F56C6C', type: 'dashed' }
      }
    }],
    grid: { left: '12%', right: '10%', bottom: '22%', top: '24%' }
  }

  if (tempChart) tempChart.setOption(tempOption, true)
  if (humidityChart) humidityChart.setOption(humidityOption, true)
  if (ultrasonicChart) ultrasonicChart.setOption(ultrasonicOption, true)
}

watch(() => props.telemetryData, () => {
  nextTick(() => {
    updateCharts()
  })
}, { deep: true })

const handleResize = () => {
  tempChart?.resize()
  humidityChart?.resize()
  ultrasonicChart?.resize()
}

onMounted(() => {
  nextTick(() => {
    initCharts()
  })
  window.addEventListener('resize', handleResize)
})

onUnmounted(() => {
  window.removeEventListener('resize', handleResize)
  tempChart?.dispose()
  humidityChart?.dispose()
  ultrasonicChart?.dispose()
})
</script>

<style scoped>
.sensor-charts {
  height: 100%;
}

.chart-card {
  height: 100%;
}

.card-title {
  font-size: 16px;
  font-weight: 600;
  display: flex;
  align-items: center;
  gap: 8px;
}

.alert-banner {
  margin-bottom: 15px;
}

.charts-row {
  display: flex;
  gap: 12px;
}

.chart-col {
  flex: 1;
  min-width: 0;
  height: 220px;
}

@media (max-width: 992px) {
  .charts-row {
    flex-direction: column;
  }
  .chart-col {
    height: 160px;
  }
}
</style>

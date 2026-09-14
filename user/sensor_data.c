#include "sensor_data.h"

// ================================================================
// ===== 全局变量定义 =====
// ================================================================

// ★ 传感器数据（所有任务共享）
SensorData_t g_sensorData = {
    .temp_int = 0,
    .humi_int = 0,
    .led_flag = 0,
    .alarm_flag = 0,
    .temp_threshold = 30,
    .hum_threshold = 80,
    .current_page = 1
};

// ★ 数据互斥锁（保护 g_sensorData）
SemaphoreHandle_t g_dataMutex = NULL;
#ifndef __SENSOR_DATA_H
#define __SENSOR_DATA_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "semphr.h"

// ★ 传感器数据结构体
typedef struct {
    int temp_int;           // 温度（整数）
    int humi_int;           // 湿度（整数）
    uint8_t led_flag;       // LED 状态
    uint8_t alarm_flag;     // 蜂鸣器状态
    
    // LCD 显示相关
    int temp_threshold;     // 温度阈值
    int hum_threshold;      // 湿度阈值
    int current_page;       // 当前页面
} SensorData_t;

// ★ 全局变量声明
extern volatile SensorData_t g_sensorData;
extern SemaphoreHandle_t g_dataMutex;

#endif
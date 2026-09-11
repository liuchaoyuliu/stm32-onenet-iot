#include "upload_task.h"
#include "bsp_esp8266.h"
#include "bsp_usart.h"
#include "net_status.h"
#include <stdio.h>
#include <string.h>
#include "semphr.h"
// ★ 外部变量声明（在 main.c 中定义）
extern volatile SensorData_t g_sensorData;
extern SemaphoreHandle_t g_dataMutex;

/**
 * @brief 属性上报任务（每 5 秒上报一次）
 */
void Upload_Task(void *pvParameters)
{
    char payload[256];
    int temp, humi;
    uint8_t led, alarm;
    
    UsartPrintf(USART1, "\r\n[Upload_Task] Started!\r\n");
    
    while (1)
    {
        // ★ 检查网络状态
        if (g_net_status != NET_STATUS_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }
        
        // ★★★ 读取传感器数据和设备状态 ★★★
        if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            temp = g_sensorData.temp_int;
            humi = g_sensorData.humi_int;
            led = g_sensorData.led_flag;
            alarm = g_sensorData.alarm_flag;
            xSemaphoreGive(g_dataMutex);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        
        // ★ 构建 JSON（包含 Temp、Hum、Led、Alarm）
        sprintf(payload,
                "{\"id\":\"123\",\"params\":{"
                "\"Temp\":{\"value\":%d},"
                "\"Hum\":{\"value\":%d},"
                "\"Led\":{\"value\":%s},"
                "\"Alarm\":{\"value\":%s}"
                "}}",
                temp, humi,
                led ? "true" : "false",
                alarm ? "true" : "false");
        
        UsartPrintf(USART1, "[Upload] %s\r\n", payload);
        
        // ★ 发送
        if (ESP8266_MQTT_Publish(TOPIC_PROPERTY_POST, payload, 0)) {
            UsartPrintf(USART1, "[Upload] OK\r\n");
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
            if (!ESP8266_MQTT_Publish(TOPIC_PROPERTY_POST, payload, 0)) {   // 重试一次
                UsartPrintf(USART1, "[Upload] FAILED\r\n");
                g_net_status = NET_STATUS_CONNECTING;
                subscribed = 0;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(UPLOAD_INTERVAL_MS));
    }
}
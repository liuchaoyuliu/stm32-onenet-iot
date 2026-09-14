#include "key_task.h"
#include "key.h"
#include "sensor_data.h"      // ★ g_sensorData 和 g_dataMutex 定义在这里

// ★ 按键任务句柄
TaskHandle_t xKeyTaskHandle = NULL;

/**
 * @brief 按键处理任务
 * 
 * @note 职责：
 *       1. 扫描按键
 *       2. WK_UP：切换页面
 *       3. KEY0：增加阈值
 *       4. KEY1：减少阈值
 */
void Key_Task(void *pvParameters)
{
    u8 key_val;
    
    while(1)
    {
        key_val = KEY_Scan(0);
        
        if (key_val != 0) {
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                switch(key_val) {
                    case 3:   // WK_UP: 切换页面
                        g_sensorData.current_page++;
                        if (g_sensorData.current_page > 3) {
                            g_sensorData.current_page = 1;
                        }
                        break;
                        
                    case 1:   // KEY0: 增加阈值
                        if (g_sensorData.current_page == 2) {
                            if (g_sensorData.temp_threshold < 100) {
                                g_sensorData.temp_threshold++;
                            }
                        } else if (g_sensorData.current_page == 3) {
                            if (g_sensorData.hum_threshold < 100) {
                                g_sensorData.hum_threshold++;
                            }
                        }
                        break;
                        
                    case 2:   // KEY1: 减少阈值
                        if (g_sensorData.current_page == 2) {
                            if (g_sensorData.temp_threshold > 0) {
                                g_sensorData.temp_threshold--;
                            }
                        } else if (g_sensorData.current_page == 3) {
                            if (g_sensorData.hum_threshold > 0) {
                                g_sensorData.hum_threshold--;
                            }
                        }
                        break;
                        
                    default:
                        break;
                }
                xSemaphoreGive(g_dataMutex);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
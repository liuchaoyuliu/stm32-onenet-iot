#include "display_task.h"
#include "lcd.h"
#include "bsp_usart.h"
#include "net_status.h"
#include "sensor_data.h"      // ★ g_sensorData 和 g_dataMutex 定义在这里
#include <stdio.h>
#include <string.h>

// ★ 显示任务句柄
TaskHandle_t xDisplayTaskHandle = NULL;

// ================================================================
// ===== 辅助显示函数 =====
// ================================================================
void LCD_ShowInt(u16 x, u16 y, int num, u8 size)
{
    char buf[10];
    sprintf(buf, "%d", num);
    LCD_ShowString(x, y, 200, size, size, (u8*)buf);
}

// ================================================================
// ===== 主界面：完整绘制 =====
// ================================================================
void LCD_DrawMainPage(void)
{
    int temp, humi, temp_th, hum_th;
    char buf[10];
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        temp = g_sensorData.temp_int;
        humi = g_sensorData.humi_int;
        temp_th = g_sensorData.temp_threshold;
        hum_th = g_sensorData.hum_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    LCD_Fill(0, 50, 319, 239, WHITE);
    
    LCD_ShowChineseString16(60, 50, "主界面", BLACK);
    
    LCD_ShowChineseString16(20, 90, "温度", BLACK);
    LCD_ShowString(52, 90, 200, 24, 24, (u8*)":");
    sprintf(buf, "%d", temp);
    LCD_ShowString(100, 90, 200, 24, 24, (u8*)buf);
    LCD_ShowString(180, 90, 200, 24, 24, (u8*)" C");
    
    LCD_ShowChineseString16(20, 130, "湿度", BLACK);
    LCD_ShowString(52, 130, 200, 24, 24, (u8*)":");
    sprintf(buf, "%d", humi);
    LCD_ShowString(100, 130, 200, 24, 24, (u8*)buf);
    LCD_ShowString(180, 130, 200, 24, 24, (u8*)" %");
    
    LCD_ShowChineseString16(20, 180, "温度阈值", BLACK);
    LCD_ShowString(100, 180, 200, 16, 16, (u8*)":");
    sprintf(buf, "%d", temp_th);
    LCD_ShowString(115, 180, 200, 16, 16, (u8*)buf);
    LCD_ShowString(140, 180, 200, 16, 16, (u8*)"C");
    
    LCD_ShowChineseString16(20, 200, "湿度阈值", BLACK);
    LCD_ShowString(100, 200, 200, 16, 16, (u8*)":");
    sprintf(buf, "%d", hum_th);
    LCD_ShowString(115, 200, 200, 16, 16, (u8*)buf);
    LCD_ShowString(140, 200, 200, 16, 16, (u8*)"%");
    
    LCD_ShowString(10, 215, 200, 16, 16, (u8*)"UP:");
    LCD_ShowChineseString16(40, 215, "切换界面", BLACK);
    
    LCD_ShowString(10, 235, 200, 16, 16, (u8*)"KEY0:");
    LCD_ShowChineseString16(55, 235, "加", BLACK);
    LCD_ShowString(100, 235, 200, 16, 16, (u8*)"  KEY1:");
    LCD_ShowChineseString16(160, 235, "减", BLACK);
}

// ================================================================
// ===== 温度阈值界面：完整绘制 =====
// ================================================================
void LCD_DrawTempThresholdPage(void)
{
    int temp_th;
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        temp_th = g_sensorData.temp_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    LCD_Fill(0, 50, 319, 239, WHITE);
    
    LCD_ShowChineseString16(30, 50, "温度阈值设置", BLACK);
    
    LCD_ShowChineseString16(20, 100, "当前阈值", BLACK);
    LCD_ShowString(110, 100, 200, 24, 24, (u8*)": ");
    LCD_ShowInt(160, 100, temp_th, 24);
    LCD_ShowString(200, 100, 200, 24, 24, (u8*)" C");
    
    LCD_Fill(20, 150, 20 + (temp_th * 2), 170, RED);
    LCD_DrawRectangle(20, 150, 220, 170);
    LCD_ShowString(70, 180, 200, 16, 16, (u8*)"MAX:100");
    
    LCD_ShowString(10, 225, 200, 16, 16, (u8*)"UP:");
    LCD_ShowChineseString16(40, 225, "切换界面", BLACK);
    
    LCD_ShowString(10, 245, 200, 16, 16, (u8*)"KEY0:");
    LCD_ShowChineseString16(55, 245, "加", BLACK);
    LCD_ShowString(100, 245, 200, 16, 16, (u8*)"  KEY1:");
    LCD_ShowChineseString16(160, 245, "减", BLACK);
}

// ================================================================
// ===== 湿度阈值界面：完整绘制 =====
// ================================================================
void LCD_DrawHumThresholdPage(void)
{
    int hum_th;
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        hum_th = g_sensorData.hum_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    LCD_Fill(0, 50, 319, 239, WHITE);
    
    LCD_ShowChineseString16(30, 50, "湿度阈值设置", BLACK);
    
    LCD_ShowChineseString16(20, 100, "当前阈值", BLACK);
    LCD_ShowString(110, 100, 200, 24, 24, (u8*)": ");
    LCD_ShowInt(160, 100, hum_th, 24);
    LCD_ShowString(200, 100, 200, 24, 24, (u8*)" %");
    
    LCD_Fill(20, 150, 20 + hum_th, 170, BLUE);
    LCD_DrawRectangle(20, 150, 220, 170);
    LCD_ShowString(70, 180, 200, 16, 16, (u8*)"MAX:100");
    
    LCD_ShowString(10, 215, 200, 16, 16, (u8*)"UP:");
    LCD_ShowChineseString16(40, 215, "切换界面", BLACK);
    
    LCD_ShowString(10, 235, 200, 16, 16, (u8*)"KEY0:");
    LCD_ShowChineseString16(55, 235, "加", BLACK);
    LCD_ShowString(100, 235, 200, 16, 16, (u8*)"  KEY1:");
    LCD_ShowChineseString16(160, 235, "减", BLACK);
}

// ================================================================
// ===== 主界面：只更新数值 =====
// ================================================================
void LCD_UpdateMainPage(void)
{
    int temp, humi, temp_th, hum_th;
    char buf[10];
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        temp = g_sensorData.temp_int;
        humi = g_sensorData.humi_int;
        temp_th = g_sensorData.temp_threshold;
        hum_th = g_sensorData.hum_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    sprintf(buf, "%d   ", temp);
    LCD_ShowString(100, 90, 200, 24, 24, (u8*)buf);
    
    sprintf(buf, "%d   ", humi);
    LCD_ShowString(100, 130, 200, 24, 24, (u8*)buf);
    
    LCD_Fill(115, 180, 135, 196, WHITE);
    sprintf(buf, "%d", temp_th);
    LCD_ShowString(115, 180, 200, 16, 16, (u8*)buf);
    
    LCD_Fill(115, 200, 135, 216, WHITE);
    sprintf(buf, "%d", hum_th);
    LCD_ShowString(115, 200, 200, 16, 16, (u8*)buf);
}

// ================================================================
// ===== 温度阈值界面：只更新数值和进度条 =====
// ================================================================
void LCD_UpdateTempThresholdPage(void)
{
    int temp_th;
    char buf[10];
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        temp_th = g_sensorData.temp_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    LCD_Fill(160, 100, 195, 124, WHITE);
    sprintf(buf, "%d", temp_th);
    LCD_ShowString(160, 100, 200, 24, 24, (u8*)buf);
    
    LCD_Fill(20, 150, 220, 170, WHITE);
    LCD_Fill(20, 150, 20 + (temp_th * 2), 170, RED);
    LCD_DrawRectangle(20, 150, 220, 170);
}

// ================================================================
// ===== 湿度阈值界面：只更新数值和进度条 =====
// ================================================================
void LCD_UpdateHumThresholdPage(void)
{
    int hum_th;
    char buf[10];
    
    if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
        hum_th = g_sensorData.hum_threshold;
        xSemaphoreGive(g_dataMutex);
    }
    
    LCD_Fill(160, 100, 195, 124, WHITE);
    sprintf(buf, "%d", hum_th);
    LCD_ShowString(160, 100, 200, 24, 24, (u8*)buf);
    
    LCD_Fill(20, 150, 220, 170, WHITE);
    LCD_Fill(20, 150, 20 + hum_th, 170, BLUE);
    LCD_DrawRectangle(20, 150, 220, 170);
}

// ================================================================
// ===== 显示任务 =====
// ================================================================
void Display_Task(void *pvParameters)
{
    TickType_t lastWakeTime = xTaskGetTickCount();
    int last_page = -1;
    vTaskDelay(pdMS_TO_TICKS(5000));
    while(1)
    {
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
        
        int page;
        if (xSemaphoreTake(g_dataMutex, portMAX_DELAY) == pdTRUE) {
            page = g_sensorData.current_page;
            xSemaphoreGive(g_dataMutex);
        }
        
        if (page != last_page) {
            LCD_Clear(WHITE);
            LCD_ShowChineseString16(10, 5, "温湿度监控", BLACK);
            LCD_ShowString(130, 5, 200, 16, 16, (u8*)"v1.0");
            LCD_ShowString(10, 25, 200, 16, 16, (u8*)"====================");
            
            switch(page) {
                case 1:  LCD_DrawMainPage(); break;
                case 2:  LCD_DrawTempThresholdPage(); break;
                case 3:  LCD_DrawHumThresholdPage(); break;
                default: LCD_DrawMainPage(); break;
            }
            last_page = page;
        } else {
            switch(page) {
                case 1:  LCD_UpdateMainPage(); break;
                case 2:  LCD_UpdateTempThresholdPage(); break;
                case 3:  LCD_UpdateHumThresholdPage(); break;
                default: LCD_UpdateMainPage(); break;
            }
        }
    }
}
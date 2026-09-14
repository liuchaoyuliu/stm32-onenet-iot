#ifndef __DISPLAY_TASK_H
#define __DISPLAY_TASK_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// ★ 显示任务句柄
extern TaskHandle_t xDisplayTaskHandle;

// ★ 显示任务函数
void Display_Task(void *pvParameters);

#endif
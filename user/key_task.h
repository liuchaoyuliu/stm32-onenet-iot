#ifndef __KEY_TASK_H
#define __KEY_TASK_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

// ★ 按键任务句柄
extern TaskHandle_t xKeyTaskHandle;

// ★ 按键任务函数
void Key_Task(void *pvParameters);

#endif
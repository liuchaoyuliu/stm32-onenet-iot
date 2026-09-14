#ifndef __BSP_RUNTIMESTATS_H
#define __BSP_RUNTIMESTATS_H

#include "stm32f10x.h"

// 全局计数变量，供 FreeRTOS 读取
extern volatile unsigned long FreeRTOSRunTimeTicks;

// 初始化函数，FreeRTOS 会自动调用
void ConfigureTimerForRunTimeStats(void);

#endif
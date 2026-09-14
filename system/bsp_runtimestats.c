#include "bsp_runtimestats.h"

volatile unsigned long FreeRTOSRunTimeTicks = 0;

/**
 * @brief  初始化 TIM6，产生 50us 中断，用于 FreeRTOS 运行时间统计
 * @note   50us 是 1ms SysTick 的 20 倍，精度足够且负担小
 */
void ConfigureTimerForRunTimeStats(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    // 1. 使能 TIM6 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM6, ENABLE);

    // 2. 配置时基：72MHz 时钟，预分频 71 → 1MHz，重装载 49 → 50us
    TIM_TimeBaseStructure.TIM_Period = 49;
    TIM_TimeBaseStructure.TIM_Prescaler = 71;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM6, &TIM_TimeBaseStructure);

    // 3. 使能更新中断
    TIM_ITConfig(TIM6, TIM_IT_Update, ENABLE);

    // 4. 配置 NVIC
    // 注意：优先级必须高于 FreeRTOS 管辖的最高优先级
    //       Cortex-M3 数值越小优先级越高，这里设为 0
    NVIC_InitStructure.NVIC_IRQChannel = TIM6_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. 清零计数器并启动
    FreeRTOSRunTimeTicks = 0;
    TIM_Cmd(TIM6, ENABLE);
}

/**
 * @brief  TIM6 中断服务函数，累加计数
 */
void TIM6_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM6, TIM_IT_Update) != RESET)
    {
        FreeRTOSRunTimeTicks++;
        TIM_ClearITPendingBit(TIM6, TIM_IT_Update);
    }
}
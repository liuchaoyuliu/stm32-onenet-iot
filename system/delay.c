#include "delay.h"
#include "FreeRTOS.h"
#include "task.h"

////////////////////////////////////////////////////////////////////////////////// 	 
// 本程序只供学习使用，未经作者许可，不得用于其它任何用途
// ALIENTEK STM32开发板
// 使用SysTick的普通计数模式对延迟进行管理（适合STM32F10x系列）
// 修改：适配 FreeRTOS
//////////////////////////////////////////////////////////////////////////////////  

static u8  fac_us = 0;		// us延时倍乘数			   
static u16 fac_ms = 0;		// ms延时倍乘数

// 当 SYSTEM_SUPPORT_OS == 1 时，适配 FreeRTOS
#if SYSTEM_SUPPORT_OS
    // FreeRTOS 调度器是否在运行
    #define delay_osrunning     (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    // FreeRTOS 系统时钟节拍频率
    #define delay_ostickspersec configTICK_RATE_HZ
    // FreeRTOS 中断嵌套级别（默认0）
    #define delay_osintnesting  0

    // us级延时时，挂起任务调度
    void delay_osschedlock(void)
    {
        vTaskSuspendAll();
    }

    // us级延时时，恢复任务调度
    void delay_osschedunlock(void)
    {
        xTaskResumeAll();
    }

    // 调用 FreeRTOS 延时
    void delay_ostimedly(u32 ticks)
    {
        vTaskDelay(ticks);
    }

    // SysTick 中断服务函数（由 FreeRTOS 接管）
    // void SysTick_Handler(void)
    // {
    //     if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED)
    //     {
    //         xPortSysTickHandler();
    //     }
    // }
#endif

// 初始化延迟函数
void delay_init(void)
{
#if SYSTEM_SUPPORT_OS
    u32 reload;
#endif
    SysTick_CLKSourceConfig(SysTick_CLKSource_HCLK_Div8);	// 选择外部时钟 HCLK/8
    fac_us = SystemCoreClock / 8000000;					// 为系统时钟的1/8  

#if SYSTEM_SUPPORT_OS
    reload = SystemCoreClock / 8000000;					// 每秒钟的计数次数 单位为K	   
    reload *= 1000000 / delay_ostickspersec;				// 根据delay_ostickspersec设定溢出时间
    fac_ms = 1000 / delay_ostickspersec;					// 代表OS可以延时的最少单位	   
    
    // FreeRTOS 会接管 SysTick，这里不使能中断
    // SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;			// 注释掉
    SysTick->LOAD = reload; 								// 每1/delay_ostickspersec秒中断一次	
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;   			// 开启SYSTICK计数器
#else
    fac_ms = (u16)fac_us * 1000;							// 非OS下,代表每个ms需要的systick时钟数   
#endif
}

#if SYSTEM_SUPPORT_OS
// 延时nus（支持OS）
void delay_us(u32 nus)
{		
    u32 ticks;
    u32 told, tnow, tcnt = 0;
    u32 reload = SysTick->LOAD;
    ticks = nus * fac_us;
    tcnt = 0;
    delay_osschedlock();						// 挂起调度，防止打断us延时
    told = SysTick->VAL;
    while(1)
    {
        tnow = SysTick->VAL;	
        if(tnow != told)
        {	    
            if(tnow < told) tcnt += told - tnow;
            else tcnt += reload - tnow + told;	    
            told = tnow;
            if(tcnt >= ticks) break;
        }  
    };
    delay_osschedunlock();						// 恢复调度
}

// 延时nms（支持OS）
void delay_ms(u16 nms)
{	
    if(delay_osrunning && delay_osintnesting == 0)	// OS在运行且不在中断中
    {		 
        if(nms >= fac_ms)						// 延时时间大于OS最小时间周期 
        { 
            delay_ostimedly(nms / fac_ms);		// OS延时
        }
        nms %= fac_ms;							// 剩余时间用普通延时
    }
    delay_us((u32)(nms * 1000));				// 普通方式延时  
}

#else // 不用OS

// 延时nus（裸机）
void delay_us(u32 nus)
{		
    u32 temp;	    	 
    SysTick->LOAD = nus * fac_us; 				// 时间加载		  	 
    SysTick->VAL = 0x00;        				// 清空计数器
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;	// 开始倒数	  
    do
    {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1 << 16)));	// 等待时间到达   
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;	// 关闭计数器
    SysTick->VAL = 0X00;      					// 清空计数器	 
}

// 延时nms（裸机）
void delay_ms(u16 nms)
{	 		  	  
    u32 temp;		   
    SysTick->LOAD = (u32)nms * fac_ms;			// 时间加载
    SysTick->VAL = 0x00;						// 清空计数器
    SysTick->CTRL |= SysTick_CTRL_ENABLE_Msk;	// 开始倒数  
    do
    {
        temp = SysTick->CTRL;
    } while((temp & 0x01) && !(temp & (1 << 16)));	// 等待时间到达   
    SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;	// 关闭计数器
    SysTick->VAL = 0X00;       					// 清空计数器	  
} 
#endif
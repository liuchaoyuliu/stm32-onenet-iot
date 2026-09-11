#include "dht11.h"
#include "delay.h"

 //////////////////////////////////////////////////////////////////////////////////	 
//本程序只供学习使用，未经作者许可，不得用于其它任何用途
//ALIENTEK精英STM32开发板
//DHT11数字温湿度传感器驱动代码	   
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//修改日期:2012/9/12
//版本：V1.0
//版权所有，盗版必究。
//Copyright(C) 广州市星翼电子科技有限公司 2009-2019
//All rights reserved									  
//////////////////////////////////////////////////////////////////////////////////
// dht11.c 顶部
#include "stm32f10x.h"
/**
 * @brief  DHT11 专用微秒延时（SysTick->VAL 版）
 * 
 * @param  nus 延时微秒数
 * 
 * @note   为什么用这个方案？
 *         - 正点原子的 delay_us 在 FreeRTOS 下用 vTaskSuspendAll，开销大
 *         - TIM4 方案依赖 APB1 时钟，如果时钟配置不对会出错
 *         - SysTick->VAL 只读计数器，不修改配置，最安全
 * 
 * @note   原理：
 *         - SysTick 是 24 位递减计数器，从 LOAD 递减到 0，自动重装
 *         - 只读取当前 VAL 值，计算经过的时钟数
 *         - 当累计时钟数 >= 需要的时钟数时，退出
 * 
 * @note   为什么安全？
 *         - 不修改 SysTick->LOAD 和 CTRL
 *         - 不关中断
 *         - 不影响 FreeRTOS 的调度
 * 
 * @note   注意事项：
 *         - SysTick 由 FreeRTOS 初始化为 1ms 周期
 *         - 如果延时超过 1ms，SysTick 会重装，需要处理重装情况
 *         - DHT11 最长延时 40us，远小于 1ms，安全
 */
static void DHT11_DelayUs(u32 nus)
{
    // ★ 计算需要多少个时钟周期
    // SystemCoreClock = 72MHz
    // SystemCoreClock / 1000000 = 72（1us 对应 72 个时钟）
    // ticks = nus * 72（比如 40us → 2880 个时钟）
    u32 ticks = nus * (SystemCoreClock / 1000000);
    
    // ★ 记录当前 SysTick 计数值（作为起点）
    u32 told = SysTick->VAL;
    
    // tnow：当前计数值（每次循环读取）
    // tcnt：累计经过的时钟数（初始 0）
    u32 tnow, tcnt = 0;
    
    // ★ 记录重装载值（FreeRTOS 默认 72000，对应 1ms）
    u32 reload = SysTick->LOAD;
    
    // ★ 循环直到累计时钟数达到目标
    while (tcnt < ticks) {
        
        // ★ 读取当前计数值
        tnow = SysTick->VAL;
        
        // ★ 只有计数值变化了才处理（避免重复计算）
        if (tnow != told) {
            
            if (tnow < told) {
                // ★ 情况1：正常递减（told → tnow）
                // 经过的时钟数 = told - tnow
                tcnt += told - tnow;
            } else {
                // ★ 情况2：发生重装（told → 0 → reload → tnow）
                // 经过的时钟数 = (reload - tnow) + told
                tcnt += reload - tnow + told;
            }
            
            // ★ 更新上次计数值，准备下一次比较
            told = tnow;
        }
    }
} 
//复位DHT11
void DHT11_Rst(void)	   
{                 
	DHT11_IO_OUT(); 	//SET OUTPUT
    DHT11_DQ_OUT=0; 	//拉低DQ
    delay_ms(20);    	//拉低至少18ms
    DHT11_DQ_OUT=1; 	//DQ=1 
	DHT11_DelayUs(30);     	//主机拉高20~40us
}
//等待DHT11的回应
//返回1:未检测到DHT11的存在
//返回0:存在
u8 DHT11_Check(void) 	   
{   
	u8 retry=0;
	DHT11_IO_IN();//SET INPUT	 
    while (DHT11_DQ_IN&&retry<100)//DHT11会拉低40~80us
	{
		retry++;
		DHT11_DelayUs(1);
	};	 
	if(retry>=100)return 1;
	else retry=0;
    while (!DHT11_DQ_IN&&retry<100)//DHT11拉低后会再次拉高40~80us
	{
		retry++;
		DHT11_DelayUs(1);
	};
	if(retry>=100)return 1;	    
	return 0;
}
//从DHT11读取一个位
//返回值：1/0
u8 DHT11_Read_Bit(void) 			 
{
 	u8 retry=0;
	while(DHT11_DQ_IN&&retry<100)//等待变为低电平
	{
		retry++;
		DHT11_DelayUs(1);
	}
	retry=0;
	while(!DHT11_DQ_IN&&retry<100)//等待变高电平
	{
		retry++;
		DHT11_DelayUs(1);
	}
	DHT11_DelayUs(40);//等待40us
	if(DHT11_DQ_IN)return 1;
	else return 0;		   
}
//从DHT11读取一个字节
//返回值：读到的数据
u8 DHT11_Read_Byte(void)    
{        
    u8 i,dat;
    dat=0;
	for (i=0;i<8;i++) 
	{
   		dat<<=1; 
	    dat|=DHT11_Read_Bit();
    }						    
    return dat;
}
//从DHT11读取一次数据
//temp:温度值(范围:0~50°)
//humi:湿度值(范围:20%~90%)
//返回值：0,正常;1,读取失败
u8 DHT11_Read_Data(u8 *temp,u8 *humi)    
{        
 	u8 buf[5];
	u8 i;
	DHT11_Rst();
	if(DHT11_Check()==0)
	{
		for(i=0;i<5;i++)//读取40位数据
		{
			buf[i]=DHT11_Read_Byte();
		}
		if((buf[0]+buf[1]+buf[2]+buf[3])==buf[4])
		{
			*humi=buf[0];
			*temp=buf[2];
		}
	}else return 1;
	return 0;	    
}
//初始化DHT11的IO口 DQ 同时检测DHT11的存在
//返回1:不存在
//返回0:存在    	 
u8 DHT11_Init(void)
{	 
 	GPIO_InitTypeDef  GPIO_InitStructure;
 	
 	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOG, ENABLE);	 //使能PG端口时钟
	
 	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;				 //PG11端口配置
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP; 		 //推挽输出
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOG, &GPIO_InitStructure);				 //初始化IO口
 	GPIO_SetBits(GPIOG,GPIO_Pin_11);						 //PG11 输出高
		    
	DHT11_Rst();  //复位DHT11
	return DHT11_Check();//等待DHT11的回应
} 








#include "bsp_usart.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <semphr.h>
#include "led.h"
#include "beep.h"

SemaphoreHandle_t xUart1Mutex;   //串口1发送互斥锁，防止多任务同时打印导致串口数据混乱
// ================================================================
// ===== DMA接收缓冲区 =====
// ================================================================
static uint8_t usart2_dma_rx_buf[USART2_DMA_BUF_SIZE];

// ================================================================
// ===== 帧状态变量 =====
// ================================================================
static volatile uint16_t usart2_rx_len = 0;
static volatile uint8_t usart2_frame_ready = 0;

// ================================================================
// ===== 双队列句柄 =====
// ================================================================
QueueHandle_t esp8266_at_queue = NULL;
QueueHandle_t esp8266_mqtt_queue = NULL;

// ================================================================
// ===== 队列初始化 =====
// ================================================================
void ESP8266_Queue_Init(void)
{
    esp8266_at_queue = xQueueCreate(ESP8266_MSG_QUEUE_LEN, sizeof(ESP8266_Msg_t));
    esp8266_mqtt_queue = xQueueCreate(ESP8266_MSG_QUEUE_LEN, sizeof(ESP8266_Msg_t));
}

// ================================================================
// ===== 队列读取函数 =====
// ================================================================
BaseType_t ESP8266_GetATMsg(ESP8266_Msg_t *msg, TickType_t waitTicks)
{
    if (esp8266_at_queue == NULL) return pdFALSE;
    return xQueueReceive(esp8266_at_queue, msg, waitTicks);
}

BaseType_t ESP8266_GetMQTTMsg(ESP8266_Msg_t *msg, TickType_t waitTicks)
{
    if (esp8266_mqtt_queue == NULL) return pdFALSE;
    return xQueueReceive(esp8266_mqtt_queue, msg, waitTicks);
}

// ================================================================
// ===== 分拣器：分离 AT 响应和 MQTT 数据 =====
// ================================================================
static void ESP8266_DispatchFrame(uint8_t *data, uint16_t len)
{
    ESP8266_Msg_t msg;
    char *ipd_pos;
    
    if (len == 0) return;
    
    //UsartPrintf(USART1, "[Frame] %.*s\r\n", len, data);
    
    // 查找 +IPD
    ipd_pos = strstr((char*)data, "+IPD,");
    
    if (ipd_pos != NULL) {
        // ===== 有 +IPD，需要拆分 =====
        
        // ① +IPD 之前的数据 → AT 队列
        if (ipd_pos > (char*)data) {
            uint16_t at_len = ipd_pos - (char*)data;
            if (at_len > 0) {
                msg.len = at_len;
                memcpy(msg.data, data, at_len);
                msg.data[at_len] = 0;
                xQueueSend(esp8266_at_queue, &msg, 0);
            }
        }
        
        // ② +IPD 及之后的数据 → MQTT 队列
        uint16_t mqtt_len = len - (ipd_pos - (char*)data);
        if (mqtt_len > 0) {
            msg.len = mqtt_len;
            //UsartPrintf(USART1, "[Frame] %.*s\r\n", mqtt_len, ipd_pos);
            memcpy(msg.data, ipd_pos, mqtt_len);
            msg.data[mqtt_len] = 0;
            xQueueSend(esp8266_mqtt_queue, &msg, 0);
        }
        
    } else {
        // ===== 没有 +IPD → 全部是 AT 响应 =====
        msg.len = len;
        memcpy(msg.data, data, len);
        msg.data[len] = 0;
        xQueueSend(esp8266_at_queue, &msg, 0);
    }
}

// ================================================================
// ===== 串口1初始化（调试） =====
// ================================================================
void Usart1_Init(unsigned int baud)
{
    GPIO_InitTypeDef gpioInitStruct;
    USART_InitTypeDef usartInitStruct;
    NVIC_InitTypeDef nvicInitStruct;
    
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    
    // PA9 TXD
    gpioInitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    gpioInitStruct.GPIO_Pin = GPIO_Pin_9;
    gpioInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpioInitStruct);
    
    // PA10 RXD
    gpioInitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpioInitStruct.GPIO_Pin = GPIO_Pin_10;
    GPIO_Init(GPIOA, &gpioInitStruct);
    
    usartInitStruct.USART_BaudRate = baud;
    usartInitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usartInitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    usartInitStruct.USART_Parity = USART_Parity_No;
    usartInitStruct.USART_StopBits = USART_StopBits_1;
    usartInitStruct.USART_WordLength = USART_WordLength_8b;
    
    USART_Init(USART1, &usartInitStruct);
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
    
    nvicInitStruct.NVIC_IRQChannel = USART1_IRQn;
    nvicInitStruct.NVIC_IRQChannelCmd = ENABLE;
    nvicInitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    nvicInitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&nvicInitStruct);

    USART_Cmd(USART1, ENABLE);
    xUart1Mutex = xSemaphoreCreateMutex();
    if (xUart1Mutex == NULL) {
        while(1); // 创建失败，系统无法运行
    }
}
void ESP8266_RST_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct;
    
    RCC_APB2PeriphClockCmd(ESP8266_RST_RCC, ENABLE);
    
    GPIO_InitStruct.GPIO_Pin = ESP8266_RST_PIN;
    GPIO_InitStruct.GPIO_Mode = GPIO_Mode_Out_PP;   // 推挽输出
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(ESP8266_RST_PORT, &GPIO_InitStruct);
    
    // ★ 默认拉高（ESP8266 正常工作）
    GPIO_SetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);
}
// ================================================================
// ===== 串口2初始化（ESP8266）+ DMA + IDLE中断 =====
// ================================================================
void Usart2_Init(unsigned int baud)
{
    GPIO_InitTypeDef gpioInitStruct;
    USART_InitTypeDef usartInitStruct;
    NVIC_InitTypeDef nvicInitStruct;
    DMA_InitTypeDef dmaInitStruct;
    
    // --- 1. 使能时钟 ---
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    ESP8266_RST_Init();
    // --- 2. GPIO初始化 ---
    // PA2 TXD
    gpioInitStruct.GPIO_Mode = GPIO_Mode_AF_PP;
    gpioInitStruct.GPIO_Pin = GPIO_Pin_2;
    gpioInitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpioInitStruct);
    
    // PA3 RXD
    gpioInitStruct.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpioInitStruct.GPIO_Pin = GPIO_Pin_3;
    GPIO_Init(GPIOA, &gpioInitStruct);
    
    // --- 3. 串口初始化 ---
    usartInitStruct.USART_BaudRate = baud;
    usartInitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usartInitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    usartInitStruct.USART_Parity = USART_Parity_No;
    usartInitStruct.USART_StopBits = USART_StopBits_1;
    usartInitStruct.USART_WordLength = USART_WordLength_8b;
    USART_Init(USART2, &usartInitStruct);
    
    // --- 4. DMA初始化（环形模式，无中断） ---
    DMA_DeInit(DMA1_Channel6);
    dmaInitStruct.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
    dmaInitStruct.DMA_MemoryBaseAddr = (uint32_t)usart2_dma_rx_buf;
    dmaInitStruct.DMA_DIR = DMA_DIR_PeripheralSRC;
    dmaInitStruct.DMA_BufferSize = USART2_DMA_BUF_SIZE;
    dmaInitStruct.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dmaInitStruct.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dmaInitStruct.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    dmaInitStruct.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    dmaInitStruct.DMA_Mode = DMA_Mode_Circular;
    dmaInitStruct.DMA_Priority = DMA_Priority_High;
    dmaInitStruct.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel6, &dmaInitStruct);
    DMA_Cmd(DMA1_Channel6, ENABLE);
    
    // --- 5. 使能串口DMA接收 ---
    USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
    USART_Cmd(USART2, ENABLE);
    
    // ============================================================
    // --- 6. 使能 IDLE 中断（代替定时器） ---
    // ============================================================
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
    
    // --- 7. 配置串口中断优先级 ---
    nvicInitStruct.NVIC_IRQChannel = USART2_IRQn;
    nvicInitStruct.NVIC_IRQChannelCmd = ENABLE;
    nvicInitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    nvicInitStruct.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&nvicInitStruct);
}

// ================================================================
// ===== 串口2中断服务函数（IDLE检测 + 分包） =====
// ================================================================
void USART2_IRQHandler(void)
{
    // ★★★ 检测 IDLE 中断（一帧数据接收完成） ★★★
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET) {
        GPIOB->ODR ^= GPIO_Pin_5;//
        // 1. 清除 IDLE 标志（先读SR，再读DR）
        USART_GetITStatus(USART2, USART_IT_IDLE);
        USART_ReceiveData(USART2);
        
        //UsartPrintf(USART1, "[IDLE] Frame end!\r\n");
        
        // 2. 检查是否有帧待处理
        if (usart2_frame_ready == 0) {
            
            // 3. 关闭DMA，获取接收长度
            DMA_Cmd(DMA1_Channel6, DISABLE);
            uint16_t remain = DMA_GetCurrDataCounter(DMA1_Channel6);
            uint16_t len = USART2_DMA_BUF_SIZE - remain;
            
            //UsartPrintf(USART1, "[IDLE] len=%d\r\n", len);
            
            if (len > 0 && len <= ESP8266_MSG_MAX_LEN) {
                usart2_rx_len = len;
                usart2_frame_ready = 1;
                
                // 4. 分拣并发送到队列
                ESP8266_DispatchFrame(usart2_dma_rx_buf, len);
            }
            
            // 5. 重置DMA
            DMA_SetCurrDataCounter(DMA1_Channel6, USART2_DMA_BUF_SIZE);
            DMA_Cmd(DMA1_Channel6, ENABLE);
            
            usart2_rx_len = 0;
            usart2_frame_ready = 0;
        }
    }
}

// ================================================================
// ===== 统一初始化 =====
// ================================================================
void Usart_Init(void)
{
    Usart1_Init(USART1_BAUDRATE);
    Usart2_Init(USART2_BAUDRATE);
    ESP8266_Queue_Init();
    UsartPrintf(USART1, "\r\n========== USART Init OK ==========\r\n");
    UsartPrintf(USART1, "USART1: Debug @ %d\r\n", USART1_BAUDRATE);
    UsartPrintf(USART1, "USART2: ESP8266 @ %d\r\n", USART2_BAUDRATE);
    UsartPrintf(USART1, "Frame detection: IDLE interrupt\r\n");
}

// ================================================================
// ===== 串口发送函数 =====
// ================================================================


void Usart_SendString(USART_TypeDef *USARTx, unsigned char *str, unsigned short len)
{
    SemaphoreHandle_t lock = NULL;

    // 只有串口 1 需要互斥量
    if (USARTx == USART1) {
        lock = xUart1Mutex;
        if (xSemaphoreTake(lock, pdMS_TO_TICKS(1000)) != pdTRUE) {
            return;   // 拿不到锁，放弃本次发送
        }
    }

    for (unsigned short i = 0; i < len; i++) {
        USART_SendData(USARTx, str[i]);

        uint32_t timeout = 100000;
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET) {
            if (--timeout == 0) {
                BEEP = BEEP_ON;
                if (lock) xSemaphoreGive(lock);   // 出错也要释放锁
                return;
            }
        }
    }

    if (lock) xSemaphoreGive(lock);
}

// ================================================================
// ===== 打印函数 =====
// ================================================================
// void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...)
// {
//     char buf[256];
//     va_list ap;
    
//     va_start(ap, fmt);
//     vsnprintf(buf, sizeof(buf), fmt, ap);
//     va_end(ap);
    
//     Usart_SendString(USARTx, (unsigned char*)buf, strlen(buf));
// }
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...)
{
    char buf[256];
    va_list ap;
   
    va_start(ap, fmt);
    
    unsigned short len = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (len < 0) return;
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    buf[len] = '\0';
    va_end(ap);
    Usart_SendString(USARTx, (unsigned char*)buf, len); 
}
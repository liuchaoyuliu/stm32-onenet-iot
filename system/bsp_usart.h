#ifndef __BSP_USART_H
#define __BSP_USART_H

#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <semphr.h>

// ================================================================
// ===== 串口1配置（调试） =====
// ================================================================
#define USART1_BAUDRATE         115200

// ================================================================
// ===== 串口2配置（ESP8266） =====
// ================================================================
#define USART2_BAUDRATE         115200

// ================================================================
// ===== DMA配置 =====
// ================================================================
#define USART2_DMA_BUF_SIZE     512

// ================================================================
// ===== 消息队列配置 =====
// ================================================================
#define ESP8266_MSG_QUEUE_LEN   10
#define ESP8266_MSG_MAX_LEN     256

// ================================================================
// ===== 消息结构体 =====
// ================================================================
typedef struct {
    uint16_t len;
    uint8_t data[ESP8266_MSG_MAX_LEN];
} ESP8266_Msg_t;

// ================================================================
// ===== 双队列句柄 =====
// ================================================================
extern QueueHandle_t esp8266_at_queue;
extern QueueHandle_t esp8266_mqtt_queue;
extern SemaphoreHandle_t xUart1Mutex;   // 声明，给其他文件用
// ================================================================
// ===== 函数声明 =====
// ================================================================
void Usart_Init(void);
void Usart_SendString(USART_TypeDef *USARTx, unsigned char *str, unsigned short len);
void UsartPrintf(USART_TypeDef *USARTx, char *fmt, ...);
BaseType_t ESP8266_GetATMsg(ESP8266_Msg_t *msg, TickType_t waitTicks);
BaseType_t ESP8266_GetMQTTMsg(ESP8266_Msg_t *msg, TickType_t waitTicks);

#endif
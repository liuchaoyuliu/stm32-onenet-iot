/**
 * @file bsp_esp8266.h
 * @brief ESP8266 WiFi模块驱动头文件
 * @note  基于DMA+定时器串口接收框架
 */

#ifndef __BSP_ESP8266_H
#define __BSP_ESP8266_H

#include "stm32f10x.h"
#include "bsp_usart.h"
#include "Net_Status.h"
#include "MqttKit.h"
#include "Common.h"
/*==============================================================================
 * 5. OneNET MQTT 配置
 *============================================================================*/

#define ONENET_MQTT_SERVER       "mqtt.heclouds.com"
#define ONENET_MQTT_PORT         "1883"
#define ONENET_PRODID            "SQ8gfZ73EX"
#define ONENET_DEVNAME           "Test1"
#define ONENET_APIKEY            "version=2018-10-31&res=products%2FSQ8gfZ73EX%2Fdevices%2FTest1&et=1808203155&method=md5&sign=Mrht4QnQ3ikjKwGVh4XZVQ%3D%3D"
#define ONENET_KEEPALIVE         600
//ONENET不支持clean session为0,所以设置为1
#define ONENET_CLEAN_SESSION       1 //ONENET只支持1
//遗嘱消息配置,onenet不支持遗嘱消息,所以设置为0
#define ONENET_WILL_QOS               0
#define ONENET_WILL_RETAIN           0
#define ONENET_WILL_TOPIC         NULL
#define ONENET_WILL_MSG           NULL

#define ONENET_TOPIC_PROPERTY_SET   "$sys/"ONENET_PRODID"/"ONENET_DEVNAME"/thing/property/set"
#define ONENET_TOPIC_PROPERTY_REPLY "$sys/"ONENET_PRODID"/"ONENET_DEVNAME"/thing/property/reply"
/*==============================================================================
 * 1. WiFi 网络配置（用户根据实际网络修改）
 *============================================================================*/

/** WiFi 热点名称（SSID） */
#define WIFI_SSID                "RedmiNote13Pro"

/** WiFi 密码 */
#define WIFI_PASSWORD            "123456789wslcy"


/*==============================================================================
 * 2. 超时与重试配置
 *============================================================================*/

/** 普通AT指令超时时间（毫秒） */
#define ESP8266_TIMEOUT          5000

/** WiFi连接超时时间（毫秒），连接WiFi需要更长时间 */
#define ESP8266_TIMEOUT_WIFI     15000

/** AT指令失败重试次数 */
#define ESP8266_RETRY_COUNT      3





/*==============================================================================
 * 4. 全局状态变量（外部声明，定义在 .c 文件中）
 *============================================================================*/



/*==============================================================================
 * 5. API 函数声明
 *============================================================================*/

/*------------------------------------
 * 5.1 初始化和状态
 *------------------------------------*/

/**
 * @brief 初始化ESP8266软件状态
 * @note  硬件初始化已在 Usart_Init() 中完成
 */
void ESP8266_Init(void);

/**
 * @brief 获取ESP8266当前状态
 * @return ESP8266_Status_t 状态枚举值
 */
Net_Status_t ESP8266_GetStatus(void);


/*------------------------------------
 * 5.2 AT指令发送
 *------------------------------------*/

/**
 * @brief 发送AT指令（不等待响应）
 * @param cmd 指令字符串（不含 \r\n）
 * @return 1=发送成功，0=发送失败
 * @note  指令会自动追加 \r\n
 */
uint8_t ESP8266_SendCmd(const char *cmd);

/**
 * @brief 从AT队列等待响应
 * @param expected 期望的响应字符串（如 "OK"）
 * @param timeout  超时时间（毫秒）
 * @return 1=收到期望响应，0=超时或收到错误
 * @note  不匹配的数据会放回队列，不会丢失
 */
uint8_t ESP8266_WaitResponse(const char *expected, uint16_t timeout);

/**
 * @brief 发送AT指令并等待响应（组合函数）
 * @param cmd      指令字符串
 * @param expected 期望的响应字符串
 * @param timeout  超时时间（毫秒）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_SendCmdWait(const char *cmd, const char *expected, uint16_t timeout);


/*------------------------------------
 * 5.3 WiFi功能
 *------------------------------------*/

/**
 * @brief AT指令测试
 * @return 1=AT指令正常，0=ESP8266无响应
 * @note  发送 "AT" 等待 "OK"
 */
uint8_t ESP8266_AT_Test(void);

/**
 * @brief 获取ESP8266的IP地址
 * @param ip_buffer 存放IP地址的缓冲区（至少20字节）
 * @return 1=成功获取IP，0=获取失败
 */
uint8_t ESP8266_GetIP(char *ip_buffer);

/**
 * @brief 连接WiFi（核心函数）
 * @param ssid     WiFi名称
 * @param password WiFi密码
 * @return 1=连接成功，0=连接失败
 * @note  内部包含：AT测试→设置模式→连接WiFi→获取IP→设置单连接
 */
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *password);
/**
 * @brief 获取WiFi连接状态
 * @return 1=WiFi已连接，0=WiFi未连接
 */
uint8_t ESP8266_GetWiFiStatus(void);

/*==============================================================================
 * 5. TCP 功能
 *============================================================================*/

/**
 * @brief 建立 TCP 连接
 * @param server 服务器地址（如 "mqtt.heclouds.com"）
 * @param port   端口号（如 "1883"）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_TCP_Connect(const char *server, const char *port);

/**
 * @brief 通过 TCP 发送数据
 * @param data 数据指针
 * @param len  数据长度
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_TCP_Send(const char *data, uint16_t len);

/**
 * @brief 关闭 TCP 连接
 * @return 1=成功
 */
uint8_t ESP8266_TCP_Close(void);


/*==============================================================================
 * 6. MQTT 功能
 *============================================================================*/

/**
 * @brief MQTT 连接 OneNET
 * @param prod_id  产品ID
 * @param dev_name 设备名称
 * @param api_key  设备级token
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Connect(const char *prod_id, const char *dev_name, const char *api_key);

/**
 * @brief MQTT 发布消息
 * @param topic   主题
 * @param payload 消息内容
 * @param qos     QoS等级（0或1）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Publish(const char *topic, const char *payload, uint8_t qos);

/**
 * @brief MQTT 订阅主题
 * @param topic 主题
 * @param qos   QoS等级（0或1）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Subscribe(const char *topic, uint8_t qos);

/**
 * @brief MQTT 心跳
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Ping(void);

void ESP8266_Clear_All(void);
void ESP8266_Clear_AT(void);
void ESP8266_ExitTransparent(void);
void ESP8266_HardReset(void);
#endif /* __BSP_ESP8266_H */
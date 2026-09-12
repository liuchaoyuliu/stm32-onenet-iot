#include "stm32f10x.h"
#include "FreeRTOS.h"
#include "task.h"
#include "bsp_usart.h"
#include "bsp_esp8266.h"
#include "key.h"
#include "lcd.h"
#include "delay.h"
#include "semphr.h"
#include "led.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>
#include "MqttKit.h"
#include "Net_Status.h"
#include "upload_task.h"
#include "dht11.h"
#include  "beep.h"
extern void LCD_ShowChineseString16(u16 x, u16 y, const char *str, u16 color);
// ================================================================
// ===== 断线统计 =====
// ================================================================
uint32_t g_wifi_lost_count = 0;        // WiFi 断线次数
uint32_t g_mqtt_pingresp_timeout = 0;  // PINGRESP 超时次数
uint32_t g_mqtt_reconnect_count = 0;   // MQTT 重连次数

// ================================================================
// ===== 全局变量定义 =====
// ================================================================
SensorData_t g_sensorData = {0, 0, 0, 0};   // 传感器数据（温度、湿度、LED、蜂鸣器状态）
SemaphoreHandle_t g_dataMutex = NULL;        // 数据互斥锁，保护 g_sensorData

// ================================================================
// ===== 重连指数退避 =====
// ================================================================
uint32_t g_reconnect_delay = 5;   // 初始重连间隔 5 秒，失败后翻倍，最大 60 秒

// ================================================================
// ===== 时间间隔宏定义 =====
// ================================================================
#define WIFI_CHECK_INTERVAL_MS    30000   // WiFi 状态查询间隔：30 秒
#define PING_INTERVAL_MS          30000   // MQTT 心跳间隔：30 秒
#define PINGRESP_TIMEOUT_MS       90000   // PINGRESP 超时阈值：90 秒
#define SENSOR_INTERVAL_MS        5000    // 传感器采集间隔：5 秒
#define STATS_PRINT_INTERVAL_MS   60000   // 断线统计打印间隔：60 秒   
// ================================================================
// ===== 任务句柄 =====
// ================================================================
TaskHandle_t xWiFiTaskHandle = NULL;       // WiFi 任务句柄
TaskHandle_t xUploadTaskHandle = NULL;     // 上报任务句柄
TaskHandle_t xParserTaskHandle = NULL;     // MQTT 解析任务句柄
TaskHandle_t xMonitorTaskHandle = NULL;    // 监控任务句柄

// ================================================================
// ===== 心跳时间戳 =====
// ================================================================
TickType_t g_last_ping_resp_time;   // 最后一次收到 PINGRESP 的时间戳

// ================================================================
// ===== MQTT Topic 定义 =====
// ================================================================
#define TOPIC_PROPERTY_SET   "$sys/SQ8gfZ73EX/Test1/thing/property/set"   // 属性设置 Topic（订阅）
const char *Topic_property_set_reply = "$sys/SQ8gfZ73EX/Test1/thing/property/set_reply";   // 属性设置回复 Topic（发布）
void System_Init(void)
{
    delay_init();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    Usart_Init();
    ESP8266_Init();
    KEY_Init();
    LCD_Init();
    LED_Init();
    DHT11_Init();
    BEEP_Init();
}
// ================================================================
// wifi_manager.c 或 main.c 中
// ================================================================
/**
 * @brief FreeRTOS 栈溢出钩子函数
 * @param xTask 溢出任务句柄
 * @param pcTaskName 溢出任务名称
 * @note  当任何任务栈溢出时，FreeRTOS 会调用此函数
 */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    // ★ 关闭中断，防止其他任务继续运行
    taskDISABLE_INTERRUPTS();
    
    // ★ 打印溢出任务名（如果串口还能用）
    UsartPrintf(USART1, "\r\n[FATAL] Stack overflow in task: %s\r\n", pcTaskName);
    
    
    while (1) {
        BEEP=BEEP_ON;
        for (volatile uint32_t i = 0; i < 1000000; i++);  // 简单延时
        BEEP=BEEP_OFF;
    }
}
/**
 * @brief WiFi + MQTT 连接管理任务（状态机版）
 * 
 * @note 职责：
 *       1. 管理 WiFi 连接（连接、断线检测、重连）
 *       2. 管理 MQTT 连接（连接、订阅、心跳、断线重连）
 *       3. 维护全局网络状态 g_net_status
 *       4. 定期打印断线统计
 * 
 * @note 状态流转：
 *       DISCONNECTED → (WiFi连接成功) → CONNECTING
 *       CONNECTING   → (收到CONNACK)   → CONNECTED
 *       CONNECTED    → (WiFi断线/Ping失败/心跳超时) → DISCONNECTED / CONNECTING
 * 
 * @note 状态说明：
 *       NET_STATUS_DISCONNECTED：WiFi 未连接，尝试连接 WiFi
 *       NET_STATUS_CONNECTING：  WiFi 已连接，MQTT 连接中
 *       NET_STATUS_CONNECTED：   全部就绪，可收发数据
 * 
 * @note 重连策略：
 *       指数退避：5 → 10 → 20 → 40 → 60 秒（最大 60 秒）
 *       连接成功后重置为 5 秒
 */
void WiFi_Task(void *pvParameters)
{
    char ip[20];                    // IP 地址缓冲区（预留）
    uint32_t retry_count = 0;       // WiFi 连接重试次数
    UsartPrintf(USART1, "\r\n========================================\r\n");
    UsartPrintf(USART1, "[WiFi_Task] Started!\r\n");
    UsartPrintf(USART1, "SSID: %s\r\n", WIFI_SSID);
    UsartPrintf(USART1, "========================================\r\n"); 
    // ★ 初始状态：WiFi 未连接
    g_net_status = NET_STATUS_DISCONNECTED;
    
    while(1)
    {
        switch(g_net_status)
        {
            /* ============================================================
             * 状态1：DISCONNECTED — WiFi 未连接，尝试连接 WiFi
             * ============================================================ */
            case NET_STATUS_DISCONNECTED:
                UsartPrintf(USART1, "\r\n[WiFi] Connecting... (Attempt %d)\r\n", ++retry_count);
                
                // ★ 调用 ESP8266 连接 WiFi
                if (ESP8266_ConnectWiFi(WIFI_SSID, WIFI_PASSWORD)) {
                    retry_count = 0;
                    UsartPrintf(USART1, "[WiFi] Connected and got IP!\r\n");
                    // ★ WiFi 连接成功，切换到 CONNECTING（去连 MQTT）
                    g_net_status = NET_STATUS_CONNECTING;
                } else {
                    // ★ 连接失败，等 5 秒重试
                    UsartPrintf(USART1, "[WiFi] Connect failed! Retry after 5s\r\n");
                    vTaskDelay(pdMS_TO_TICKS(5000));
                }
                break;
            
            /* ============================================================
             * 状态2：CONNECTING — WiFi 已连接，MQTT 连接中
             * ============================================================ */
            case NET_STATUS_CONNECTING:
                UsartPrintf(USART1, "\r\n[MQTT] Connecting... (Count: %d)\r\n", ++g_mqtt_reconnect_count);
                if (!ESP8266_GetWiFiStatus()) {
                        // ★ WiFi 断线，触发重连
                        UsartPrintf(USART1, "\r\n[WiFi] Connection lost! (Count: %d)\r\n", ++g_wifi_lost_count);
                        g_net_status = NET_STATUS_DISCONNECTED;
                        subscribed = 0;
                        break;
                    }
                // ★ 只发送 MQTT CONNECT，不等待 CONNACK
                // CONNACK 由 ESP8266_MQTT_ParserTask 异步处理
                if (ESP8266_MQTT_Connect(ONENET_PRODID, ONENET_DEVNAME, ONENET_APIKEY)) {
                    UsartPrintf(USART1, "[MQTT] CONNECT sent, waiting CONNACK...\r\n");
                } else {
                    UsartPrintf(USART1, "[MQTT] Send CONNECT failed, retry 5s\r\n");
                }
                
                // ★ 指数退避：5 → 10 → 20 → 40 → 60（最大 60 秒）
                vTaskDelay(pdMS_TO_TICKS(g_reconnect_delay * 1000));
                g_reconnect_delay *= 2;
                if (g_reconnect_delay > 60) g_reconnect_delay = 60;
                break;
            
            /* ============================================================
             * 状态3：CONNECTED — 全部就绪，维持心跳 + 检测断线
             * ============================================================ */
            case NET_STATUS_CONNECTED:
                vTaskDelay(pdMS_TO_TICKS(2000));
                
                // ★ 检查 WiFi 状态（每 WIFI_CHECK_INTERVAL_MS 毫秒一次）
                static TickType_t last_wifi_check = 0;
                TickType_t now = xTaskGetTickCount();
                
                if (now - last_wifi_check > pdMS_TO_TICKS(WIFI_CHECK_INTERVAL_MS)) {
                    last_wifi_check = now;
                    
                    if (!ESP8266_GetWiFiStatus()) {
                        // ★ WiFi 断线，触发重连
                        UsartPrintf(USART1, "\r\n[WiFi] Connection lost! (Count: %d)\r\n", ++g_wifi_lost_count);
                        g_net_status = NET_STATUS_DISCONNECTED;
                        subscribed = 0;
                        break;
                    }
                }
                
                // ★ 检查 PINGRESP 超时（超过 PINGRESP_TIMEOUT_MS 毫秒没收到心跳回应）
                now = xTaskGetTickCount();
                if (now - g_last_ping_resp_time > pdMS_TO_TICKS(PINGRESP_TIMEOUT_MS)) {
                    UsartPrintf(USART1, "[MQTT] PINGRESP timeout! (Count: %d)\r\n", ++g_mqtt_pingresp_timeout);
                    g_net_status = NET_STATUS_CONNECTING;
                    subscribed = 0;  // ★ 超时时重置订阅标志
                    break;
                }
                
                // ★★★ 订阅主题（每次进入 CONNECTED 且未订阅时执行） ★★★
                if (!subscribed) {
                    UsartPrintf(USART1, "[MQTT] Subscribing...\r\n");
                    if (ESP8266_MQTT_Subscribe(TOPIC_PROPERTY_SET, 0)) {
                        UsartPrintf(USART1, "[MQTT] Subscribe OK\r\n");
                    } else {
                        UsartPrintf(USART1, "[MQTT] Subscribe FAILED, retry next loop\r\n");
                    }
                }
                
                // ★ 每 PING_INTERVAL_MS 毫秒发一次 MQTT 心跳
                static TickType_t last_ping_time = 0;
                now = xTaskGetTickCount();
                if (now - last_ping_time > pdMS_TO_TICKS(PING_INTERVAL_MS)) {
                    if (!ESP8266_MQTT_Ping()) {
                        UsartPrintf(USART1, "[MQTT] Ping send failed\r\n");
                        g_net_status = NET_STATUS_CONNECTING;
                        subscribed = 0;  // ★ Ping失败时重置订阅标志
                    } else {
                        last_ping_time = now;
                    }
                }
                
                // ★★★ 每 STATS_PRINT_INTERVAL_MS打印一次断线统计 ★★★
                static TickType_t last_stat_print = 0;
                now = xTaskGetTickCount();
                if (now - last_stat_print > pdMS_TO_TICKS(STATS_PRINT_INTERVAL_MS)) {
                    last_stat_print = now;
                    UsartPrintf(USART1, "\r\n========== Link Stats ==========\r\n");
                    UsartPrintf(USART1, "[Stats] WiFi lost: %d\r\n", g_wifi_lost_count);
                    UsartPrintf(USART1, "[Stats] PINGRESP timeout: %d\r\n", g_mqtt_pingresp_timeout);
                    UsartPrintf(USART1, "[Stats] MQTT reconnect: %d\r\n", g_mqtt_reconnect_count);
                    UsartPrintf(USART1, "================================\r\n");
                }
                break;
            
            /* ============================================================
             * 异常状态：复位到 DISCONNECTED
             * ============================================================ */
            default:
                UsartPrintf(USART1, "[WiFi] Unknown state, reset to DISCONNECTED\r\n");
                g_net_status = NET_STATUS_DISCONNECTED;
                break;
        }
    }
}
void Monitor_Task(void *pvParameters)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        // 打印各任务状态
        UsartPrintf(USART1, "\r\n[Monitor] Heap: %d bytes\r\n", xPortGetFreeHeapSize());
        UsartPrintf(USART1, "[Monitor] WiFi stack: %d\r\n", 
                    uxTaskGetStackHighWaterMark(xWiFiTaskHandle));
        UsartPrintf(USART1, "[Monitor] Upload stack: %d\r\n", 
                    uxTaskGetStackHighWaterMark(xUploadTaskHandle));
        UsartPrintf(USART1, "[Monitor] Parser stack: %d\r\n", 
                    uxTaskGetStackHighWaterMark(xParserTaskHandle));
    }
}

/**
 * @brief OneNET 下行指令处理函数
 * 
 * @param topic   消息主题（应为属性设置 Topic）
 * @param payload 消息内容（JSON 格式）
 * @param len     消息长度
 * 
 * @note 职责：
 *       1. 解析 OneNET 下发的 JSON 指令
 *       2. 控制硬件（LED、蜂鸣器）
 *       3. 更新全局状态 g_sensorData
 *       4. 回复云端确认
 * 
 * @note 支持的指令格式：
 *       {"id":"123","params":{"LED":true,"BEEP":false}}
 * 
 * @note 处理流程：
 *       1. 检查 Topic 是否为属性设置
 *       2. 解析 JSON
 *       3. 获取 params 对象
 *       4. 解析 LED 控制
 *       5. 解析 BEEP 控制
 *       6. 回复云端 {"id":"xxx","code":200,"msg":"success"}
 *       7. 释放 JSON 内存
 * 
 * @note 注意：
 *       - 回复失败不影响功能，不触发重连
 *       - 该函数由 ESP8266_MQTT_ParserTask 调用
 */
void OneNET_ProcessCommand(char *topic, char *payload, uint16_t len)
{
    cJSON *json = NULL;
    cJSON *params_json = NULL;
    cJSON *led_json = NULL;
    cJSON *beep_json = NULL;
    cJSON *id_json = NULL;
    char reply_buf[128];
    
    UsartPrintf(USART1, "\r\n[OneNET] ===== Process Command =====\r\n");
    UsartPrintf(USART1, "[OneNET] Topic: %s\r\n", topic);
    UsartPrintf(USART1, "[OneNET] Payload: %s\r\n", payload);
    
    // ★ 1. 检查 Topic 是否是属性设置
    if (strcmp(topic, TOPIC_PROPERTY_SET) != 0) {
        UsartPrintf(USART1, "[OneNET] Not property set topic, ignore\r\n");
        return;
    }
    
    // ★ 2. 解析 JSON
    json = cJSON_Parse(payload);
    if (json == NULL) {
        UsartPrintf(USART1, "[OneNET] JSON parse error!\r\n");
        return;
    }
    
    // ★ 3. 获取 params 对象
    params_json = cJSON_GetObjectItem(json, "params");
    if (params_json == NULL) {
        UsartPrintf(USART1, "[OneNET] No 'params' field\r\n");
        cJSON_Delete(json);
        return;
    }
    
    // ★ 4. 解析 LED 控制
    led_json = cJSON_GetObjectItem(params_json, "LED");
    if (led_json != NULL) {
        if (led_json->type == cJSON_True) {
            // LED 亮
            LED1 = LED_ON;
            // ★★★ 更新全局状态 ★★★
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensorData.led_flag = 1;
                xSemaphoreGive(g_dataMutex);
            }
            UsartPrintf(USART1, "[OneNET] LED ON\r\n");
        } else if (led_json->type == cJSON_False) {
            // LED 灭
            LED1 = LED_OFF;
             // ★★★ 更新全局状态 ★★★
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensorData.led_flag = 0;
                xSemaphoreGive(g_dataMutex);
            }
        
            UsartPrintf(USART1, "[OneNET] LED OFF\r\n");
        } else {
            UsartPrintf(USART1, "[OneNET] LED value is not boolean\r\n");
        }
    } else {
        UsartPrintf(USART1, "[OneNET] No 'LED' field\r\n");
    }
    // ★★★ 5. 解析 BEEP 控制（照写 LED） ★★★
    beep_json = cJSON_GetObjectItem(params_json, "Alarm");
    if (beep_json != NULL) {
        if (beep_json->type == cJSON_True) {
            // 蜂鸣器开
            BEEP = BEEP_ON;   // ★ 根据你的硬件修改
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensorData.alarm_flag = 1;
                xSemaphoreGive(g_dataMutex);
            }
            UsartPrintf(USART1, "[OneNET] BEEP ON\r\n");
        } else if (beep_json->type == cJSON_False) {
            // 蜂鸣器关
            BEEP = BEEP_OFF;
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensorData.alarm_flag = 0;
                xSemaphoreGive(g_dataMutex);
            }
            UsartPrintf(USART1, "[OneNET] BEEP OFF\r\n");
        } else {
            UsartPrintf(USART1, "[OneNET] BEEP value is not boolean\r\n");
        }
    } else {
        UsartPrintf(USART1, "[OneNET] No 'BEEP' field\r\n");
    }
    // ★ 6. 回复云端（可选）
    id_json = cJSON_GetObjectItem(json, "id");
    if (id_json != NULL && id_json->type == cJSON_String) {
        char *id = id_json->valuestring;
        UsartPrintf(USART1, "[OneNET] ID: %s\r\n", id);
        
        // 构建回复 JSON
        memset(reply_buf, 0, sizeof(reply_buf));
        sprintf(reply_buf, "{\"id\":\"%s\",\"code\":200,\"msg\":\"success\"}", id);
        
        // ★★★ 检查返回值 ★★★
        if (ESP8266_MQTT_Publish(Topic_property_set_reply, reply_buf, 0)) {
            UsartPrintf(USART1, "[OneNET] Reply OK\r\n");
        } else {
            UsartPrintf(USART1, "[OneNET] Reply FAILED\r\n");
            // 回复失败不影响功能，不触发重连
        }
    }
    
    // ★ 7. 释放 JSON 内存
    cJSON_Delete(json);
    
    UsartPrintf(USART1, "[OneNET] ===== Process Done =====\r\n");
}
/**
 * @brief MQTT 解析任务（处理 CONNACK / PUBLISH / PINGRESP）
 */
/**
 * @brief MQTT 解析任务（处理 CONNACK / PUBLISH / PINGRESP / SUBACK）
 * 
 * @param pvParameters 未使用
 * 
 * @note 职责：
 *       1. 从 MQTT 队列读取数据
 *       2. 解析 MQTT 包类型
 *       3. 根据包类型执行对应操作
 *       4. 维护心跳时间和连接状态
 * 
 * @note 处理的包类型：
 *       - CONNACK：连接确认 → 设置 CONNECTED 状态，重置退避时间
 *       - PUBLISH：云端下发指令 → 调用 OneNET_ProcessCommand
 *       - PINGRESP：心跳回应 → 刷新心跳时间，重置退避时间
 *       - SUBACK：订阅确认 → 设置 subscribed = 1
 * 
 * @note 数据流：
 *       ESP8266 → USART2 → IDLE中断 → MQTT队列 → ParserTask → 业务处理
 * 
 * @note 心跳维护：
 *       收到任何 MQTT 包都刷新 g_last_ping_resp_time
 *       防止因 PINGRESP 丢失导致误判断线
 */

void ESP8266_MQTT_ParserTask(void *pvParameters)
{
    ESP8266_Msg_t msg;                  // MQTT 消息结构体（从队列读取）
    uint8_t *cmd;                       // 指向 MQTT 包数据的指针
    uint16_t dataLen;                   // MQTT 包数据长度
    uint8_t packet_type;                // MQTT 包类型
    
    char payload[256];                  // 载荷缓冲区（预留）
    uint16_t payload_len;               // 载荷长度（预留）
    char *req_payload = NULL;           // 解析出的载荷指针
    char *cmdid_topic = NULL;           // 解析出的 Topic 指针
    
    unsigned short topic_len = 0;       // Topic 长度
    unsigned short req_len = 0;         // 载荷长度
    
    unsigned char type = 0;             // 包类型（预留）
    unsigned char qos = 0;              // QoS 等级
    static unsigned short pkt_id = 0;   // 包 ID
    
    UsartPrintf(USART1, "[MQTT_Parser] Started\r\n");
    
    // ★★★ 初始化最后心跳时间（防止刚上电就超时） ★★★
    g_last_ping_resp_time = xTaskGetTickCount();
    
    while(1)
    {
        // ★ 阻塞等待 MQTT 队列数据（不占用 CPU）
        if (ESP8266_GetMQTTMsg(&msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        
        // ★ 去掉 +IPD 前缀，提取实际 MQTT 数据
        char *ipd = strstr((char*)msg.data, "+IPD,");
        if (ipd != NULL) {
            // ★ 找到冒号，跳过 "+IPD,<len>:" 前缀
            char *colon = strchr(ipd, ':');
            if (colon != NULL) {
                cmd = (uint8_t*)(colon + 1);                    // 指向数据起始
                dataLen = msg.len - (cmd - msg.data);           // 计算数据长度
            } else {
                cmd = msg.data;
                dataLen = msg.len;
            }
        } else {
            // ★ 没有 +IPD 前缀，直接用原始数据
            cmd = msg.data;
            dataLen = msg.len;
        }
        
        // ★ 数据长度为 0，跳过
        if (dataLen == 0) continue;
        
        // ★ 识别 MQTT 包类型（高 4 位）
        packet_type = MQTT_UnPacketRecv(cmd);
        
        switch(packet_type)
        {
            /* ============================================================
             * CONNACK：连接确认
             * ============================================================ */
            case MQTT_PKT_CONNACK:
                if (MQTT_UnPacketConnectAck(cmd) == 0) {
                    // ★ 连接成功
                    UsartPrintf(USART1, "[MQTT] CONNACK OK\n");
                    // ★ 刷新心跳时间
                    g_last_ping_resp_time = xTaskGetTickCount();
                    // ★ 切换状态为 CONNECTED
                    g_net_status = NET_STATUS_CONNECTED;
                    // ★ 重置退避时间
                    g_reconnect_delay = 5;
                    // ★ 重置订阅标志（重连后需要重新订阅）
                    subscribed = 0;
                } else {
                    // ★ 连接被拒绝
                    UsartPrintf(USART1, "[MQTT] CONNACK REFUSED\n");
                    g_net_status = NET_STATUS_CONNECTING;
                }
                break;
                
            /* ============================================================
             * PUBLISH：云端下发指令
             * ============================================================ */
            case MQTT_PKT_PUBLISH:
                // ★★★ 收到 PUBLISH 也刷新心跳时间 ★★★
                g_last_ping_resp_time = xTaskGetTickCount();
                
                if (MQTT_UnPacketPublish(cmd, &cmdid_topic, &topic_len, &req_payload, &req_len, &qos, &pkt_id) == 0) {
                    // ★ 解析成功，打印 Topic 和载荷
                    UsartPrintf(USART1, "[MQTT] PUBLISH: %s\n", cmdid_topic);
                    UsartPrintf(USART1, "[MQTT] Payload: %s\n", req_payload);
                    
                    // ★★★ 调用业务处理函数 ★★★
                    OneNET_ProcessCommand(cmdid_topic, req_payload, req_len);
                    
                    // ★ 释放内存（MQTT_UnPacketPublish 动态分配了内存）
                    MQTT_FreeBuffer(cmdid_topic);
                    MQTT_FreeBuffer(req_payload);
                }
                break;
                
            /* ============================================================
             * PINGRESP：心跳回应
             * ============================================================ */
            case MQTT_PKT_PINGRESP:
                // ★★★ 收到心跳回应，刷新时间戳 ★★★
                g_last_ping_resp_time = xTaskGetTickCount();
                UsartPrintf(USART1, "[MQTT] PINGRESP received\n");
                // ★ 重置退避时间
                g_reconnect_delay = 5;
                // ★ 确保状态是 CONNECTED
                if (g_net_status != NET_STATUS_CONNECTED) {
                    g_net_status = NET_STATUS_CONNECTED;
                }
                break;
                
            /* ============================================================
             * SUBACK：订阅确认
             * ============================================================ */
            case MQTT_PKT_SUBACK:
                UsartPrintf(USART1, "[MQTT] SUBACK received\n");
                // ★★★ 订阅成功 ★★★
                subscribed = 1;
                // ★ 刷新心跳时间
                g_last_ping_resp_time = xTaskGetTickCount();
                // ★ 重置退避时间
                g_reconnect_delay = 5;
                // ★ 确保状态是 CONNECTED
                if (g_net_status != NET_STATUS_CONNECTED) {
                    g_net_status = NET_STATUS_CONNECTED;
                }
                break;
                
            /* ============================================================
             * 其他包类型：忽略
             * ============================================================ */
            default:
                break;
        }
    }
}
/**
 * @brief DHT11 温湿度传感器采集任务
 * 
 * @param pvParameters 未使用
 * 
 * @note 职责：
 *       1. 初始化 DHT11 传感器
 *       2. 每 SENSOR_INTERVAL_MS 毫秒采集一次温湿度
 *       3. 更新全局数据 g_sensorData
 * 
 * @note 采集流程：
 *       1. 调用 DHT11_Read_Data 读取温湿度
 *       2. 读取成功：更新 g_sensorData.temp_int 和 humi_int
 *       3. 读取失败：打印错误，等待下次采集
 * 
 * @note 数据保护：
 *       更新 g_sensorData 时使用互斥锁 g_dataMutex
 *       防止与 Upload_Task 竞争
 * 
 * @note 注意事项：
 *       - DHT11 最小采集周期 1 秒，这里用 5 秒
 *       - 读取时使用 TIM4 独立延时，不依赖 SysTick
 *       - 任务优先级设为 configMAX_PRIORITIES - 2（较高）
 */
void Sensor_Task(void *pvParameters)
{
    // ★ 温湿度变量（DHT11 返回整数）
    u8 temp, humi;
    
    // ★ 初始化 DHT11（配置 GPIO + 延时定时器）
    DHT11_Init();
    
    // ★ 等待 DHT11 上电稳定（2 秒）
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    UsartPrintf(USART1, "[Sensor] DHT11 Task started\r\n");
    
    while (1) {
        
        // ★★★ 读取 DHT11 温湿度 ★★★
        // 返回值：0 = 成功，1 = 失败
        u8 result = DHT11_Read_Data(&temp, &humi);
        
        if (result == 0) {
            // ★ 读取成功，打印温湿度
            UsartPrintf(USART1, "[Sensor] Temp=%d C, Hum=%d %%\r\n", temp, humi);
            
            // ★★★ 更新全局数据（加锁保护） ★★★
            if (xSemaphoreTake(g_dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                g_sensorData.temp_int = temp;                            // 温度
                g_sensorData.humi_int = humi;                            // 湿度
                g_sensorData.led_flag = (LED1 == LED_ON) ? 1 : 0;        // LED 状态
                g_sensorData.alarm_flag =(BEEP == BEEP_ON) ? 1: 0;       // 蜂鸣器状态
                xSemaphoreGive(g_dataMutex);
            }
        } else {
            // ★ 读取失败，打印错误
            UsartPrintf(USART1, "[Sensor] Read failed\r\n");
        }
        
        // ★ 等待 SENSOR_INTERVAL_MS 毫秒后再次采集
        vTaskDelay(pdMS_TO_TICKS(SENSOR_INTERVAL_MS));
    }
}
int main(void)
{
    System_Init();
    
    LCD_Clear(WHITE);
    LCD_ShowChineseString16(10, 10, "FreeRTOS温控", BLACK);
    LCD_ShowString(150, 10, 200, 24, 24, (u8*)"v1.0");
    LCD_ShowString(10, 50, 200, 16, 16, (u8*)"WiFi Connecting...");
     // ★ 创建互斥锁
    g_dataMutex = xSemaphoreCreateMutex();
    if (g_dataMutex == NULL) {
        while(1);
    }
    xTaskCreate(WiFi_Task, "WiFi", 1024, NULL, 1, &xWiFiTaskHandle);
    xTaskCreate(Upload_Task, "Upload", 2048, NULL, 1, &xUploadTaskHandle);
    xTaskCreate(ESP8266_MQTT_ParserTask, "Parser", 1024, NULL, 1, &xParserTaskHandle);
    xTaskCreate(Sensor_Task, "Sensor", 512, NULL, configMAX_PRIORITIES - 2, NULL);//优先级设置高点，防止打乱读取时序
    // xTaskCreate(Monitor_Task, "Monitor", 512, NULL, 0, NULL);  // 最低优先级
    vTaskStartScheduler();
    
    while(1);
}


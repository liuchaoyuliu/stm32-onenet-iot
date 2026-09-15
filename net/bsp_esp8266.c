/**
 * @file bsp_esp8266.c
 * @brief ESP8266 WiFi模块驱动实现
 * @note  基于DMA+定时器串口接收框架
 */

#include "bsp_esp8266.h"
#include "bsp_usart.h"
#include "delay.h"
#include <string.h>


/*==============================================================================
 * 1. 全局状态变量
 *============================================================================*/
MQTT_PACKET_STRUCTURE mqttPacket = {NULL, 0, 0, 0};
uint8 mqttPacket_data[512];
/**
 * @brief WiFi连接状态
 * @note  volatile修饰，供多任务访问
 */
// volatile uint8_t g_wifi_connected = 0;
// volatile uint8_t g_mqtt_connected = 0;  // ★★★ 新增




/*==============================================================================
 * 2. 初始化和状态
 *============================================================================*/

/**
 * @brief 获取ESP8266当前状态
 * @return ESP8266_Status_t 状态枚举值
 */
Net_Status_t ESP8266_GetStatus(void)
{
    return g_net_status;
}

/**
 * @brief 初始化ESP8266软件状态
 * @note  硬件初始化已在 Usart_Init() 中完成
 */
void ESP8266_Init(void)
{
    
    UsartPrintf(USART1, "[ESP8266] Init OK\r\n");
}


/*==============================================================================
 * 3. AT指令发送
 *============================================================================*/

/**
 * @brief 发送AT指令（不等待响应）
 * @param cmd 指令字符串（不含 \r\n）
 * @return 1=发送成功，0=发送失败
 * @note  指令会自动追加 \r\n
 */
uint8_t ESP8266_SendCmd(const char *cmd)
{
    if (cmd == NULL) return 0;
    
    /* 发送指令 + \r\n */
    ESP8266_Clear_AT();
    Usart_SendString(USART2, (unsigned char*)cmd, strlen(cmd));
    Usart_SendString(USART2, (unsigned char*)"\r\n", 2);
    
    
    
    return 1;
}

/**
 * @brief 从AT队列等待响应
 * @param expected 期望的响应字符串
 * @param timeout  超时时间（毫秒）
 * @return 1=收到期望响应，0=超时或收到错误
 * @note  不匹配的数据会放回队列头部，避免丢失
 */
uint8_t ESP8266_WaitResponse(const char *expected, uint16_t timeout_ms)
{
    ESP8266_Msg_t msg;
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    while (1) {
        // ★ 计算剩余时间
        TickType_t elapsed = xTaskGetTickCount() - start_time;
        if (elapsed >= timeout_ticks) {
            UsartPrintf(USART1, "[Resp] TIMEOUT (%d ms)\r\n", timeout_ms);
            ESP8266_Clear_AT();
            return 0;
        }
        
        TickType_t remaining = timeout_ticks - elapsed;
        // ★ 最多等待 100ms，但不超过剩余时间
        TickType_t wait_ticks = (remaining < pdMS_TO_TICKS(100)) ? remaining : pdMS_TO_TICKS(100);
        
        if (ESP8266_GetATMsg(&msg, wait_ticks) == pdTRUE) {
            UsartPrintf(USART1, "[RX] %s\r\n", msg.data);
            
            if (expected != NULL && strstr((char*)msg.data, expected) != NULL) {
                UsartPrintf(USART1, "[Resp] %s\r\n", expected);
                ESP8266_Clear_AT();
                return 1;
            }
            
            if (strstr((char*)msg.data, "ERROR") != NULL) {
                UsartPrintf(USART1, "[Resp] ERROR\r\n");
                ESP8266_Clear_AT();
                return 0;
            }
            if (strstr((char*)msg.data, "FAIL") != NULL) {
                UsartPrintf(USART1, "[Resp] FAIL\r\n");
                ESP8266_Clear_AT();
                return 0;
            }
        }
        // 如果没收到数据，继续循环（时间在每次循环开头计算）
    }
}

/**
 * @brief 发送AT指令并等待响应（组合函数）
 * @param cmd      指令字符串
 * @param expected 期望的响应字符串
 * @param timeout  超时时间（毫秒）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_SendCmdWait(const char *cmd, const char *expected, uint16_t timeout)
{
    ESP8266_SendCmd(cmd);
    return ESP8266_WaitResponse(expected, timeout);
}


/*==============================================================================
 * 4. WiFi功能
 *============================================================================*/

/**
 * @brief AT指令测试
 * @return 1=AT指令正常，0=ESP8266无响应
 * @note  发送 "AT" 等待 "OK"，重试3次
 */
uint8_t ESP8266_AT_Test(void)
{
    UsartPrintf(USART1, "\r\n========== AT Test ==========\r\n");
    
    for (int retry = 0; retry < ESP8266_RETRY_COUNT; retry++) {
        if (ESP8266_SendCmdWait("AT", "OK", 2000)) {
            UsartPrintf(USART1, "[AT] OK\r\n");
            
            return 1;
        }
        UsartPrintf(USART1, "[AT] Retry %d/%d...\r\n", retry + 1, ESP8266_RETRY_COUNT);
        delay_ms(1000);
    }
    
    UsartPrintf(USART1, "[AT] FAILED\r\n");
    //g_net_status = NET_STATUS_DISCONNECTED;
    subscribed = 0;  // ★ 断开时重置订阅标志
    return 0;
}

/**
 * @brief 获取ESP8266的IP地址
 * @param ip_buffer 存放IP地址的缓冲区（至少20字节）
 * @return 1=成功获取IP，0=获取失败
 * @note  发送 AT+CIFSR，解析响应中的 STAIP
 */
uint8_t ESP8266_GetIP(char *ip_buffer)
{
    char *start, *end;
    ESP8266_Msg_t msg;
    
    UsartPrintf(USART1, "\r\n========== Get IP ==========\r\n");
    ESP8266_SendCmd("AT+CIFSR");
    
    /* 等待响应，最多3秒 */
    for (int i = 0; i < 30; i++) {
        if (ESP8266_GetATMsg(&msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            /* 格式1：+CIFSR:STAIP,"192.168.1.100" */
            start = strstr((char*)msg.data, "STAIP,\"");
            if (start != NULL) {
                start += 7;                      /* 跳过 "STAIP,\"" */
                end = strchr(start, '\"');       /* 查找结束引号 */
                if (end != NULL) {
                    memcpy(ip_buffer, start, end - start);
                    ip_buffer[end - start] = '\0';
                    UsartPrintf(USART1, "[IP] %s\r\n", ip_buffer);
                    ESP8266_Clear_AT();
                    return 1;
                }
            }
            
            /* 格式2：+CIFSR:STAIP,192.168.1.100 */
            start = strstr((char*)msg.data, "STAIP,");
            if (start != NULL) {
                start += 5;                      /* 跳过 "STAIP," */
                end = strchr(start, '\r');       /* 查找换行结束 */
                if (end != NULL) {
                    memcpy(ip_buffer, start, end - start);
                    ip_buffer[end - start] = '\0';
                    UsartPrintf(USART1, "[IP] %s\r\n", ip_buffer);
                    ESP8266_Clear_AT();
                    return 1;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    UsartPrintf(USART1, "[IP] TIMEOUT\r\n");
    ESP8266_Clear_AT();
    return 0;
}

/**
 * @brief 连接WiFi（核心函数）
 * @param ssid     WiFi名称
 * @param password WiFi密码
 * @return 1=连接成功，0=连接失败
 * 
 * @note 执行流程：
 *       第1步：AT测试         → 检查ESP8266是否正常
 *       第2步：设置Station模式 → AT+CWMODE=1
 *       第3步：关闭回显       → ATE0
 *       第4步：连接WiFi       → AT+CWJAP="SSID","PWD"（重试3次）
 *       第5步：获取IP地址     → AT+CIFSR
 *       第6步：设置单连接模式 → AT+CIPMUX=0
 */
uint8_t ESP8266_ConnectWiFi(const char *ssid, const char *password)
{
    char cmd[128];
    char ip[20];
    
    UsartPrintf(USART1, "\r\n========================================\r\n");
    UsartPrintf(USART1, "========== WiFi Connect ==========\r\n");
    UsartPrintf(USART1, "========================================\r\n");
    UsartPrintf(USART1, "[SSID] %s\r\n", ssid);
    UsartPrintf(USART1, "[PWD]  %s\r\n", password);
     // ★★★ 先硬复位 ESP8266 ★★★
    ESP8266_HardReset();
     // ★★★ 清空启动日志残留 ★★★
    ESP8266_Clear_AT();
    UsartPrintf(USART1, "[Step 0] ESP8266 ready!\r\n");

    // ★★★ 额外等待 200ms，让系统完全稳定 ★★★
    delay_ms(200);
    ESP8266_Clear_AT();  // ★ 失败时清空
    /* ===== 第1步：AT测试 ===== */
    UsartPrintf(USART1, "\r\n[Step 1] AT Test...\r\n");
    if (!ESP8266_AT_Test()) {
        UsartPrintf(USART1, "[Error] AT test failed\r\n");
        return 0;
    }
    ESP8266_Clear_AT();
    /* ===== 第2步：设置WiFi模式为Station ===== */
    UsartPrintf(USART1, "\r\n[Step 2] Set Station Mode...\r\n");
    if (!ESP8266_SendCmdWait("AT+CWMODE=1", "OK", 2000)) {
        UsartPrintf(USART1, "[Error] Set mode failed\r\n");
        return 0;
    }
    ESP8266_Clear_AT();
    /* ===== 第3步：关闭回显 ===== */
    UsartPrintf(USART1, "\r\n[Step 3] Disable Echo...\r\n");
    ESP8266_SendCmdWait("ATE0", "OK", 1000);
    ESP8266_Clear_AT();

    /* ===== 第4步：连接WiFi（带重试） ===== */
    UsartPrintf(USART1, "\r\n[Step 4] Connecting WiFi...\r\n");
    
    int retry;
    for (retry = 0; retry < ESP8266_RETRY_COUNT; retry++) {
        sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", ssid, password);
        if (ESP8266_SendCmdWait(cmd, "WIFI GOT IP", ESP8266_TIMEOUT_WIFI)) {
            break;
        }
        UsartPrintf(USART1, "[Retry] %d/%d...\r\n", retry + 1, ESP8266_RETRY_COUNT);
        delay_ms(2000);
    }
    
    if (retry >= ESP8266_RETRY_COUNT) {
        UsartPrintf(USART1, "[Error] WiFi connect failed\r\n");
        g_net_status = NET_STATUS_DISCONNECTED;
        subscribed = 0;  // ★ 断开时重置订阅标志
        return 0;
    }
    
    UsartPrintf(USART1, "[WiFi] Connected!\r\n");
    
    // = 1;
    ESP8266_Clear_AT();    
    /* ===== 第5步：获取IP地址 ===== */
    UsartPrintf(USART1, "\r\n[Step 5] Getting IP...\r\n");
    delay_ms(500);  /* 等待IP分配 */
    if (ESP8266_GetIP(ip)) {
        UsartPrintf(USART1, "[IP] %s\r\n", ip);
    } else {
        UsartPrintf(USART1, "[IP] Not obtained\r\n");
    }
    ESP8266_Clear_AT();
    /* ===== 第6步：设置单连接模式（为后续TCP做准备） ===== */
    UsartPrintf(USART1, "\r\n[Step 6] Set Single Connection Mode...\r\n");
    ESP8266_SendCmdWait("AT+CIPMUX=0", "OK", 1000);
    ESP8266_Clear_AT();
    UsartPrintf(USART1, "\r\n========================================\r\n");
    UsartPrintf(USART1, "========== WiFi Connect SUCCESS ==========\r\n");
    UsartPrintf(USART1, "========================================\r\n");
    
    return 1;
}

/**
 * @brief 获取WiFi连接状态
 * @return 1=WiFi已连接，0=WiFi未连接
 * @note  发送 AT+CWJAP? 查询当前连接的WiFi
 */
uint8_t ESP8266_GetWiFiStatus(void)
{
    ESP8266_Msg_t msg;
    
    UsartPrintf(USART1, "\r\n========== Get WiFi Status ==========\r\n");
    
    /* 发送查询指令 */
    ESP8266_SendCmd("AT+CWJAP?");
    
    /* 等待响应，最多5秒 */
    for (int i = 0; i < 50; i++) {
        if (ESP8266_GetATMsg(&msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            
            /* 检查是否包含 +CWJAP: 表示已连接 */
            if (strstr((char*)msg.data, "+CWJAP:") != NULL) {
                UsartPrintf(USART1, "[WiFi] Status: Connected\r\n");
                ESP8266_Clear_AT();
                return 1;
            }
            
            /* 检查错误或未连接 */
            if (strstr((char*)msg.data, "ERROR") != NULL) {
                UsartPrintf(USART1, "[WiFi] Status: Disconnected\r\n");
                ESP8266_Clear_AT();
                return 0;
            }
            
            /* 检查是否没有连接任何AP */
            if (strstr((char*)msg.data, "No AP") != NULL) {
                UsartPrintf(USART1, "[WiFi] Status: No AP\r\n");
                ESP8266_Clear_AT();
                return 0;
            }
        }
        
    }
    
    UsartPrintf(USART1, "[WiFi] Status: TIMEOUT\r\n");
    return 0;
}
/*==============================================================================
 * 5. TCP 功能
 *============================================================================*/

/**
 * @brief 建立 TCP 连接
 * @param server 服务器地址
 * @param port   端口号
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_TCP_Connect(const char *server, const char *port)
{
    char cmd[128];
    
    UsartPrintf(USART1, "\r\n========== TCP Connect ==========\r\n");
    UsartPrintf(USART1, "Server: %s:%s\r\n", server, port);
    
    /* 先关闭可能存在的连接 */
    ESP8266_TCP_Close();
    
    /* 发送 TCP 连接指令 */
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"mqtts.heclouds.com\",1883");
    if (!ESP8266_SendCmdWait(cmd, "CONNECT", 10000)) {
        UsartPrintf(USART1, "[Error] TCP connect failed\r\n");
        return 0;
    }
    
    UsartPrintf(USART1, "[TCP] Connected!\r\n");
    return 1;
}

/**
 * @brief 通过 TCP 发送数据
 * @param data 数据指针
 * @param len  数据长度
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_TCP_Send(const char *data, uint16_t len)
{
    char cmd[32];
    int i;
    for(i = 0; i < 32; i++){
        cmd[i] = '\0';
    }
    if (len == 0) len = strlen(data);
    
    UsartPrintf(USART1, "[TCP] Send: %d bytes\r\n", len);
    
    /* 发送 AT+CIPSEND */
    sprintf(cmd, "AT+CIPSEND=%d", len);
    UsartPrintf(USART1, "%s\r\n", cmd);
    ESP8266_SendCmd(cmd);
    
    /* 等待 ">" 提示符 */
    if (!ESP8266_WaitResponse(">", 3000)) {
        UsartPrintf(USART1, "[Error] Send failed: no prompt\r\n");
        return 0;
    }
    
    /* 发送数据 */
    Usart_SendString(USART2, (unsigned char*)data, len);
    UsartPrintf(USART1, "[TCP] Send OK\r\n");
    delay_ms(50);

    return 1;
}

/**
 * @brief 关闭 TCP 连接
 * @return 1=成功
 */
uint8_t ESP8266_TCP_Close(void)
{
    ESP8266_SendCmd("AT+CIPCLOSE");
    ESP8266_WaitResponse("OK", 2000);
    return 1;
}


/*==============================================================================
 * 6. MQTT 功能（需包含 MqttKit.h）
 *============================================================================*/

#include "MqttKit.h"

static uint8_t mqtt_connected = 0;
static uint16_t mqtt_pkt_id = 1;

/**
 * @brief 获取下一个 mqttPacket ID
 */
static uint16_t MQTT_GetNextPktId(void)
{
    return mqtt_pkt_id++;
}


uint8_t ESP8266_MQTT_Connect(const char *prod_id, const char *dev_name, const char *api_key)
{
    
    uint8_t result;
    char client_id[64];
    
    UsartPrintf(USART1, "\r\n========== MQTT Connect ==========\r\n");
    
    sprintf(client_id, "%s", dev_name);
    UsartPrintf(USART1, "ClientID: %s\r\n", client_id);
    UsartPrintf(USART1, "Username: %s\r\n", prod_id);
    
    mqttPacket._data=mqttPacket_data;					
	mqttPacket._memFlag=MEM_FLAG_STATIC;
	mqttPacket._size=sizeof(mqttPacket_data);
    
    result = MQTT_PacketConnect(
        prod_id,        // username
        api_key,        // password
        dev_name,      // clientID
        ONENET_KEEPALIVE,            // KeepAlive
        ONENET_CLEAN_SESSION,              // Clean Session
        ONENET_WILL_QOS,
        ONENET_WILL_TOPIC,
        ONENET_WILL_MSG,
        ONENET_WILL_RETAIN,
        &mqttPacket
    );
    
    if (result != 0) {
        UsartPrintf(USART1, "[Error] PacketConnect failed: %d\r\n", result);
        return 0;
    }
    
    UsartPrintf(USART1, "[MQTT] mqttPacket Len: %d\r\n", mqttPacket._len);
    
    // ★ 建立 TCP 连接
    if (!ESP8266_TCP_Connect(ONENET_MQTT_SERVER, ONENET_MQTT_PORT)) {
        UsartPrintf(USART1, "[Error] TCP connect failed\r\n");
        MQTT_DeleteBuffer(&mqttPacket);
        return 0;
    }
    
    // ★ 发送 CONNECT 包
    if (!ESP8266_TCP_Send((const char*)mqttPacket._data, mqttPacket._len)) {
        UsartPrintf(USART1, "[Error] Send CONNECT failed\r\n");
        MQTT_DeleteBuffer(&mqttPacket);
        ESP8266_TCP_Close();
        return 0;
    }
    
    MQTT_DeleteBuffer(&mqttPacket);
    
    // ★★★ 删除 CONNACK 等待逻辑 ★★★
    // CONNACK 由 MQTT_ParserTask 异步处理
    
    UsartPrintf(USART1, "[MQTT] CONNECT sent, waiting for CONNACK in parser...\r\n");
    return 1;  // 只表示"发送成功"，不代表"连接成功"
}
/**
 * @brief MQTT 发布消息
 * @param topic   主题
 * @param payload 消息内容
 * @param qos     QoS等级（0或1）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Publish(const char *topic, const char *payload, uint8_t qos)
{
    //
    
    uint8_t result;
    
    if (g_net_status != NET_STATUS_CONNECTED) {
        UsartPrintf(USART1, "[Error] Not connected\r\n");
        subscribed = 0;  // ★ 断开时重置订阅标志
        return 0;
    }
    
   
    
    
    
    result = MQTT_PacketPublish(MQTT_PUBLISH_ID	, topic, payload, strlen(payload), MQTT_QOS_LEVEL0, 0, 1, &mqttPacket);
    if (result != 0) {
        UsartPrintf(USART1, "[Error] PacketPublish failed: %d\r\n", result);
        return 0;
    }
    
    UsartPrintf(USART1, "[MQTT] Publish: %s\r\n", topic);
    UsartPrintf(USART1, "[MQTT] Payload: %s\r\n", payload);
    
    if (!ESP8266_TCP_Send((const char*)mqttPacket._data, mqttPacket._len)) {
        UsartPrintf(USART1, "[Error] Send PUBLISH failed\r\n");
        MQTT_DeleteBuffer(&mqttPacket);
        return 0;
    }
    
    MQTT_DeleteBuffer(&mqttPacket);
    
    /* QoS=1 等待 PUBACK */
    if (qos == 1) {
        ESP8266_Msg_t msg;
        for (int i = 0; i < 20; i++) {
            if (ESP8266_GetMQTTMsg(&msg, pdMS_TO_TICKS(100)) == pdTRUE) {
                char *ipd_start = strstr((char*)msg.data, "+IPD,");
                uint8_t *pData = msg.data;
                if (ipd_start != NULL) {
                    char *colon = strchr(ipd_start, ':');
                    if (colon != NULL) {
                        pData = (uint8_t*)(colon + 1);
                    }
                }
                /* PUBACK 固定头部是 0x40 */
                if ((pData[0] & 0xF0) == 0x40) {
                    UsartPrintf(USART1, "[MQTT] PUBACK received\r\n");
                    break;
                }
            }
            delay_ms(10);
        }
    }
    
    return 1;
}

/**
 * @brief MQTT 订阅主题
 * @param topic 主题
 * @param qos   QoS等级（0或1）
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Subscribe(const char *topic, uint8_t qos)
{
   
    uint16_t pkt_id;
    const char *topics[1];
    uint8_t result;
    
    if (g_net_status != NET_STATUS_CONNECTED) {
        UsartPrintf(USART1, "[Error] Not connected\r\n");
        subscribed = 0;  // ★ 断开时重置订阅标志
        return 0;
    }
    
    
    
    pkt_id = MQTT_SUBSCRIBE_ID;  /* 固定订阅包ID */
    topics[0] = topic;
    
    result = MQTT_PacketSubscribe(pkt_id, qos, topics, 1, &mqttPacket);
    if (result != 0) {
        UsartPrintf(USART1, "[Error] PacketSubscribe failed: %d\r\n", result);
        return 0;
    }
    
    UsartPrintf(USART1, "[MQTT] Subscribe: %s\r\n", topic);
    
    if (!ESP8266_TCP_Send((const char*)mqttPacket._data, mqttPacket._len)) {
        UsartPrintf(USART1, "[Error] Send SUBSCRIBE failed\r\n");
        MQTT_DeleteBuffer(&mqttPacket);
        return 0;
    }
    
    MQTT_DeleteBuffer(&mqttPacket);
    return 1;
}

/**
 * @brief MQTT 心跳
 * @return 1=成功，0=失败
 */
uint8_t ESP8266_MQTT_Ping(void)
{
    //
    
    if (g_net_status != NET_STATUS_CONNECTED) {
        UsartPrintf(USART1, "[Error] Not connected\r\n");
        subscribed = 0;  // ★ 断开时重置订阅标志
        return 0;
    }
    
    
    
    if (MQTT_PacketPing(&mqttPacket) != 0) {
        return 0;
    }
    
    if (!ESP8266_TCP_Send((const char*)mqttPacket._data, mqttPacket._len)) {
        MQTT_DeleteBuffer(&mqttPacket);
        return 0;
    }
    
    MQTT_DeleteBuffer(&mqttPacket);
    return 1;
}
/**
 * @brief 清空 ESP8266 的 AT 和 MQTT 队列
 * @note  丢弃所有未处理的数据，用于超时恢复
 */
void ESP8266_Clear_All(void)
{
    ESP8266_Msg_t msg;
    int at_count = 0;
    int mqtt_count = 0;
    
    // ★ 清空 AT 队列（非阻塞读取，全部丢弃）
    while (ESP8266_GetATMsg(&msg, 0) == pdTRUE) {
        at_count++;
    }
    
    // ★ 清空 MQTT 队列
    while (ESP8266_GetMQTTMsg(&msg, 0) == pdTRUE) {
        mqtt_count++;
    }
    
    if (at_count > 0 || mqtt_count > 0) {
        UsartPrintf(USART1, "[ESP8266] Cleared: AT=%d, MQTT=%d\r\n", at_count, mqtt_count);
    }
}
void ESP8266_Clear_AT(void)
{
    ESP8266_Msg_t msg;
    int count = 0;
    
    // ★ 只清 AT 队列
    while (ESP8266_GetATMsg(&msg, 0) == pdTRUE) {
        count++;
    }
    
    if (count > 0) {
        UsartPrintf(USART1, "[ESP8266] Cleared: AT=%d\r\n", count);
    }
}
/**
 * @brief 退出 ESP8266 透传模式
 * @note  发送 +++ 退出，前后需要 1 秒静默
 */
void ESP8266_ExitTransparent(void)
{
    UsartPrintf(USART1, "[ESP8266] Exiting transparent mode...\r\n");
    
    // ★ 1. 等待 1 秒静默（确保没有数据在发送）
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // ★ 2. 发送 +++（不加 \r\n）
    Usart_SendString(USART2, (uint8_t*)"+++", 3);
    
    // ★ 3. 再等待 1 秒静默
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // ★ 4. 清空队列
    ESP8266_Clear_AT();
    
    // ★ 5. 发送 AT 测试是否退出成功
    ESP8266_SendCmd("AT");
    if (ESP8266_WaitResponse("OK", 2000)) {
        UsartPrintf(USART1, "[ESP8266] Exit transparent OK\r\n");
    } else {
        UsartPrintf(USART1, "[ESP8266] Exit transparent FAILED\r\n");
    }
}
void ESP8266_HardReset(void)
{
    
    
    // ★ 拉低 100ms
    GPIO_ResetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // ★ 拉高，等待 ESP8266 启动
    GPIO_SetBits(ESP8266_RST_PORT, ESP8266_RST_PIN);
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    UsartPrintf(USART1, "[ESP8266] Reset done\r\n");
}
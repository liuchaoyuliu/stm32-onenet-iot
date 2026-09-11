#ifndef _NET_STATUS_H
#define _NET_STATUS_H

typedef enum {
    NET_STATUS_DISCONNECTED = 0,   // WiFi未连接
    NET_STATUS_CONNECTING,          // WiFi已连，MQTT连接中/订阅中
    NET_STATUS_CONNECTED           // 全部就绪（WiFi+MQTT+订阅完成）
} Net_Status_t;

// ★ 全局网络状态（所有任务共享）
extern volatile Net_Status_t g_net_status ;
extern volatile int subscribed;  // ★ 是否已订阅主题（所有任务共享）

#endif

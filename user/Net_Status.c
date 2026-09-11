#include "net_status.h"

// ★★★ 唯一定义的地方 ★★★
volatile Net_Status_t g_net_status = NET_STATUS_DISCONNECTED;
volatile int subscribed=0;

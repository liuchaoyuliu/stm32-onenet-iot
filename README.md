# STM32 + ESP8266 + OneNET 物联网温湿度监控系统

## 项目简介

基于 STM32F103ZET6 + ESP8266 + DHT11 的物联网温湿度监控系统，支持：
- DHT11 温湿度采集
- WiFi 连接 + MQTT 通信
- OneNET 云平台数据上报
- 云端指令下发控制 LED / 蜂鸣器
- FreeRTOS 多任务管理

## 硬件

| 模块 | 型号 |
|------|------|
| 主控 | 正点原子精英板 STM32F103ZET6 |
| WiFi | ESP-01S (ESP8266) |
| 温湿度 | DHT11 |
| 调试 | ST-Link V2 |

## 软件架构

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Sensor_Task  │     │ WiFi_Task    │     │ ParserTask   │
│ 采集温湿度   │     │ 管理WiFi/MQTT│     │ 解析MQTT     │
└──────┬───────┘     └──────┬───────┘     └──────┬───────┘
       │                    │                    │
       ▼                    ▼                    ▼
┌─────────────────────────────────────────────────────────┐
│              g_sensorData (共享数据)                     │
│           Temp / Hum / Led / Alarm                       │
└─────────────────────────────────────────────────────────┘
       │                    │                    │
       ▼                    ▼                    ▼
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│ Upload_Task  │     │ OneNET_Process│    │ 云端下发指令 │
│ 上报数据     │     │ Command       │    │              │
└──────────────┘     └──────────────┘     └──────────────┘
```

## 功能列表

| 功能 | 状态 |
|------|------|
| WiFi 连接 | ? |
| MQTT 连接 OneNET | ? |
| 心跳保活 | ? |
| 订阅主题 | ? |
| DHT11 温湿度采集 | ? |
| 数据上报（5 秒/次） | ? |
| 云端指令下发 | ? |
| LED 控制 | ? |
| 蜂鸣器控制 | ? |
| 断线自动重连 | ? |
| 指数退避 | ? |
| 栈溢出检测 | ? |

## 关键配置

### OneNET 配置

```c
#define ONENET_PRODID   "your_product_id"
#define ONENET_DEVNAME  "your_device_name"
#define ONENET_APIKEY   "your_api_key"
```

### WiFi 配置

```c
#define WIFI_SSID       "your_wifi_ssid"
#define WIFI_PASSWORD   "your_wifi_password"
```

### 时间间隔

```c
#define WIFI_CHECK_INTERVAL_MS    30000   // WiFi 状态查询间隔：30 秒
#define PING_INTERVAL_MS          30000   // MQTT 心跳间隔：30 秒
#define PINGRESP_TIMEOUT_MS       90000   // PINGRESP 超时阈值：90 秒
#define SENSOR_INTERVAL_MS        5000    // 传感器采集间隔：5 秒
#define STATS_PRINT_INTERVAL_MS   60000   // 断线统计打印间隔：60 秒
```

## 编译与烧录

### 环境

- Keil MDK 5.x
- STM32F10x 标准库
- FreeRTOS
- cJSON
- MqttKit

### 编译

1. 打开 `basic_project.uvprojx`
2. 编译（F7）
3. 烧录（F8）

### 烧录配置

- **ST-Link V2**：SWD 模式
- **串口下载**：USART1，115200

## ESP8266 固件烧录

| 配置项 | 值 |
|--------|-----|
| 固件 | (1471)ESP8266-AT-1M.bin |
| SPI MODE | DOUT |
| Flash Size | 8Mbit |
| 烧录地址 | 0x00000 |
| 波特率 | 115200 |

## 调试经验

### 1. AT 指令发不出去

**原因**：`Usart_SendString` 中的地址检查误杀了 Flash 中的常量字符串 `"AT"`。

**解决**：去掉地址范围检查，只保留 `len` 和 `NULL` 检查。

### 2. DHT11 在 FreeRTOS 下读取失败

**原因**：正点原子的 `delay_us` 在 FreeRTOS 下用 `vTaskSuspendAll`，开销大，导致 DHT11 时序错乱。

**解决**：用 SysTick->VAL 独立实现 `DHT11_DelayUs`，只读 SysTick，不修改配置。

### 3. ESP8266 卡在透传模式

**原因**：`AT+CIPSEND` 后没发完整数据，ESP8266 卡在透传模式。

**解决**：发送 `+++` 退出透传，或重新烧录固件。

### 4. MQTT 频繁重连

**原因**：PINGRESP 误判、WiFi 查询太频繁、OneNET 限流。

**解决**：
- 收到 PUBLISH/PINGRESP/SUBACK 都刷新心跳时间
- WiFi 查询间隔改为 30 秒
- 上报间隔改为 10 秒
- 指数退避 5→10→20→40→60 秒

## 稳定性数据

| 时间 | WiFi lost | PINGRESP timeout | MQTT reconnect |
|------|-----------|------------------|----------------|
| 2.5 小时 | 1 | 0 | 3 |

**达到工业级设备稳定性要求。**

## 作者

- 刘朝宇
- 159708944@qq.com


#ifndef __UPLOAD_TASK_H
#define __UPLOAD_TASK_H

#include "FreeRTOS.h"
#include "task.h"

typedef struct {
    int temp_int;
    int humi_int;
    uint8_t led_flag;
    uint8_t alarm_flag;
} SensorData_t;
#define TOPIC_PROPERTY_POST   "$sys/SQ8gfZ73EX/Test1/thing/property/post"
#define UPLOAD_INTERVAL_MS    10000

void Upload_Task(void *pvParameters);

#endif
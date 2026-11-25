#pragma once
#include "FreeRTOS.h"
TaskHandle_t xTaskGetCurrentTaskHandle();
void vTaskDelay(TickType_t);
void xTaskNotifyGive(TaskHandle_t);
void vTaskSetTimeOutState(TimeOut_t *);
int xTaskCheckForTimeOut(TimeOut_t *, TickType_t *);
uint32_t ulTaskNotifyTake(int, TickType_t);

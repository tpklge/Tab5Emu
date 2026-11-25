#pragma once
#include "FreeRTOS.h"
struct TestStream;
using StreamBufferHandle_t = TestStream *;
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t);
size_t xStreamBufferReceive(StreamBufferHandle_t, void *, size_t, TickType_t);
int xStreamBufferReset(StreamBufferHandle_t);

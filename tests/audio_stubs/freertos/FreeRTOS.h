#pragma once
#include <cstddef>
#include <cstdint>
using TickType_t = uint32_t;
struct TimeOut_t { TickType_t last; };
using TaskHandle_t = void *;
inline constexpr int pdFALSE=0, pdTRUE=1;
#define pdMS_TO_TICKS(ms) (ms)

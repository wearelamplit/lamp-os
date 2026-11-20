#pragma once
#include "freertos/FreeRTOS.h"

using TaskHandle_t = void*;

namespace {
void* g_mock_current_task = reinterpret_cast<void*>(1);
}

inline TaskHandle_t xTaskGetCurrentTaskHandle() { return g_mock_current_task; }
inline void set_mock_current_task(void* t) { g_mock_current_task = t; }

// Called on each delay, so a test can act as another task while the caller
// waits.
inline void (*g_taskDelayHook)() = nullptr;
inline void vTaskDelay(uint32_t) {
  if (g_taskDelayHook) g_taskDelayHook();
}

#pragma once

// FreeRTOS mutex surface for classes that build both on-device and native.
// Native test build stubs it to no-ops: the single-thread test harness has
// no concurrent access; the mutex acts solely as a sequence point on hardware.

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#else
typedef void* SemaphoreHandle_t;
#define pdTRUE         1
#define portMAX_DELAY  0xFFFFFFFFu
inline int pdMS_TO_TICKS(unsigned ms) { return static_cast<int>(ms); }
inline SemaphoreHandle_t xSemaphoreCreateMutex() {
  // Return a non-null sentinel so callers' null-checks succeed.
  return reinterpret_cast<SemaphoreHandle_t>(0x1);
}
inline int xSemaphoreTake(SemaphoreHandle_t, int) { return pdTRUE; }
inline void xSemaphoreGive(SemaphoreHandle_t) {}
inline void vSemaphoreDelete(SemaphoreHandle_t) {}
#endif

#pragma once
// Minimal fake for native tests that compile code using a FreeRTOS mutex
// (config/disposition_store.cpp). The host runner is single-threaded, so
// takes always succeed and give is a no-op.
#include <cstdint>

constexpr uint32_t portMAX_DELAY = 0xFFFFFFFFu;
constexpr int pdTRUE = 1;

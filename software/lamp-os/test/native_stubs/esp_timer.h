#pragma once

#include <cstdint>

#include "Arduino.h"

// Whole millis() wraps the mock clock has run through. The 64-bit esp_timer
// uptime reads it as the high word above g_mock_millis, so a test sets both to
// land a full wrap later on a clock that does not wrap.
namespace {
uint32_t g_mock_millis_wraps = 0;
}

inline int64_t esp_timer_get_time() {
  return ((static_cast<int64_t>(g_mock_millis_wraps) << 32) + g_mock_millis) * 1000;
}

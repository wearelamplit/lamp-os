#pragma once

#include <cstdint>

// The mock esp_timer clock: microseconds since boot, set by each test.
inline int64_t g_mock_uptime_us = 0;

inline int64_t esp_timer_get_time() { return g_mock_uptime_us; }

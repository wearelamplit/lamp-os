#pragma once

#include <cstdint>

namespace {
uint32_t g_mock_millis = 0;
}

inline uint32_t millis() { return g_mock_millis; }
inline void set_mock_millis(uint32_t ms) { g_mock_millis = ms; }

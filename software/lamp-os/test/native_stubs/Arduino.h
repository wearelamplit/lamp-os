#pragma once

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {
uint32_t g_mock_millis = 0;
}

inline uint32_t millis() { return g_mock_millis; }
inline void set_mock_millis(uint32_t ms) { g_mock_millis = ms; }

// ArduinoJson 7 serializes/deserializes std::string natively; aliasing lets
// String-returning production code (config.cpp) link in the native test env.
using String = std::string;

// Minimal stand-in for the ESP32 Serial global; production log calls need a
// callable target to link natively. Prints to stdout, same as a real board's
// USB-CDC log would show on a monitor.
struct NativeSerialStub {
  void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
  }
  void println(const char* s = "") { printf("%s\n", s); }
};
inline NativeSerialStub Serial;

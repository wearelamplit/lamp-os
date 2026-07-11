// Minimal Arduino.h stub for native unit tests.
#pragma once
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using String = std::string;

inline unsigned long millis() { return 0; }

struct ArduinoSerial {
  void println(const char*) {}
  void printf(const char*, ...) {}
};
inline ArduinoSerial Serial;

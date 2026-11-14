#pragma once

#include <Arduino.h>

#include <new>

namespace lamp {

// Logs "[heap] bad_alloc caught in <stage>", at most once every 5 s. Takes
// no heap while `stage` stays under 30 characters. Header-only so the native
// tests that compile a caller's .cpp alone link without it.
inline void logBadAlloc(const char* stage) {
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (lastMs != 0 && now - lastMs < 5000) return;
  lastMs = now == 0 ? 1 : now;
  Serial.printf("[heap] bad_alloc caught in %s\n", stage);
}

// Runs `body`, catching a std::bad_alloc that escapes it, so a task drops
// that unit of work instead of aborting the lamp. False when it caught one.
// A lock `body` held when it threw stays held, so a path that allocates under
// a lock catches inside the lock itself.
template <class Body>
bool catchBadAlloc(const char* stage, Body&& body) {
  try {
    body();
    return true;
  } catch (const std::bad_alloc&) {
    logBadAlloc(stage);
    return false;
  }
}

}  // namespace lamp

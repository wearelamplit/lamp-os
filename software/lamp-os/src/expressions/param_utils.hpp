#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

#ifdef LAMP_DEBUG
#include <Arduino.h>
#endif

namespace lamp {

inline uint32_t getParam(const std::map<std::string, uint32_t>& params,
                         const char* key, uint32_t fallback) {
  auto it = params.find(key);
  return it != params.end() ? it->second : fallback;
}

// Authoritative min-gap clamp for a range pair. Raises hi to lo+minGap
// (capped at rangeMax) so a client that ignores the catalog's minGap can't
// commit a too-narrow spread. lo/hi and minGap share the same unit.
inline uint32_t clampRangeHiGap(uint32_t lo, uint32_t hi,
                                uint32_t minGap, uint32_t rangeMax) {
  const uint32_t floor = std::min(lo + minGap, rangeMax);
  return hi < floor ? floor : hi;
}

// Descriptor-declared keys only. applyDefaults folds every ParamSpec.key plus
// interval/duration write-back keys into the map before configureFromParameters
// runs, so a miss here is a schema bug, not a missing preset.
inline uint32_t getParam(const std::map<std::string, uint32_t>& params,
                         const char* key) {
  auto it = params.find(key);
  if (it != params.end()) return it->second;
#ifdef LAMP_DEBUG
  Serial.printf("[param] descriptor key %s absent after applyDefaults\n", key);
#endif
  return 0;
}

}  // namespace lamp

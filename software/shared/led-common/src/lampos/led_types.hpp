#pragma once

#include <cstdint>
#include <cstring>

// Included at global scope (never inside the namespace) so its declarations
// don't leak into lampos::led. Only the NEO_* macros are consumed, and the
// native test build has no such header.
#if __has_include(<Adafruit_NeoPixel.h>)
#include <Adafruit_NeoPixel.h>
#define LAMPOS_LED_HAS_NEOPIXEL 1
#endif

namespace lampos {
namespace led {

// FROZEN enum values, stored in NVS; do not reorder.
enum class ByteOrder : uint8_t { GRBW = 0, GRB = 1, BGR = 2 };

// Parses "GRBW", "GRB", or "BGR". Leaves `out` untouched and returns false
// for null or unrecognized input, so callers fall back to the strip's default.
inline bool byteOrderFromString(const char* s, ByteOrder& out) {
  if (s == nullptr) return false;
  if (std::strcmp(s, "GRBW") == 0) { out = ByteOrder::GRBW; return true; }
  if (std::strcmp(s, "GRB")  == 0) { out = ByteOrder::GRB;  return true; }
  if (std::strcmp(s, "BGR")  == 0) { out = ByteOrder::BGR;  return true; }
  return false;
}

inline const char* byteOrderToString(ByteOrder b) {
  switch (b) {
    case ByteOrder::GRBW: return "GRBW";
    case ByteOrder::GRB:  return "GRB";
    case ByteOrder::BGR:  return "BGR";
  }
  return "GRB";
}

// NEO_* value WITHOUT the KHZ term; callers add `+ NEO_KHZ800`.
// Absent in native test builds (no Adafruit header).
#ifdef LAMPOS_LED_HAS_NEOPIXEL
inline uint16_t neoPixelFormat(ByteOrder b) {
  switch (b) {
    case ByteOrder::GRBW: return NEO_GRBW;
    case ByteOrder::GRB:  return NEO_GRB;
    case ByteOrder::BGR:  return NEO_BGR;
  }
  return NEO_GRBW;
}
#endif

}  // namespace led
}  // namespace lampos

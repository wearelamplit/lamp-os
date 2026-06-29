#include "color.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace lamp {
// std::format pulled in tens of KB of <format> machinery
// (locale + facet code) and allocated a std::string per call. Every persist
// and serialize path that touches a color palette went through here. Replace
// with snprintf into a 10-byte stack buffer (9 chars + NUL), then construct
// the returned std::string from exactly 9 chars. Lowercase preserved to
// match the previous "{:02x}" output byte-for-byte — callers (config.cpp,
// expression_invocation.cpp, lamp.cpp) serialize this directly
// into JSON, and hexStringToColor accepts both cases on the way back in.
std::string colorToHexString(Color inColor) {
  char buf[10];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", inColor.r, inColor.g, inColor.b, inColor.w);
  return std::string(buf, 9);
};

namespace {
// Map a single ASCII hex char to its 0..15 value, or -1 if not a hex digit.
inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// Parse 2 hex chars at `p` into `out`. Returns false on any non-hex byte.
inline bool parseHexByte(const char* p, uint8_t& out) {
  int hi = hexNibble(p[0]);
  int lo = hexNibble(p[1]);
  if (hi < 0 || lo < 0) return false;
  out = static_cast<uint8_t>((hi << 4) | lo);
  return true;
}
}  // namespace

// This function used to call std::stoul on substrings of
// `inHexString`, which throws std::invalid_argument on non-hex chars. Under
// Arduino-ESP32's default -fno-exceptions that throw turns into abort(),
// and these payload bytes are attacker-reachable over BLE + ESP-NOW
// (shadeColors, baseColors, expressionOp.entry.colors,
// settings_blob.{base,shade}.colors). The validating 2-char-nibble parser
// below removes the std::stoul exception path entirely, and also eliminates
// the 4 substr heap allocations per call. On any invalid input (wrong
// length, missing '#', or any non-hex char) we return a default Color —
// the same fallback the old wrong-length branch already had.
//
// See also test/test_color/color.cpp which pins this contract end-to-end.
Color hexStringToColor(std::string inHexString) {
  Color output = Color();

  if (inHexString.size() != 9) return output;
  if (inHexString[0] != '#') return output;

  const char* s = inHexString.c_str();
  uint8_t r, g, b, w;
  if (!parseHexByte(s + 1, r)) return output;
  if (!parseHexByte(s + 3, g)) return output;
  if (!parseHexByte(s + 5, b)) return output;
  if (!parseHexByte(s + 7, w)) return output;

  output.r = r;
  output.g = g;
  output.b = b;
  output.w = w;
  return output;
};

uint32_t colorDistance(Color c1, Color c2) {
  return uint32_t(sqrtf(powf((c2.r - c1.r), 2) + powf((c2.g - c1.g), 2) + powf((c2.b - c1.b), 2) + powf(c2.w - c1.w, 2)));
}

Color colorFromHue(uint16_t hueDeg) {
  hueDeg %= 360;
  const uint8_t region = hueDeg / 60;              // 0..5 sextant
  const uint8_t rem = (hueDeg % 60) * 255 / 60;    // 0..254 rising within
  const uint8_t down = 255 - rem;                  // 255..1 falling
  switch (region) {
    case 0:  return Color(255, rem, 0, 0);   // red   → yellow
    case 1:  return Color(down, 255, 0, 0);  // yellow→ green
    case 2:  return Color(0, 255, rem, 0);   // green → cyan
    case 3:  return Color(0, down, 255, 0);  // cyan  → blue
    case 4:  return Color(rem, 0, 255, 0);   // blue  → magenta
    default: return Color(255, 0, down, 0);  // magenta→ red
  }
}

}  // namespace lamp

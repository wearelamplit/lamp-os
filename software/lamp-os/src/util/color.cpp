#include "color.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace lamp {
// snprintf into a 10-byte stack buffer (9 chars + NUL), then construct
// the returned std::string from exactly 9 chars. Lowercase hex: callers
// (config.cpp, expression_invocation.cpp, lamp.cpp) serialize this
// directly into JSON, and hexStringToColor accepts both cases on read-back.
std::string colorToHexString(Color inColor) {
  char buf[10];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", inColor.r, inColor.g, inColor.b, inColor.w);
  return std::string(buf, 9);
};

bool isValidColorHex(const char* s) {
  if (!s) return false;
  if (s[0] != '#') return false;
  for (int i = 1; i < 9; i++) {
    const char c = s[i];
    if (c == '\0') return false;
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                    (c >= 'a' && c <= 'f');
    if (!ok) return false;
  }
  return s[9] == '\0';
}

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

// These payload bytes are attacker-reachable over BLE + ESP-NOW
// (shadeColors, baseColors, expressionOp.entry.colors,
// settings_blob.{base,shade}.colors). The validating 2-char-nibble
// parser below avoids the std::stoul exception path (a throw becomes
// abort() under -fno-exceptions) and eliminates per-call heap
// allocations. On any invalid input (wrong length, missing '#', or any
// non-hex char) returns a default Color.
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

std::vector<Color> parseColorList(const std::string& csv) {
  std::vector<Color> out;
  size_t pos = 0;
  while (pos <= csv.size()) {
    size_t comma = csv.find(',', pos);
    size_t len   = (comma == std::string::npos ? csv.size() : comma) - pos;
    std::string token = csv.substr(pos, len);
    size_t start = token.find_first_not_of(' ');
    size_t end   = token.find_last_not_of(' ');
    if (start != std::string::npos)
      out.push_back(hexStringToColor(token.substr(start, end - start + 1)));
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return out;
}

std::string colorsToPackedHex(const std::vector<Color>& colors) {
  std::string out;
  out.reserve(colors.size() * 8);
  char buf[9];
  for (const Color& c : colors) {
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", c.r, c.g, c.b, c.w);
    out.append(buf, 8);
  }
  return out;
}

bool packedHexToColors(const char* s, std::vector<Color>& out) {
  out.clear();
  if (!s) return false;
  const size_t len = std::strlen(s);
  if (len % 8 != 0) return false;
  out.reserve(len / 8);
  for (size_t i = 0; i < len; i += 8) {
    uint8_t r, g, b, w;
    if (!parseHexByte(s + i, r) || !parseHexByte(s + i + 2, g) ||
        !parseHexByte(s + i + 4, b) || !parseHexByte(s + i + 6, w)) {
      out.clear();
      return false;
    }
    out.push_back(Color(r, g, b, w));
  }
  return true;
}

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

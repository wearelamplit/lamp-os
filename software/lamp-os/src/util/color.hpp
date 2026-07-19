#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lamp {
class Color {
 public:
  uint8_t r, g, b, w;
  constexpr Color() : r(0), g(0), b(0), w(0) {}
  constexpr Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW)
      : r(inR), g(inG), b(inB), w(inW) {}
  constexpr bool operator==(const Color &inColor) const {
    return r == inColor.r && g == inColor.g && b == inColor.b && w == inColor.w;
  }
};

/**
 * transform a color object to a hex string
 * eg: r:0xFF, g:0x23, b:0x42, w:0x12 -> "#FF234212"
 */
std::string colorToHexString(Color inColor);

/**
 * True iff `s` is exactly a 9-char "#RRGGBBWW" string with hex digits
 * (upper or lower case). Payload bytes are attacker-reachable over BLE + ESP-NOW;
 * validate before calling hexStringToColor.
 */
bool isValidColorHex(const char* s);

/**
 * transform a 32 bit color string prefixed with a # to a color object
 * eg: "#FF234212" -> r:0xFF, g:0x23, b:0x42, w:0x12
 */
Color hexStringToColor(std::string inHexString);

/**
 * Split csv on commas, parse each trimmed token as a #RRGGBBWW hex
 * color, and return the non-empty results.
 */
std::vector<Color> parseColorList(const std::string& csv);

/**
 * Serialize colors as packed lowercase hex: 8 chars per color
 * ("rrggbbww"), no '#', no separators. Mesh invocation wire form
 * (see docs/dev/networking.md, MSG_EVENT / MSG_COMMAND payload).
 */
std::string colorsToPackedHex(const std::vector<Color>& colors);

/**
 * Parse the packed form back into `out` (replaced, not appended).
 * Returns false, with `out` left empty, if the length is not a multiple
 * of 8 or any char is not a hex digit. Empty input parses to an empty list.
 */
bool packedHexToColors(const char* s, std::vector<Color>& out);

uint32_t colorDistance(Color c1, Color c2);

/**
 * vivid RGB color for a hue in degrees. Full saturation + value,
 * white channel 0. Used to give a fresh lamp a unique default color (the
 * caller supplies a random hue). hue is taken mod 360.
 */
Color colorFromHue(uint16_t hueDeg);
}  // namespace lamp

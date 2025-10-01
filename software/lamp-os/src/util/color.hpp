#ifndef LAMP_UTIL_COLOR_H
#define LAMP_UTIL_COLOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace lamp {
/**
 * @brief encapsulate color values as 4 bytes rgbw value
 */
class Color {
 public:
  uint8_t r, g, b, w;
  Color();
  Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW);
  bool operator==(const Color &inColor) const;
};

/**
 * @brief transform a color object to a hex string
 * eg: r:0xFF, g:0x23, b:0x42, w:0x12 -> "#FF234212"
 */
std::string colorToHexString(Color inColor);

/**
 * @brief transform a 32 bit color string prefixed with a # to a color object
 * eg: "#FF234212" -> r:0xFF, g:0x23, b:0x42, w:0x12
 */
Color hexStringToColor(std::string inHexString);

/**
 * @brief get the Euclidean distance between 2 colors as an integer
 */
uint32_t colorDistance(Color c1, Color c2);
}  // namespace lamp
#endif
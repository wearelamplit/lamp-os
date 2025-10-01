#include "./color.hpp"

#include <cmath>
#include <cstdint>
#include <format>
#include <string>

namespace lamp {
std::string colorToHexString(Color inColor) {
  return std::format("#{:02x}{:02x}{:02x}{:02x}", inColor.r, inColor.g, inColor.b, inColor.w);
};

Color hexStringToColor(std::string inHexString) {
  Color output = Color();

  if (!(inHexString.size() == 9)) {
    return output;
  }

  output.r = std::stoul(inHexString.substr(1, 2), nullptr, 16);
  output.g = std::stoul(inHexString.substr(3, 2), nullptr, 16);
  output.b = std::stoul(inHexString.substr(5, 2), nullptr, 16);
  output.w = std::stoul(inHexString.substr(7, 2), nullptr, 16);

  return output;
};

uint32_t colorDistance(Color c1, Color c2) {
  return uint32_t(sqrtf(powf((c2.r - c1.r), 2) + powf((c2.g - c1.g), 2) + powf((c2.b - c1.b), 2) + powf(c2.w - c1.w, 2)));
}

Color::Color() { r = g = b = w = 0; }

Color::Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW) : r(inR), g(inG), b(inB), w(inW) {};

bool Color::operator==(const Color &inColor) const {
  return (
      r == inColor.r &&
      g == inColor.g &&
      b == inColor.b &&
      w == inColor.w);
};

}  // namespace lamp

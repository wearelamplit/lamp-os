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

bool Color::operator==(const Color& inColor) const {
  return (
      r == inColor.r &&
      g == inColor.g &&
      b == inColor.b &&
      w == inColor.w);
};

float colorToHue(const Color& inColor) {
  float r = inColor.r / 255.0f;
  float g = inColor.g / 255.0f;
  float b = inColor.b / 255.0f;

  float cmax = fmaxf(r, fmaxf(g, b));
  float cmin = fminf(r, fminf(g, b));
  float delta = cmax - cmin;

  if (delta < 0.001f) return 0.0f;  // achromatic — no meaningful hue

  float hue;
  if (cmax == r)
    hue = 60.0f * fmodf((g - b) / delta, 6.0f);
  else if (cmax == g)
    hue = 60.0f * ((b - r) / delta + 2.0f);
  else
    hue = 60.0f * ((r - g) / delta + 4.0f);

  if (hue < 0.0f) hue += 360.0f;
  return hue;
}

Color hsvToColor(float hue, uint32_t wht) {
  float h = fmodf(hue, 360.0f);
  if (h < 0.0f) h += 360.0f;

  float c = 1.0f;
  float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));

  float r, g, b;
  if (h < 60) {
    r = c;
    g = x;
    b = 0;
  } else if (h < 120) {
    r = x;
    g = c;
    b = 0;
  } else if (h < 180) {
    r = 0;
    g = c;
    b = x;
  } else if (h < 240) {
    r = 0;
    g = x;
    b = c;
  } else if (h < 300) {
    r = x;
    g = 0;
    b = c;
  } else {
    r = c;
    g = 0;
    b = x;
  }

  // float wFactor = fmaxf(0.0f, 1.0f - (fmodf(hue, 360.0f) / 180.0f));
  // uint8_t w = (uint8_t)(wht * wFactor);

  return Color(
      (uint8_t)(r * 255),
      (uint8_t)(g * 255),
      (uint8_t)(b * 255),
      0);
}

}  // namespace lamp

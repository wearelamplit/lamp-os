#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <ranges>
#include <vector>

class Color {
 public:
  uint8_t r, g, b, w;
  Color() { r = g = b = w = 0; }
  Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW) {
    r = inR;
    g = inG;
    b = inB;
    w = inW;
  };
};

std::vector<uint16_t> linear = {
    0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100,
    1200, 1300, 1400, 1500, 1600, 1700, 1800, 1900, 2000, 2100, 2200, 2300,
    2400, 2500, 2600, 2700, 2800, 2899, 3000, 3100, 3200, 3300, 3400, 3500,
    3600, 3700, 3800, 3900, 4000, 4100, 4200, 4300, 4400, 4500, 4600, 4700,
    4800, 4900, 5000, 5100, 5200, 5300, 5400, 5500, 5600, 5699, 5799, 5900,
    6000, 6100, 6200, 6300, 6400, 6500, 6600, 6700, 6800, 6900, 7000, 7100,
    7200, 7300, 7400, 7500, 7600, 7700, 7800, 7900, 8000, 8100, 8200, 8300,
    8400, 8500, 8600, 8700, 8800, 8900, 9000, 9100, 9200, 9300, 9400, 9500,
    9600, 9700, 9800, 9900, 10000};

uint8_t easeLinear(uint8_t start, uint8_t end, uint32_t duration,
                   uint32_t currentStep) {
  uint16_t factor = linear[(currentStep * 100 / duration * 100) / 100];
  TEST_MESSAGE(std::format("{:d}", factor).c_str());

  return (((end - start) * factor) / 10000) + start;
};

Color fadeLinear(Color start, Color end, uint32_t steps, uint32_t currentStep) {
  return Color(
      (start.r == end.r) ? end.r
                         : easeLinear(start.r, end.r, steps, currentStep),
      (start.g == end.g) ? end.g
                         : easeLinear(start.g, end.g, steps, currentStep),
      (start.b == end.b) ? end.b
                         : easeLinear(start.b, end.b, steps, currentStep),
      (start.w == end.w) ? end.w
                         : easeLinear(start.w, end.w, steps, currentStep));
};

std::vector<Color> calculateGradient(Color inColorStart, Color inColorEnd,
                                     uint8_t inSteps) {
  std::vector<Color> output;

  for (int i = 0; i < inSteps; i++) {
    output.push_back(fadeLinear(inColorStart, inColorEnd, inSteps, i));
  }

  return output;
};

std::vector<Color> buildGradientWithStops(uint8_t inNumberPixels,
                                          std::vector<Color> inColorStops) {
  uint8_t numberColors = inColorStops.size();
  uint8_t i = 0;
  std::vector<Color> gradient;

  // input color stops are empty
  if (numberColors < 1) {
    return std::vector<Color>{inNumberPixels, Color()};
  }

  // single color - return a uniform pixel buffer
  if (numberColors == 1) {
    return std::vector<Color>{inNumberPixels, inColorStops[0]};
  }

  // two colors - return a single gradient
  if (numberColors == 2) {
    return calculateGradient(inColorStops[0], inColorStops[1], inNumberPixels);
  }

  // multiple colors - use integer math to calculate an even fit for all the
  // stops
  uint8_t steps = floor(inNumberPixels / (numberColors - 1));
  uint8_t remainder = inNumberPixels % (numberColors - 1);
  std::vector<uint8_t> breaks = std::vector<uint8_t>(numberColors - 1, steps);

  if (remainder != 0) {
    for (i = 0; i < numberColors - 1; i++) {
      breaks[i] = breaks[i] + 1;

      remainder--;

      if (remainder == 0) {
        break;
      }
    }
  }

  // with all the breakpoints identified, build the gradients
  std::vector<Color> buf;
  buf.reserve(inNumberPixels);
  for (i = 0; i < breaks.size(); i++) {
    gradient =
        calculateGradient(inColorStops[i], inColorStops[i + 1], breaks[i]);
    buf.insert(buf.end(), gradient.begin(), gradient.end());
  }

  return buf;
};

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

void test_gradient_empty() {
  std::vector<Color> colorList;

  auto result = buildGradientWithStops(40, colorList);

  for (int i = 0; i < result.size(); i++) {
    TEST_MESSAGE(std::format("#{:02x}{:02x}{:02x}{:02x}", result[i].r, result[i].g, result[i].b, result[i].w).c_str());
  }

  TEST_ASSERT(result[0].r == 0x00 && result[39].r == 0x00);
}

void test_gradient_single_color() {
  std::vector<Color> colorList = {Color(0x10, 0xFF, 0x00, 0xFF)};
  auto result = buildGradientWithStops(40, colorList);

  for (int i = 0; i < result.size(); i++) {
    TEST_MESSAGE(std::format("#{:02x}{:02x}{:02x}{:02x}", result[i].r, result[i].g, result[i].b, result[i].w).c_str());
  }

  TEST_ASSERT(result[0].r == result[39].r);
}

void test_gradient_two_color() {
  std::vector<Color> colorList = {
      Color(0x10, 0xFF, 0x00, 0x00),
      Color(0x60, 0xFF, 0x00, 0xFF)};
  auto result = buildGradientWithStops(40, colorList);

  for (int i = 0; i < result.size(); i++) {
    TEST_MESSAGE(std::format("#{:02x}{:02x}{:02x}{:02x}", result[i].r, result[i].g, result[i].b, result[i].w).c_str());
  }

  TEST_ASSERT(result[0].r == 0x10 && result[39].r == 0x5D);
}

void test_gradient_no_remainder() {
  std::vector<Color> colorList = {
      Color(0x10, 0xFF, 0x00, 0x00),
      Color(0x60, 0xFF, 0x00, 0xFF),
      Color(0x10, 0xFF, 0x00, 0xFF)};
  auto result = buildGradientWithStops(39, colorList);

  for (int i = 0; i < result.size(); i++) {
    TEST_MESSAGE(std::format("#{:02x}{:02x}{:02x}{:02x}", result[i].r, result[i].g, result[i].b, result[i].w).c_str());
  }

  TEST_ASSERT(result[0].r == 0x10 && result[38].r == 0x15);
}

void test_gradient_five_color() {
  std::vector<Color> colorList = {
      Color(0x10, 0xFF, 0x00, 0x00),
      Color(0x60, 0xFF, 0x00, 0xFF),
      Color(0x30, 0xFF, 0x00, 0x00),
      Color(0x90, 0xFF, 0x00, 0xFF),
      Color(0x10, 0xFF, 0x00, 0x00)};

  auto result = buildGradientWithStops(39, colorList);

  for (int i = 0; i < result.size(); i++) {
    TEST_MESSAGE(std::format("#{:02x}{:02x}{:02x}{:02x}", result[i].r, result[i].g, result[i].b, result[i].w).c_str());
  }

  TEST_ASSERT(result[0].r == 0x10 && result[38].r == 0x20);
}

int main(int argc, char **argv) {
  UNITY_BEGIN();

  RUN_TEST(test_gradient_empty);
  RUN_TEST(test_gradient_single_color);
  RUN_TEST(test_gradient_two_color);
  RUN_TEST(test_gradient_no_remainder);
  RUN_TEST(test_gradient_five_color);

  UNITY_END();
}
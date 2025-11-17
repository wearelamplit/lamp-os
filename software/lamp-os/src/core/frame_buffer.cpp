#include "./frame_buffer.hpp"

#include <Adafruit_NeoPixel.h>

#include <cstdint>
#include <vector>

#include "./util/color.hpp"

namespace lamp {
FrameBuffer::FrameBuffer() {};

void FrameBuffer::begin(std::vector<Color> inDefaultColors, uint8_t inPixelCount, Adafruit_NeoPixel* inDriver) {
  defaultColors = inDefaultColors;
  pixelCount = inPixelCount;
  buffer = std::vector<Color>(inPixelCount);
  driver = inDriver;
  driver->begin();
  driver->fill(0);
  driver->show();
};

void FrameBuffer::fill(Color inColor) {
  for (i = 0; i < pixelCount; i++) {
    buffer[i] = inColor;
  }
};

void FrameBuffer::flush() {
  if (buffer == previousBuffer && driver->getBrightness() == previousBrightness) {
    return;
  }

  for (i = 0; i < pixelCount; i++) {
    driver->setPixelColor(i, (uint32_t)((buffer[i].w << 24) | (buffer[i].r << 16) | (buffer[i].g << 8) | (buffer[i].b)));
  }

  if (driver->canShow()) {
    driver->show();
  }

  previousBuffer = buffer;
  previousBrightness = driver->getBrightness();
};
}  // namespace lamp
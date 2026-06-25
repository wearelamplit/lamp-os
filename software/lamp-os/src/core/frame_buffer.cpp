#include "frame_buffer.hpp"

#include <Adafruit_NeoPixel.h>

#include <cstdint>
#include <vector>

#include "util/color.hpp"

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

void FrameBuffer::flush() {
  if (buffer == previousBuffer && driver->getBrightness() == previousBrightness) {
    return;
  }

#ifdef LAMP_DEBUG
  // Lowest-level color probe for the 2026-06-13 "jacko renders pink
  // despite correct beginFade bytes" investigation. Prints pixel[0]
  // exactly as it's about to be handed to NeoPixel — anything that
  // mutated the buffer between configurator->beginFade() and here will
  // show up as a delta from the override-side log. driver pointer +
  // pixelCount discriminate base vs shade (different strip sizes /
  // different Adafruit_NeoPixel instances). Rate-limited to ~2 Hz per
  // strip so fade frames don't drown the serial.
  const uint32_t nowMs = millis();
  if (nowMs - lastFlushLogMs_ >= 500) {
    Serial.printf("[fb] flush driver=%p px=%u pixel0=(R=%u G=%u B=%u W=%u) bright=%u\n",
                  (void*)driver, (unsigned)pixelCount,
                  (unsigned)buffer[0].r, (unsigned)buffer[0].g,
                  (unsigned)buffer[0].b, (unsigned)buffer[0].w,
                  (unsigned)driver->getBrightness());
    lastFlushLogMs_ = nowMs;
  }
#endif

  for (size_t i = 0; i < pixelCount; i++) {
    driver->setPixelColor(i, (uint32_t)((buffer[i].w << 24) | (buffer[i].r << 16) | (buffer[i].g << 8) | (buffer[i].b)));
  }

  if (driver->canShow()) {
    driver->show();
  }

  previousBuffer = buffer;
  previousBrightness = driver->getBrightness();
};
}  // namespace lamp
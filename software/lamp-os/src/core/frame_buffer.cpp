#include "frame_buffer.hpp"

#include <Adafruit_NeoPixel.h>

#include <cstdint>
#include <vector>

#include "util/color.hpp"

namespace lamp {

FrameBuffer::FrameBuffer() {}

void FrameBuffer::begin(std::vector<Color> inDefaultColors, std::vector<StripSegment> inSegments) {
  defaultColors = std::move(inDefaultColors);
  segments = std::move(inSegments);
  uint16_t total = 0;
  for (const auto& seg : segments) {
    total += seg.pixelCount;
  }
  pixelCount = static_cast<uint8_t>(total);
  buffer = std::vector<Color>(pixelCount);
  for (auto& seg : segments) {
    seg.driver->begin();
    seg.driver->fill(0);
    seg.driver->show();
  }
}

void FrameBuffer::begin(std::vector<Color> inDefaultColors, uint8_t inPixelCount,
                        Adafruit_NeoPixel* inDriver) {
  begin(std::move(inDefaultColors), {{inDriver, "", 0, inPixelCount}});
}

void FrameBuffer::flush() {
  if (segments.empty()) return;
  // Brightness is uniform across segments; segments[0] is representative.
  if (buffer == previousBuffer &&
      segments[0].driver->getBrightness() == previousBrightness) {
    return;
  }
  bool allShown = true;
  for (const auto& seg : segments) {
    for (uint16_t i = 0; i < seg.pixelCount; i++) {
      const Color& c = buffer[seg.offset + (seg.reversed ? (seg.pixelCount - 1 - i) : i)];
      seg.driver->setPixelColor(
          i, (uint32_t)((Adafruit_NeoPixel::gamma8(c.w) << 24) |
                        (Adafruit_NeoPixel::gamma8(c.r) << 16) |
                        (Adafruit_NeoPixel::gamma8(c.g) << 8) |
                        Adafruit_NeoPixel::gamma8(c.b)));
    }
    if (seg.driver->canShow()) {
      seg.driver->show();
    } else {
      allShown = false;
    }
  }
  // A skipped segment retransmits on the next frame.
  if (allShown) {
    previousBuffer = buffer;
    previousBrightness = segments[0].driver->getBrightness();
  }
}

}  // namespace lamp

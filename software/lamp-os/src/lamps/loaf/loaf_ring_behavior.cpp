#include "lamps/loaf/loaf_ring_behavior.hpp"

#include <Arduino.h>

#include <cmath>

#include "config/config.hpp"
#include "core/frame_buffer.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"

extern lamp::Config config;

namespace lamp { namespace loaf {

namespace {
constexpr uint32_t kRingFrames = 600;   // loop length; never playOnce
}

LoafRingBehavior::LoafRingBehavior(FrameBuffer* fb, bool isBase, uint32_t revMs)
    : AnimatedBehavior(fb, kRingFrames, /*inAutoPlay=*/true),
      isBase_(isBase), revMs_(revMs) {}

void LoafRingBehavior::rebuild() {
  std::vector<Color> closed = stops_;
  if (closed.size() >= 2) closed.push_back(closed.front());   // close the loop
  ring_ = buildGradientWithStops(static_cast<uint8_t>(windowSize()), closed);
}

void LoafRingBehavior::control() {
  const std::vector<Color>& live =
      isBase_ ? config.base.broadcastColors() : config.shade.broadcastColors();
  if (live != stops_ || ring_.size() != windowSize()) {
    stops_ = live;
    rebuild();
  }
}

void LoafRingBehavior::draw() {
  if (!fb || ring_.empty()) { nextFrame(); return; }
  const uint16_t n = windowSize();
  const float phase =
      revMs_ ? std::fmod(static_cast<float>(millis()) / revMs_, 1.0f) * n : 0.0f;
  for (uint16_t i = 0; i < n && i < fb->buffer.size(); ++i) {
    const float src = static_cast<float>(i) + phase;
    const float fl = std::floor(src);
    const uint16_t lo = static_cast<uint16_t>(fl) % n;
    const uint16_t hi = (lo + 1) % n;
    fb->buffer[i] = mixColorWeight(ring_[lo], ring_[hi], src - fl);
  }
  nextFrame();
}

}}  // namespace loaf, namespace lamp

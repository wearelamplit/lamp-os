#include "lamps/loaf/loaf_ring_behavior.hpp"

#include <Arduino.h>

#include <cmath>
#include <vector>

#include "config/config.hpp"
#include "core/frame_buffer.hpp"
#include "util/fade.hpp"

extern lamp::Config config;

namespace lamp { namespace loaf {

namespace {
constexpr uint32_t kRingFrames = 600;   // loop length; never playOnce

// Interpolate between two hues the short way around the wheel at full
// saturation + value. Linear-RGB interpolation between dim stops produces
// low-value two-channel mixes that gamma then crushes to near-off; a
// full-value hue keeps every pixel bright so the ring stays contiguous.
Color hueLerp(uint16_t a, uint16_t b, float t) {
  const int diff = ((static_cast<int>(b) - static_cast<int>(a) + 540) % 360) - 180;
  float h = std::fmod(static_cast<float>(a) + diff * t + 360.0f, 360.0f);
  return colorFromHue(static_cast<uint16_t>(std::lround(h)) % 360);
}
}  // namespace

LoafRingBehavior::LoafRingBehavior(FrameBuffer* fb, bool isBase, uint32_t revMs)
    : AnimatedBehavior(fb, kRingFrames, /*inAutoPlay=*/true),
      isBase_(isBase), revMs_(revMs) {}

void LoafRingBehavior::rebuild() {
  ring_.clear();
  const uint16_t n = windowSize();
  const size_t k = stops_.size();
  if (k == 0 || n == 0) return;
  if (k == 1) {
    ring_.assign(n, colorFromHue(colorToHue(stops_[0])));
    return;
  }
  std::vector<uint16_t> hues(k);
  for (size_t i = 0; i < k; ++i) hues[i] = colorToHue(stops_[i]);
  ring_.resize(n);
  for (uint16_t i = 0; i < n; ++i) {
    const float pos = static_cast<float>(i) * k / n;   // [0, k), closed loop
    const size_t s = static_cast<size_t>(pos) % k;
    ring_[i] = hueLerp(hues[s], hues[(s + 1) % k], pos - std::floor(pos));
  }
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

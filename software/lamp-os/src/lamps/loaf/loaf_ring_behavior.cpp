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
constexpr float kRevRampMs = 400.0f;    // period ease window (companion speed-up)

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

LoafRingBehavior::LoafRingBehavior(FrameBuffer* fb, bool isBase, uint32_t revMs,
                                   uint32_t companionRevMs)
    : AnimatedBehavior(fb, kRingFrames, /*inAutoPlay=*/true),
      isBase_(isBase), baseRevMs_(revMs), companionRevMs_(companionRevMs),
      targetRevMs_(revMs), curRevMs_(static_cast<float>(revMs)) {}

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
  if (companionRevMs_ != 0 && context_) {
    bool loafNear = false;
    context_->forEachNearby([&](const PeerView& p) {
      if (p.variant == lamp_protocol::LampVariant::Loaf) { loafNear = true; return true; }
      return false;
    });
    setRevolutionMs(loafNear ? companionRevMs_ : baseRevMs_);
  }
}

void LoafRingBehavior::draw() {
  if (!fb || ring_.empty()) { nextFrame(); return; }
  const uint16_t n = windowSize();
  const uint32_t now = millis();
  const uint32_t dt = lastDrawMs_ ? (now - lastDrawMs_) : 0;
  lastDrawMs_ = now;
  if (dt) {
    float step = static_cast<float>(dt) / kRevRampMs;
    if (step > 1.0f) step = 1.0f;
    curRevMs_ += (static_cast<float>(targetRevMs_) - curRevMs_) * step;
    if (curRevMs_ >= 1.0f) {
      phase_ = std::fmod(phase_ + static_cast<float>(dt) / curRevMs_, 1.0f);
    }
  }
  const float phasePx = phase_ * n;
  for (uint16_t i = 0; i < n && i < fb->buffer.size(); ++i) {
    const float src = static_cast<float>(i) + phasePx;
    const float fl = std::floor(src);
    const uint16_t lo = static_cast<uint16_t>(fl) % n;
    const uint16_t hi = (lo + 1) % n;
    fb->buffer[i] = mixColorWeight(ring_[lo], ring_[hi], src - fl);
  }
  nextFrame();
}

}}  // namespace loaf, namespace lamp

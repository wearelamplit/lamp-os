#include "lamps/snafu/dots_behavior.hpp"

#include "util/fade.hpp"
#include "util/gradient.hpp"

namespace lamp {
bool isWispCurrentlyOverriding();  // defined in expressions/expression.cpp
}

namespace lamp { namespace snafu {

namespace {
constexpr uint32_t kSolidJitterBrightnessMinPct = 55;
constexpr uint32_t kSolidJitterBrightnessMaxPct = 100;
constexpr int32_t kSolidJitterSaturationPercentMax = 12;

// A solid borrowed color renders every dot identical. Scale brightness
// per-pixel and pull RGB slightly toward/away from their mean (a cheap
// saturation nudge) so borrowed peers still show dot-to-dot texture.
Color jitterSolidColor(Color c, FastRng& rng) {
  const uint32_t briPct = rng.range(kSolidJitterBrightnessMinPct, kSolidJitterBrightnessMaxPct);
  auto scaleBrightness = [&](uint8_t ch) {
    return static_cast<uint8_t>((static_cast<uint32_t>(ch) * briPct) / 100);
  };
  const uint8_t r = scaleBrightness(c.r);
  const uint8_t g = scaleBrightness(c.g);
  const uint8_t b = scaleBrightness(c.b);
  const uint8_t w = scaleBrightness(c.w);

  const int32_t avg = (static_cast<int32_t>(r) + g + b) / 3;
  const int32_t satPct = 100 + static_cast<int32_t>(
      rng.range(0, 2 * kSolidJitterSaturationPercentMax)) - kSolidJitterSaturationPercentMax;
  auto pullTowardMean = [&](uint8_t ch) {
    const int32_t v = avg + (static_cast<int32_t>(ch) - avg) * satPct / 100;
    return static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
  };
  return Color(pullTowardMean(r), pullTowardMean(g), pullTowardMean(b), w);
}
}  // namespace

void DotsBehavior::reshuffle() {
  perm_.assign(fb->segments.size(), {});
  for (size_t k = 0; k < fb->segments.size(); ++k) {
    const uint16_t n = fb->segments[k].pixelCount;
    std::vector<uint16_t>& p = perm_[k];
    p.resize(n);
    for (uint16_t i = 0; i < n; ++i) p[i] = i;
    for (size_t i = n; i > 1; --i) {
      const size_t j = rng_.range(0, static_cast<uint32_t>(i - 1));
      std::swap(p[i - 1], p[j]);
    }
  }
}

void DotsBehavior::ensurePerm() {
  if (perm_.size() != fb->segments.size()) { reshuffle(); return; }
  for (size_t k = 0; k < fb->segments.size(); ++k) {
    if (perm_[k].size() != fb->segments[k].pixelCount) { reshuffle(); return; }
  }
}

std::vector<Color> DotsBehavior::buildScene() { return buildSceneWith(borrowActive_); }

std::vector<Color> DotsBehavior::buildSceneWith(bool useBorrow) {
  ensurePerm();
  std::vector<Color> scene(fb->buffer.size());
  const auto& segs = cfg_->segments;
  for (size_t k = 0; k < fb->segments.size(); ++k) {
    const StripSegment& geo = fb->segments[k];
    // Segment 0 is Small Dots (snafu_lamp.hpp strip declaration order); it
    // borrows the peer's shade stops, the rest borrow the peer's base stops.
    const std::vector<Color>* borrowPal = (k == 0) ? &borrowShade_ : &borrowBase_;
    const bool borrowingSolid = useBorrow && borrowPal->size() == 1;
    const std::vector<Color>& palette =
        (useBorrow && !borrowPal->empty())
            ? *borrowPal
            : (k < segs.size() ? segs[k].colors : cfg_->broadcastColors());
    std::vector<Color> grad =
        buildGradientWithStops(static_cast<uint8_t>(geo.pixelCount), palette);
    if (borrowingSolid) {
      for (Color& px : grad) px = jitterSolidColor(px, rng_);
    }
    const std::vector<uint16_t>& p = perm_[k];
    for (size_t i = 0; i < grad.size() && geo.offset + i < scene.size(); ++i) {
      const size_t src = (i < p.size() && p[i] < grad.size()) ? p[i] : i;
      scene[geo.offset + i] = grad[src];
    }
  }
  return scene;
}

bool DotsBehavior::anyMultiColor() const {
  for (const auto& s : cfg_->segments) {
    if (s.colors.size() > 1) return true;
  }
  return false;
}

bool DotsBehavior::colorsChanged() {
  // Runs every frame; compare in place so the common no-change path allocates
  // nothing. Snapshot only when the palettes actually moved.
  bool same = lastColors_.size() == cfg_->segments.size();
  for (size_t k = 0; same && k < cfg_->segments.size(); ++k) {
    same = lastColors_[k] == cfg_->segments[k].colors;
  }
  if (same) return false;
  lastColors_.resize(cfg_->segments.size());
  for (size_t k = 0; k < cfg_->segments.size(); ++k) {
    lastColors_[k] = cfg_->segments[k].colors;
  }
  return true;
}

void DotsBehavior::borrowColors(const std::vector<Color>& baseStops,
                                const std::vector<Color>& shadeStops,
                                uint32_t durationFrames) {
  if (baseStops.empty() && shadeStops.empty()) return;
  borrowBase_     = baseStops;
  borrowShade_    = shadeStops;
  borrowActive_   = true;
  borrowElapsed_  = 0;
  borrowDuration_ = durationFrames;
  sinceShuffle_   = 0;
  reshuffle();
  prev_ = buildScene();
  cur_  = buildScene();
  ownMelt_ = buildSceneWith(false);
  frame = 0;
  sceneChange_ = false;
  frames = kBorrowSceneFrames;
}

void DotsBehavior::control() {
  if (!fb || fb->buffer.empty()) return;
  if (borrowActive_) {
    if (++borrowElapsed_ >= borrowDuration_) {
      borrowActive_ = false;
      borrowBase_.clear();
      borrowShade_.clear();
      sinceShuffle_ = 0;
      reshuffle();
      prev_ = buildScene();
      cur_  = buildScene();
      frame = 0;
      sceneChange_ = false;
      frames = kSceneFrames;
      return;
    }
    if (++sinceShuffle_ >= kBorrowHoldFrames) {
      sinceShuffle_ = 0;
      reshuffle();
      prev_ = cur_.empty() ? buildScene() : cur_;
      cur_  = buildScene();
      ownMelt_ = buildSceneWith(false);
      frame = 0;
      sceneChange_ = true;
    }
    return;
  }
  if (colorsChanged()) {
    // Live preview: recolor in place (same scatter) now, and defer the next
    // ambient re-scatter so editing never shuffles the dots.
    cur_ = buildScene();
    prev_ = cur_;
    sceneChange_ = false;
    frame = 0;
    sinceShuffle_ = 0;
    return;
  }
  if (sceneChange_) return;
  if (!anyMultiColor()) return;
  if (++sinceShuffle_ < kAmbientHoldFrames) return;  // hold before re-scattering
  sinceShuffle_ = 0;
  reshuffle();  // ambient cycle only: a color edit never reaches here
  prev_ = cur_.empty() ? buildScene() : cur_;
  cur_ = buildScene();
  frame = 0;
  sceneChange_ = true;
}

void DotsBehavior::draw() {
  if (!fb || fb->buffer.empty()) { nextFrame(); return; }
  if (isWispCurrentlyOverriding()) { nextFrame(); return; }
  if (cur_.empty()) { cur_ = buildScene(); prev_ = cur_; }
  if (sceneChange_) {
    for (size_t j = 0; j < fb->buffer.size(); ++j)
      fb->buffer[j] = fade(prev_[j], cur_[j], frames, frame);
    if (isLastFrame()) sceneChange_ = false;
  } else {
    for (size_t j = 0; j < fb->buffer.size(); ++j) fb->buffer[j] = cur_[j];
  }
  if (borrowActive_ && borrowElapsed_ * 2 > borrowDuration_ && !ownMelt_.empty()) {
    const uint32_t half = borrowDuration_ / 2;
    const uint32_t span = borrowDuration_ - half;
    const uint32_t pos  = borrowElapsed_ - half;
    for (size_t j = 0; j < fb->buffer.size() && j < ownMelt_.size(); ++j) {
      fb->buffer[j] = fade(fb->buffer[j], ownMelt_[j], span, pos);
    }
  }
  nextFrame();
}

}}  // namespace snafu, namespace lamp

#include "lamps/snafu/dots_behavior.hpp"

#include "core/behavior_context.hpp"
#include "core/compositor.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"


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

void DotsBehavior::buildScene(std::vector<Color>& scene) { buildSceneWith(scene, borrowActive_); }

void DotsBehavior::buildSceneWith(std::vector<Color>& scene, bool useBorrow) {
  ensurePerm();
  scene.assign(fb->buffer.size(), Color());
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
}

void DotsBehavior::rescatter() {
  if (cur_.empty()) {
    buildScene(prev_);
  } else {
    prev_.swap(cur_);
  }
  buildScene(cur_);
}

void DotsBehavior::releaseBorrow() {
  std::vector<Color>().swap(borrowBase_);
  std::vector<Color>().swap(borrowShade_);
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
  if (context_ && context_->ignoresPeers()) return;
  borrowBase_     = baseStops;
  borrowShade_    = shadeStops;
  borrowActive_   = true;
  drifting_       = false;
  borrowElapsed_  = 0;
  borrowDuration_ = durationFrames;
  sinceShuffle_   = 0;
  reshuffle();
  buildScene(prev_);
  buildScene(cur_);
  buildSceneWith(ownMelt_, false);
  frame = 0;
  sceneChange_ = false;
  frames = kBorrowSceneFrames;
}

void DotsBehavior::endDriftIfDone() {
  if (!drifting_ || sceneChange_) return;
  drifting_ = false;
  driftMeltSpan_ = 0;
  std::vector<Color>().swap(ownMelt_);
  frames = kSceneFrames;
}

void DotsBehavior::control() {
  if (!fb || fb->buffer.empty()) return;
  if (borrowActive_ && context_ && context_->ignoresPeers()) {
    // Drift from what draw() would paint next back to the own palette: the
    // scene (mid-crossfade included) crossfades to the own scene beneath the
    // wisp mix, and a melt already under way keeps overlaying above it,
    // easing out over the same frames.
    // prev_ becomes the frame on show, computed in place.
    if (cur_.empty()) buildScene(cur_);
    if (sceneChange_ && prev_.size() == cur_.size()) {
      for (size_t j = 0; j < cur_.size(); ++j) prev_[j] = fade(prev_[j], cur_[j], frames, frame);
    } else {
      prev_ = cur_;
    }
    drifting_ = true;
    driftMeltSpan_ = 0;
    if (borrowElapsed_ * 2 > borrowDuration_ && ownMelt_.size() == prev_.size()) {
      const uint32_t half = borrowDuration_ / 2;
      driftMeltSpan_ = borrowDuration_ - half;
      driftMeltPos_  = borrowElapsed_ - half;
    }
    borrowActive_ = false;
    releaseBorrow();
    sinceShuffle_ = 0;
    // A palette edited during the borrow lands in cur_ here; refresh the
    // snapshot so the next pass does not read the edit as new and snap.
    colorsChanged();
    buildScene(cur_);
    frame = 0;
    sceneChange_ = true;
    frames = kBorrowSceneFrames;
    return;
  }
  if (borrowActive_) {
    if (++borrowElapsed_ >= borrowDuration_) {
      borrowActive_ = false;
      releaseBorrow();
      std::vector<Color>().swap(ownMelt_);
      sinceShuffle_ = 0;
      reshuffle();
      buildScene(prev_);
      buildScene(cur_);
      frame = 0;
      sceneChange_ = false;
      frames = kSceneFrames;
      return;
    }
    if (++sinceShuffle_ >= kBorrowHoldFrames) {
      sinceShuffle_ = 0;
      reshuffle();
      rescatter();
      buildSceneWith(ownMelt_, false);
      frame = 0;
      sceneChange_ = true;
    }
    return;
  }
  if (colorsChanged()) {
    // Live preview: recolor in place (same scatter) now, and defer the next
    // ambient re-scatter so editing never shuffles the dots.
    buildScene(cur_);
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
  rescatter();
  frame = 0;
  sceneChange_ = true;
}

void DotsBehavior::draw() {
  if (!fb || fb->buffer.empty()) { nextFrame(); return; }
  if (cur_.empty()) { buildScene(cur_); prev_ = cur_; }
  endDriftIfDone();
  // Yield the shade to the wisp by its eased presence: at w==1 the dots hand
  // fully to the already-composited wisp, at w==0 they own the shade, and the
  // ~500ms ease crossfades both take and release (fb->buffer holds the eased
  // wisp composite from compositeWisp()).
  const float w = (context_ && context_->compositor)
                      ? context_->compositor->wispPresence(false)
                      : 0.0f;
  if (sceneChange_) {
    for (size_t j = 0; j < fb->buffer.size(); ++j)
      fb->buffer[j] = mixColorWeight(fade(prev_[j], cur_[j], frames, frame),
                                     fb->buffer[j], w);
    if (isLastFrame()) sceneChange_ = false;
  } else {
    for (size_t j = 0; j < fb->buffer.size(); ++j)
      fb->buffer[j] = mixColorWeight(cur_[j], fb->buffer[j], w);
  }
  // Greeting melt renders over the composited wisp, unlike the dots above; a
  // greeting stays visible over wisp paint.
  if (borrowActive_ && borrowElapsed_ * 2 > borrowDuration_ && !ownMelt_.empty()) {
    const uint32_t half = borrowDuration_ / 2;
    const uint32_t span = borrowDuration_ - half;
    const uint32_t pos  = borrowElapsed_ - half;
    for (size_t j = 0; j < fb->buffer.size() && j < ownMelt_.size(); ++j) {
      fb->buffer[j] = fade(fb->buffer[j], ownMelt_[j], span, pos);
    }
  } else if (drifting_ && driftMeltSpan_ && frame < frames) {
    const uint32_t pos = static_cast<uint32_t>(
        static_cast<uint64_t>(driftMeltPos_) * (frames - frame) / frames);
    for (size_t j = 0; j < fb->buffer.size() && j < ownMelt_.size(); ++j) {
      fb->buffer[j] = fade(fb->buffer[j], ownMelt_[j], driftMeltSpan_, pos);
    }
  }
  nextFrame();
}

}}  // namespace snafu, namespace lamp

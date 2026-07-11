#include "lamps/snafu/dots_behavior.hpp"

#include "util/fade.hpp"
#include "util/gradient.hpp"

namespace lamp {
bool isWispCurrentlyOverriding();  // defined in expressions/expression.cpp
}

namespace lamp { namespace snafu {

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

std::vector<Color> DotsBehavior::buildScene() {
  ensurePerm();
  std::vector<Color> scene(fb->buffer.size());
  const auto& segs = cfg_->segments;
  for (size_t k = 0; k < fb->segments.size(); ++k) {
    const StripSegment& geo = fb->segments[k];
    const std::vector<Color>& palette =
        k < segs.size() ? segs[k].colors : cfg_->broadcastColors();
    std::vector<Color> grad =
        buildGradientWithStops(static_cast<uint8_t>(geo.pixelCount), palette);
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

void DotsBehavior::control() {
  if (!fb || fb->buffer.empty()) return;
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
  nextFrame();
}

}}  // namespace snafu, namespace lamp

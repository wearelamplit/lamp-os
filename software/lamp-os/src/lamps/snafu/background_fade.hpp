#pragma once
#include "core/animated_behavior.hpp"
#include "util/color.hpp"
#include <vector>

namespace lamp { namespace snafu {

// Slow palette-cycle on the shade FrameBuffer. 12 gradient-pair scenes;
// picks one at random, fades over `frames` ticks, holds, picks another.
// Ported verbatim (palettes + visual contract) from legacy snafu.py
// BackgroundColorFade.
class BackgroundFade : public AnimatedBehavior {
 public:
  BackgroundFade(FrameBuffer* inFb, uint32_t inFrames = 2700)
    : AnimatedBehavior(inFb, inFrames, /*inAutoPlay=*/true) {}

  void draw() override;
  void control() override;

 private:
  static constexpr size_t kSceneCount = 12;
  // Verbatim port of legacy snafu.py::shade_colors. Each entry is a
  // gradient pair {start, end}. The cap visual cycles through these.
  static const Color kPalettes[kSceneCount][2];

  size_t currentScene_ = 0;
  bool sceneChange_ = false;
  // Cached per-scene gradient buffers (start/end interpolated across pixels).
  // Allocated once on first control() tick.
  std::vector<Color> previousScenePixels_;
  std::vector<Color> currentScenePixels_;

  std::vector<Color> buildScene(size_t scene, uint8_t pixelCount);
};

} }  // namespace snafu, namespace lamp

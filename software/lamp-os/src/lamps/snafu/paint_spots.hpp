#pragma once
#include "core/animated_behavior.hpp"
#include "util/color.hpp"
#include <vector>

namespace lamp { namespace snafu {

// Gradient palette cycle constrained to the spot pixel sub-region.
// Operates only on indices [startPx_, endPx_] (inclusive) within the shade
// FrameBuffer. Amanita hardware: startPx=24, endPx=32 (9 spots).
class PaintSpots : public AnimatedBehavior {
 public:
  PaintSpots(FrameBuffer* inFb, uint16_t startPx, uint16_t endPx,
                  uint32_t inFrames = 1440)
    : AnimatedBehavior(inFb, inFrames, /*inAutoPlay=*/true),
      startPx_(startPx), endPx_(endPx) {}

  void draw() override;
  void control() override;

 private:
  static constexpr size_t kSceneCount = 12;
  static const Color kSpotPalettes[kSceneCount][2];

  uint16_t startPx_;
  uint16_t endPx_;   // inclusive
  size_t currentScene_ = static_cast<size_t>(-1);
  bool sceneChange_ = false;
  // Gradient buffers sized to the spot count (endPx_ - startPx_ + 1).
  std::vector<Color> previousScenePixels_;
  std::vector<Color> currentScenePixels_;

  std::vector<Color> buildScene(size_t scene);
  uint8_t spotCount() const {
    if (endPx_ < startPx_) return 0;
    return static_cast<uint8_t>(endPx_ - startPx_ + 1);
  }
};

} }  // namespace snafu, namespace lamp

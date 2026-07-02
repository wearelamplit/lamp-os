#include "lamps/snafu/paint_spots.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/fast_rng.hpp"

namespace lamp::snafu {

const Color PaintSpots::kSpotPalettes[PaintSpots::kSceneCount][2] = {
  { Color(34,16,43,0),    Color(97,45,44,0)   },
  { Color(120,16,0,0),    Color(98,0,12,0)    },
  { Color(31,91,45,0),    Color(47,52,12,0)   },
  { Color(155,0,0,0),     Color(122,94,0,0)   },
  { Color(155,44,0,0),    Color(122,119,0,0)  },
  { Color(64,122,0,0),    Color(115,89,0,0)   },
  { Color(0,122,106,0),   Color(76,115,0,0)   },
  { Color(0,88,122,0),    Color(115,114,0,0)  },
  { Color(115,69,0,0),    Color(0,52,122,0)   },
  { Color(0,107,115,0),   Color(24,0,122,0)   },
  { Color(0,54,115,0),    Color(122,0,78,0)   },
  { Color(0,56,115,0),    Color(122,0,37,0)   },
};

namespace {
FastRng gSpotRng;
}  // namespace

std::vector<Color> PaintSpots::buildScene(size_t scene) {
  return calculateGradient(kSpotPalettes[scene][0], kSpotPalettes[scene][1],
                           spotCount());
}

void PaintSpots::draw() {
  if (!fb) { nextFrame(); return; }

  const uint8_t sc = spotCount();
  if (sc == 0 || fb->buffer.size() <= endPx_) { nextFrame(); return; }

  // Lazy-init on first draw: seed current scene pixels from scene 0 when
  // currentScene_ is still the sentinel.
  if (currentScenePixels_.empty()) {
    if (currentScene_ == static_cast<size_t>(-1)) {
      currentScene_ = 0;
    }
    currentScenePixels_  = buildScene(currentScene_);
    previousScenePixels_ = currentScenePixels_;
  }

  if (sceneChange_) {
    // Per-pixel fade across the spot region only.
    for (uint8_t j = 0; j < sc; ++j) {
      fb->buffer[startPx_ + j] = fade(previousScenePixels_[j],
                                      currentScenePixels_[j],
                                      frames, frame);
    }
    if (isLastFrame()) {
      sceneChange_ = false;
    }
  } else {
    // Hold: paint current spot colors directly.
    for (uint8_t j = 0; j < sc; ++j) {
      fb->buffer[startPx_ + j] = currentScenePixels_[j];
    }
  }

  nextFrame();
}

void PaintSpots::control() {
  if (!fb || sceneChange_) return;

  const uint8_t sc = spotCount();
  if (sc == 0) return;

  // Pick a random scene.
  size_t newScene = static_cast<size_t>(gSpotRng.range(0, kSceneCount - 1));
  if (newScene == currentScene_) {
    newScene = (newScene + 1) % kSceneCount;
  }

  // On very first control() call (currentScene_ == sentinel), skip preserving
  // previous — just set current.
  if (currentScene_ == static_cast<size_t>(-1)) {
    currentScenePixels_  = buildScene(newScene);
    previousScenePixels_ = currentScenePixels_;
  } else {
    previousScenePixels_ = buildScene(currentScene_);
    currentScenePixels_  = buildScene(newScene);
  }

  currentScene_ = newScene;
  frame         = 0;
  sceneChange_  = true;
}

}  // namespace lamp::snafu

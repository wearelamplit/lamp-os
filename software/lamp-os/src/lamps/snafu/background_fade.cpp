#include "lamps/snafu/background_fade.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/fast_rng.hpp"

namespace lamp::snafu {

const Color BackgroundFade::kPalettes[BackgroundFade::kSceneCount][2] = {
  { Color(5,100,15,0),   Color(70,50,2,0)    },
  { Color(50,0,43,0),    Color(120,25,15,0)  },
  { Color(120,16,0,0),   Color(98,0,12,0)    },
  { Color(9,9,100,0),    Color(0,80,50,0)    },
  { Color(20,100,1,0),   Color(150,45,0,0)   },
  { Color(140,30,20,0),  Color(125,10,40,0)  },
  { Color(30,4,120,0),   Color(125,20,0,0)   },
  { Color(9,125,4,0),    Color(0,60,120,0)   },
  { Color(4,50,110,0),   Color(0,38,120,0)   },
  { Color(140,4,40,0),   Color(120,0,50,0)   },
  { Color(133,24,0,0),   Color(150,50,0,0)   },
  { Color(0,80,120,0),   Color(0,73,25,0)    },
};

namespace {
FastRng gBgRng;
}  // namespace

std::vector<Color> BackgroundFade::buildScene(size_t scene, uint8_t pixelCount) {
  return calculateGradient(kPalettes[scene][0], kPalettes[scene][1], pixelCount);
}

void BackgroundFade::draw() {
  if (!fb) { nextFrame(); return; }

  const auto pixelCount = static_cast<uint8_t>(fb->buffer.size());
  if (pixelCount == 0) { nextFrame(); return; }

  // Lazy-init scene buffers on first draw
  if (currentScenePixels_.empty()) {
    currentScenePixels_ = buildScene(currentScene_, pixelCount);
    previousScenePixels_ = currentScenePixels_;
  }

  if (sceneChange_) {
    // Per-pixel fade from previous scene to current scene over `frames` ticks.
    for (size_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fade(previousScenePixels_[i], currentScenePixels_[i],
                           frames, frame);
    }
    if (isLastFrame()) {
      sceneChange_ = false;
    }
  } else {
    // Hold: paint current scene directly.
    for (size_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = currentScenePixels_[i];
    }
  }

  nextFrame();
}

void BackgroundFade::control() {
  if (!fb || sceneChange_) return;

  const auto pixelCount = static_cast<uint8_t>(fb->buffer.size());
  if (pixelCount == 0) return;

  // Pick a random scene different from the current one.
  size_t newScene = static_cast<size_t>(gBgRng.range(0, kSceneCount - 1));
  if (newScene == currentScene_) {
    newScene = (newScene + 1) % kSceneCount;
  }

  previousScenePixels_ = buildScene(currentScene_, pixelCount);
  currentScenePixels_  = buildScene(newScene, pixelCount);
  currentScene_        = newScene;
  frame                = 0;
  sceneChange_         = true;
}

}  // namespace lamp::snafu

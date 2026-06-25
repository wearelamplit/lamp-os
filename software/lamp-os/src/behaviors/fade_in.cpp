#include "fade_in.hpp"

#include "util/color.hpp"
#include "util/fade.hpp"

namespace lamp {
void FadeInBehavior::draw() {
  // fade() divides by `duration`. frames==0 would UB; frames==1 means a
  // single-frame snap-in (duration==0 hits fade()'s start==end early
  // return). Clamp to a 1-frame floor so callers that misconfigure us
  // produce a snap rather than a crash.
  const uint32_t duration = frames > 1 ? frames - 1 : 1;
  for (int i = 0; i < fb->pixelCount; i++) {
    fb->buffer[i] = fade(Color(0, 0, 0, 0), fb->defaultColors[i], duration, frame);
  }

  nextFrame();
};

void FadeInBehavior::control() {
  if (animationState == STOPPED && currentLoop == 0) {
    playOnce();
  }
};
}  // namespace lamp
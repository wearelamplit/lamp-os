#include "lamps/snafu/greeting.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/fast_rng.hpp"
#include <Arduino.h>

namespace lamp::snafu {

namespace {
FastRng gGreetRng;
}  // namespace

void Greeting::control() {
  if (!context_ || !context_->nearbyLamps) {
    lastTickMs_ = millis();
    return;
  }

  // Skip if already playing a greeting
  if (animationState == PLAYING || animationState == PLAYING_ONCE ||
      animationState == STOPPING) {
    lastTickMs_ = millis();
    return;
  }

  const auto peers = context_->nearbyLamps->getReachableViaBle(/*maxAgeMs=*/5000);
  for (const auto& p : peers) {
    if (p.bdAddr.empty()) continue;
    // Edge detect: firstSeenMs >= lastTickMs_ means this peer was first seen
    // since our last check — they just arrived.
    if (p.firstSeenMs >= lastTickMs_ && p.bdAddr != lastGreetedBdAddr_) {
      arrivedColor_      = p.baseColor;
      lastGreetedBdAddr_ = p.bdAddr;
      glitchColors_.clear();  // will rebuild in draw()
      glitchOffset_ = 0;
      frame = 0;
      playOnce();
      break;
    }
  }

  lastTickMs_ = millis();
}

void Greeting::draw() {
  if (!fb) { nextFrame(); return; }

  const auto pixelCount = static_cast<uint8_t>(fb->buffer.size());
  if (pixelCount == 0) { nextFrame(); return; }

  // Lazy-build glitch gradient: (0,45,200,0) → (180,0,60,0).
  if (glitchColors_.empty()) {
    glitchColors_ = calculateGradient(Color(0,45,200,0), Color(180,0,60,0),
                                      pixelCount);
  }

  const uint32_t f = frame;

  if (f < kGlitchFrames) {
    // Glitch phase: rotate the precomputed gradient by a random offset per frame.
    glitchOffset_ = gGreetRng.range(0, pixelCount - 1);
    for (uint8_t i = 0; i < pixelCount; ++i) {
      const uint8_t src = static_cast<uint8_t>((i + glitchOffset_) % pixelCount);
      fb->buffer[i] = glitchColors_[src];
    }
  } else if (f < kFadeInFrames) {
    // Ease-in: fade each pixel toward arrivedColor_ over kEaseFrames.
    const uint32_t phase = f - kGlitchFrames;
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fadeLinear(fb->buffer[i], arrivedColor_,
                                 kEaseFrames, phase);
    }
  } else if (f > frames - kEaseFrames) {
    // Fade-out: ease arrivedColor_ back toward black over the last kEaseFrames.
    const uint32_t phase = f - (frames - kEaseFrames);
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fadeLinear(arrivedColor_, Color(0,0,0,0),
                                 kEaseFrames, phase);
    }
  } else {
    // Hold: fill with arrived peer's base color.
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = arrivedColor_;
    }
  }

  nextFrame();
}

}  // namespace lamp::snafu

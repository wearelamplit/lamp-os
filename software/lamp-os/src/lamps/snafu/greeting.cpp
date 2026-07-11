#include "lamps/snafu/greeting.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/fast_rng.hpp"
#include "expressions/expression.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "config/config.hpp"
#include <Arduino.h>
#include <algorithm>
#include <cstring>

extern lamp::ExpressionManager expressionManager;
extern lamp::Config config;

namespace lamp::snafu {

namespace {
FastRng gGreetRng;
}  // namespace

lamp::GreetingState Greeting::greetingState() const {
  const bool playing = (animationState == PLAYING ||
                        animationState == PLAYING_ONCE ||
                        animationState == STOPPING);
  if (!playing) return {};
  lamp::GreetingState gs;
  gs.active     = true;
  gs.peerBdAddr = greetedBdAddr_;
  gs.kind       = "glitch";
  return gs;
}

void Greeting::doGreet(const lamp::NearbyLamp& peer) {
  arrivedColor_    = peer.baseColor;
  std::memcpy(greetedMac_, peer.mac, 6);
  greetedHasMac_   = peer.hasMac;
  greetedBdAddr_   = peer.bdAddr;
  greetStartMs_    = millis();
  stage2Done_      = false;
  stage3Done_      = false;
  glitchColors_.clear();
  glitchOffset_    = 0;
  frame            = 0;
  playOnce();
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
}

void Greeting::triggerGreeting(const lamp::NearbyLamp& peer) {
  doGreet(peer);
}

void Greeting::tickStages() {
  const uint32_t elapsed = millis() - greetStartMs_;
  const auto mode = config.lamp.socialMode;

  if (!stage2Done_ && elapsed >= kStage2Ms &&
      (mode == SocialMode::Ambivert || mode == SocialMode::Extrovert)) {
    stage2Done_ = true;
    if (greetedHasMac_ && context_ && context_->nearbyLamps) {
      const auto peers = context_->nearbyLamps->getReachableViaEspNow(kEspNowMaxAgeMs);
      bool reachable = std::any_of(peers.begin(), peers.end(),
        [&](const lamp::NearbyLamp& p) {
          return p.hasMac && std::memcmp(p.mac, greetedMac_, 6) == 0;
        });
      if (reachable) {
        ExpressionInvocation inv;
        inv.type   = "glitchy";
        inv.colors = {arrivedColor_};
        inv.target = static_cast<uint8_t>(TARGET_SHADE);
        inv.parameters = {{"durationMin", kFastGlitchFrames},
                          {"durationMax", kFastGlitchFrames}};
        inv.delayMs = 0;
        expressionManager.sendInvocationTo(greetedMac_, inv);
      }
    }
  }

  if (!stage3Done_ && elapsed >= kStage3Ms && mode == SocialMode::Extrovert) {
    stage3Done_ = true;
    if (greetedHasMac_ && context_ && context_->nearbyLamps) {
      const auto peers = context_->nearbyLamps->getReachableViaEspNow(kEspNowMaxAgeMs);
      bool reachable = std::any_of(peers.begin(), peers.end(),
        [&](const lamp::NearbyLamp& p) {
          return p.hasMac && std::memcmp(p.mac, greetedMac_, 6) == 0;
        });
      if (reachable) {
        ExpressionInvocation inv;
        inv.type   = "glitchy";
        inv.colors = {};
        if (!config.base.broadcastColors().empty()) {
          inv.colors.push_back(config.base.broadcastColors()[0]);
        }
        inv.colors.push_back(arrivedColor_);
        inv.target = static_cast<uint8_t>(TARGET_SHADE);
        inv.parameters = {{"durationMin", kFastGlitchFrames},
                          {"durationMax", kFastGlitchFrames}};
        inv.delayMs = 0;
        expressionManager.broadcastInvocation(inv, greetedMac_);
      }
    }
  }
}

void Greeting::control() {
  if (!context_ || !context_->nearbyLamps) return;

  tickStages();

  const bool greetingNowActive = (animationState == PLAYING ||
                                  animationState == PLAYING_ONCE ||
                                  animationState == STOPPING);
  if (!greetingNowActive && greetingWasActive_) {
    greetedBdAddr_.clear();
    if (onGreetingChange_) onGreetingChange_();
  }
  greetingWasActive_ = greetingNowActive;

  if (greetingNowActive) {
    return;
  }

  const auto arrivals = context_->nearbyLamps->getUngreetedArrivals(kBleMaxAgeMs);
  for (const auto& p : arrivals) {
    if (p.bdAddr.empty()) continue;
    doGreet(p);
    context_->nearbyLamps->acknowledge(p.name);
    break;
  }
}

void Greeting::draw() {
  if (!fb) { nextFrame(); return; }

  const auto pixelCount = static_cast<uint8_t>(fb->buffer.size());
  if (pixelCount == 0) { nextFrame(); return; }

  if (glitchColors_.empty()) {
    glitchColors_ = calculateGradient(Color(0,45,200,0), Color(180,0,60,0),
                                      pixelCount);
  }

  const uint32_t f = frame;

  if (f < kGlitchFrames) {
    glitchOffset_ = gGreetRng.range(0, pixelCount - 1);
    for (uint8_t i = 0; i < pixelCount; ++i) {
      const uint8_t src = static_cast<uint8_t>((i + glitchOffset_) % pixelCount);
      fb->buffer[i] = glitchColors_[src];
    }
  } else if (f < kFadeInFrames) {
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fade(fb->buffer[i], arrivedColor_, kFadeInFrames, f);
    }
  } else if (f > frames - kEaseFrames) {
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fade(arrivedColor_, fb->buffer[i], kEaseFrames, f % kEaseFrames);
    }
  } else {
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = arrivedColor_;
    }
  }

  nextFrame();
}

}  // namespace lamp::snafu

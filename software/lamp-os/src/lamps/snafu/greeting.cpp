#include "lamps/snafu/greeting.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/fast_rng.hpp"
#include "expressions/expression.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"
#include "config/config.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "core/personality_engine.hpp"
#include "lamps/snafu/dots_behavior.hpp"
#include <Arduino.h>
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
  gs.peerLampId = greetedLampId_;
  gs.kind       = "glitch";
  return gs;
}

void Greeting::doGreet(const lamp::RosterEntry& peer) {
  arrivedColor_    = peer.baseColor;
  std::memcpy(greetedMac_, peer.mac, 6);
  greetedHasMac_   = peer.hasMac;
  greetedLampId_   = peer.macStr();
  greetStartMs_    = millis();
  stage2Done_      = false;
  stage3Done_      = false;
  glitchColors_.clear();
  targetColors_.clear();
  glitchOffset_    = 0;
  frame            = 0;
  greetedBaseStops_.clear();
  greetedShadeStops_.clear();
  if (greetedHasMac_ && meshLink_) {
    meshLink_->sendColorQuery(greetedMac_);
  }
  playOnce();
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
}

void Greeting::triggerGreeting(const lamp::RosterEntry& peer) {
  doGreet(peer);
}

void Greeting::tickStages() {
  const uint32_t elapsed = millis() - greetStartMs_;
  const auto mode = config.lamp.socialMode;

  if (!stage2Done_ && elapsed >= kStage2Ms &&
      (mode == SocialMode::Ambivert || mode == SocialMode::Extrovert)) {
    stage2Done_ = true;
    if (greetedHasMac_) {
      ExpressionInvocation inv;
      inv.type   = "glitchy";
      inv.colors = {arrivedColor_};
      inv.target = static_cast<uint8_t>(TARGET_SHADE);
      inv.parameters = {{"durationMin", kGlitchMinMs},
                        {"durationMax", kGlitchMaxMs}};
      inv.delayMs = 0;
      expressionManager.sendInvocationTo(greetedMac_, inv);
    }
  }

  if (!stage3Done_ && elapsed >= kStage3Ms && mode == SocialMode::Extrovert) {
    stage3Done_ = true;
    if (greetedHasMac_) {
      ExpressionInvocation inv;
      inv.type   = "glitchy";
      inv.colors = {};
      if (!config.base.broadcastColors().empty()) {
        inv.colors.push_back(config.base.broadcastColors()[0]);
      }
      inv.colors.push_back(arrivedColor_);
      inv.target = static_cast<uint8_t>(TARGET_SHADE);
      inv.parameters = {{"durationMin", kGlitchMinMs},
                        {"durationMax", kGlitchMaxMs},
                        {"cascadeStaggerMs", kCascadeStaggerMs}};
      inv.delayMs = 0;
      expressionManager.broadcastInvocation(inv, greetedMac_);
    }
  }
}

void Greeting::control() {
  if (!context_ || !context_->lampRoster) return;

  tickStages();

  const bool greetingNowActive = (animationState == PLAYING ||
                                  animationState == PLAYING_ONCE ||
                                  animationState == STOPPING);
  if (!greetingNowActive && greetingWasActive_) {
    if (dots_ && greetedHasMac_) {
      const uint32_t hold = personalityEngine.greetingFor(greetedLampId_).holdFrames;
      dots_->borrowColors(greetedBaseStops_, greetedShadeStops_,
                          hold * kBorrowHoldMultiplier);
    }
    greetedLampId_.clear();
    if (onGreetingChange_) onGreetingChange_();
  }
  greetingWasActive_ = greetingNowActive;

  if (greetingNowActive) {
    return;
  }

  const auto arrivals = context_->lampRoster->getUngreetedArrivals(kBleMaxAgeMs);
  for (const auto& p : arrivals) {
    doGreet(p);
    context_->lampRoster->acknowledge(p.name);
    break;
  }
}

void Greeting::draw() {
  if (!fb) { nextFrame(); return; }

  const auto pixelCount = static_cast<uint8_t>(fb->buffer.size());
  if (pixelCount == 0) { nextFrame(); return; }

  const bool haveStops = !greetedBaseStops_.empty();
  if (targetColors_.size() != static_cast<size_t>(pixelCount) ||
      haveStops != targetsFromStops_) {
    targetsFromStops_ = haveStops;
    targetColors_.assign(pixelCount, arrivedColor_);
    if (haveStops && !fb->segments.empty()) {
      for (const auto& geo : fb->segments) {
        std::vector<Color> grad = buildGradientWithStops(
            static_cast<uint8_t>(geo.pixelCount), greetedBaseStops_);
        for (size_t i = 0; i < grad.size() && geo.offset + i < targetColors_.size(); ++i) {
          targetColors_[geo.offset + i] = grad[i];
        }
      }
    }
  }

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
      fb->buffer[i] = fade(fb->buffer[i], targetColors_[i], kFadeInFrames, f);
    }
  } else if (f > frames - kEaseFrames) {
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = fade(targetColors_[i], fb->buffer[i], kEaseFrames,
                           f - (frames - kEaseFrames));
    }
  } else {
    for (uint8_t i = 0; i < pixelCount; ++i) {
      fb->buffer[i] = targetColors_[i];
    }
  }

  nextFrame();
}

void Greeting::onColorInfo(const uint8_t srcMac[6],
                           const std::vector<Color>& baseStops,
                           const std::vector<Color>& shadeStops) {
  if (animationState == STOPPED) return;
  if (!greetedHasMac_ || std::memcmp(srcMac, greetedMac_, 6) != 0) return;
  greetedBaseStops_  = baseStops;
  greetedShadeStops_ = shadeStops;
}

}  // namespace lamp::snafu

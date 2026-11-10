#include "social.hpp"

#include <Arduino.h>
#include <algorithm>
#include <array>
#include <cstring>

#include "components/network/mesh/mesh_link.hpp"

#include "components/network/mesh/lamp_roster.hpp"
#include "core/personality_engine.hpp"
#include "core/override_aggregate.hpp"
#include "behaviors/greetable.hpp"
#include "config/config.hpp"
#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "util/easing.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "util/uptime.hpp"

namespace lamp {

namespace {

// Darken `c` toward zero by `strength`/255. strength=0 returns c;
// strength=255 returns black. Per-channel; preserves the hue.
Color darken(const Color& c, uint8_t strength) {
  const uint16_t keep = static_cast<uint16_t>(255 - strength);
  return Color(static_cast<uint8_t>(c.r * keep / 255),
               static_cast<uint8_t>(c.g * keep / 255),
               static_cast<uint8_t>(c.b * keep / 255),
               static_cast<uint8_t>(c.w * keep / 255));
}

// Blend start → end at ramp position `step`/`span` shaped by `curve`.
// Easing shapes the POSITION; the blend itself is linear, so the curve
// isn't applied twice.
Color easedRamp(const Color& start, const Color& end, Easing curve,
                uint32_t step, uint32_t span) {
  const float t = span > 0 ? static_cast<float>(step) / static_cast<float>(span) : 1.0f;
  const uint32_t pos = static_cast<uint32_t>(applyEasing(curve, t) * 255.0f + 0.5f);
  return fadeLinear(start, end, 255, pos);
}

}  // namespace

GreetingState SocialBehavior::greetingState() const {
  if (animationState == STOPPED) return {};
  GreetingState gs;
  gs.active     = true;
  if (greetedHasMac_) {
    char buf[18];
    formatBdAddr(greetedMac_, buf);
    gs.peerLampId = buf;
  }
  if (snub) {
    gs.kind = "snub";
  } else if (pulseBackStrength > 0) {
    gs.kind = "warm";
  } else {
    gs.kind = "reserved";
  }
  return gs;
}

void SocialBehavior::applyTuning(const GreetingTuning& t) {
  easeInFrames      = t.easeInFrames;
  holdFrames        = t.holdFrames;
  fadeOutFrames     = t.fadeOutFrames;
  pulseBackStrength = t.pulseBackStrength;
  pulseBackCount    = t.pulseBackCount;
  breathCycleFrames = t.breathCycleFrames;
  snub              = t.snub;
  easeInCurve       = t.easeInCurve;
  easeOutCurve      = t.easeOutCurve;
  breathCurve       = t.breathCurve;
  // AnimatedBehavior's playOnce/nextFrame drive `frames`; keep it in
  // lockstep with totalFrames.
  frames            = t.totalFrames;
}

void SocialBehavior::draw() {
  // Expressions self-suppress while the operator edits this surface; the
  // greeting must too, or it overpaints the color-editor preview underneath.
  if (lamp::overrides.shade.operatorEditing()) { nextFrame(); return; }

  // Waveform: ease-in → hold (steady or pulsed depending on
  // pulseBackStrength + pulseBackCount) → ease-out.
  const uint32_t easeIn  = easeInFrames;
  const uint32_t hold    = holdFrames;
  const uint32_t fadeOut = fadeOutFrames;

  // Resolve pulse parameters once per draw call. Snubs don't use the
  // in-hold pulse machinery. They fold the dim into the ease-in/out.
  const bool pulseEnabled = (!snub && pulseBackStrength > 0 && pulseBackCount > 0 && hold > 0);
  uint32_t pulseSpan = 0;
  uint32_t cycleFrames = 0;
  if (pulseEnabled) {
    // Clamp the nominal cycle to the available hold so the dip+return
    // always completes inside the hold phase. Without this, a profile
    // whose hold < breathCycleFrames would snap mid-breath when the
    // hold ends.
    const uint32_t nominalCycle = std::min<uint32_t>(breathCycleFrames, hold);
    if (pulseBackCount == kPulseCountContinuous) {
      uint32_t cycles = (hold + nominalCycle / 2) / nominalCycle;
      if (cycles == 0) cycles = 1;
      cycleFrames = hold / cycles;
      pulseSpan = cycleFrames * cycles;
    } else {
      cycleFrames = nominalCycle;
      const uint32_t requested = static_cast<uint32_t>(pulseBackCount) * cycleFrames;
      pulseSpan = (requested < hold) ? requested : hold;
    }
  }

  const bool haveGradient = !greetedBaseStops_.empty();
  if (haveGradient) {
    if (gradientCache_.size() != static_cast<size_t>(fb->pixelCount)) {
      gradientCache_ = buildGradientWithStops(
          static_cast<uint8_t>(fb->pixelCount), greetedBaseStops_);
    }
    if (greetedBlend_ < 255) {
      const uint16_t next = greetedBlend_ + kGradientBlendStep;
      greetedBlend_ = next > 255 ? 255 : static_cast<uint8_t>(next);
    }
  }

  for (int i = 0; i < fb->pixelCount; i++) {
    const Color buf = fb->buffer[i];
    Color target = foundLampColor;
    if (haveGradient) {
      // fade(span=255, pos=blend) is the 0..255 blend primitive here.
      target = fade(foundLampColor, gradientCache_[i], 255, greetedBlend_);
    }
    Color out;
    if (easeIn > 0 && frame < easeIn) {
      out = easedRamp(buf, target, easeInCurve, frame, easeIn - 1);
      if (snub) {
        out = darken(out, easeLinear(0, pulseBackStrength, easeIn - 1, frame));
      }
    } else if (frame < easeIn + hold) {
      const uint32_t holdFrame = frame - easeIn;
      if (snub) {
        out = darken(target, pulseBackStrength);
      } else if (pulseEnabled && holdFrame < pulseSpan && cycleFrames > 0) {
        const Color dimmed = darken(target, pulseBackStrength);
        const uint32_t cyclePos = holdFrame % cycleFrames;
        const uint32_t halfCycle = cycleFrames / 2;
        if (halfCycle > 0 && cyclePos < halfCycle) {
          out = easedRamp(target, dimmed, breathCurve, cyclePos, halfCycle - 1);
        } else if (halfCycle > 0) {
          out = easedRamp(dimmed, target, breathCurve, cyclePos - halfCycle, halfCycle - 1);
        } else {
          out = target;
        }
      } else {
        out = target;
      }
    } else if (fadeOut > 0 && frame < easeIn + hold + fadeOut) {
      const uint32_t fadeFrame = frame - (easeIn + hold);
      out = easedRamp(target, buf, easeOutCurve, fadeFrame, fadeOut - 1);
      if (snub) {
        out = darken(out, easeLinear(pulseBackStrength, 0, fadeOut - 1, fadeFrame));
      }
    } else {
      out = buf;
    }
    fb->buffer[i] = out;
  }

  nextFrame();
};

void SocialBehavior::control() {
  const uint32_t now = millis();
  const uint64_t up = uptimeMs();

  // onGreetingChange_ is the sole path to CHAR_STATE_NOTIFY; keeps behaviors
  // free of BLE includes.
  const bool greetingNowActive = (animationState != STOPPED);
  if (!greetingNowActive && greetingWasActive_) {
    greetedHasMac_ = false;
    appGreeting_ = false;
    if (onGreetingChange_) onGreetingChange_();
  }
  greetingWasActive_ = greetingNowActive;

  // Greeting / cooldown logic below only runs after the previous
  // greeting animation finishes.
  if (animationState != STOPPED) return;

  // An app greeting can run while home mode ignores peers (the preview); once
  // it ends, no peer is picked or acknowledged.
  if (context_ && context_->ignoresPeers()) return;

  if (lamp::overrides.shade.operatorEditing()) return;

  if (up < nextAcknowledgeTimeMs) return;

  const SocialMode mode = config_ ? config_->lamp.socialMode : SocialMode::Ambivert;

  // Introvert fatigue gate: skip if the lamp is in its post-fatigue rest window.
  if (mode == SocialMode::Introvert && up < tiredUntilMs_) {
    return;
  }

  uint32_t regreetWindowMs = 0;
  switch (mode) {
    case SocialMode::Extrovert: regreetWindowMs = 0; break;
    case SocialMode::Ambivert:  regreetWindowMs = AMBIVERT_REGREET_WINDOW_MS; break;
    case SocialMode::Introvert: regreetWindowMs = INTROVERT_REGREET_WINDOW_MS; break;
  }

  // Re-greet window: even if the RosterEntry's acknowledged flag was reset
  // (peer pruned + returned), enforce a per-peer cooldown. The predicate runs
  // in place under the roster mutex; the mac-keyed map keeps it allocation-free.
  RosterEntry arrival;
  const bool haveArrival = lampRoster.bestUngreetedArrival(
      LAMP_PRUNE_TIME_MS, now,
      [&](const RosterEntry& e) {
        if (regreetWindowMs == 0) return true;
        std::array<uint8_t, 6> key;
        std::memcpy(key.data(), e.mac, 6);
        auto last = lastGreetedAtMs_.find(key);
        return last == lastGreetedAtMs_.end() ||
               up - last->second >= regreetWindowMs;
      },
      arrival);
  if (!haveArrival) return;

  const GreetingTuning tuning = personalityEngine.greetingFor(arrival.mac);

#ifdef LAMP_DEBUG
  Serial.printf("[social] greet %s (mode=%u frames=%u pulse=%u count=%u)\n",
                arrival.name, (unsigned)mode,
                (unsigned)tuning.totalFrames,
                (unsigned)tuning.pulseBackStrength,
                (unsigned)tuning.pulseBackCount);
#endif
  lampRoster.acknowledge(arrival.mac);
  foundLampColor = arrival.baseColor;
  std::memcpy(greetedMac_, arrival.mac, 6);
  greetedHasMac_ = arrival.hasMac;
  greetedBaseStops_.clear();
  greetedBlend_ = 0;
  gradientCache_.clear();
  if (greetedHasMac_ && meshLink_) {
    meshLink_->sendColorQuery(greetedMac_);
  }
  applyTuning(tuning);

  markGreeted(arrival.mac, up);

  // Introvert: trim the fatigue window, enter "tired" after too many
  // greetings burned through recently.
  if (mode == SocialMode::Introvert) {
    recentGreetMs_.push_back(up);
    while (!recentGreetMs_.empty() &&
           up - recentGreetMs_.front() > INTROVERT_FATIGUE_WINDOW_MS) {
      recentGreetMs_.erase(recentGreetMs_.begin());
    }
    if (recentGreetMs_.size() >= INTROVERT_FATIGUE_COUNT) {
      tiredUntilMs_ = up + INTROVERT_TIRED_DURATION_MS;
      recentGreetMs_.clear();
#ifdef LAMP_DEBUG
      Serial.printf("[social] introvert tired until +%u ms\n",
                    (unsigned)INTROVERT_TIRED_DURATION_MS);
#endif
    }
  }

  playOnce();
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
};

void SocialBehavior::cancelGreeting() {
  if (animationState == STOPPED && !greetingWasActive_) return;
  animationState = STOPPED;
  frame = 0;
  greetingWasActive_ = false;
  greetedHasMac_ = false;
  appGreeting_ = false;
  if (onGreetingChange_) onGreetingChange_();
}

void SocialBehavior::triggerGreeting(const PeerView& peer) {
  const GreetingTuning tuning = personalityEngine.greetingFor(peer.mac);
  foundLampColor = peer.baseColor;
  std::memcpy(greetedMac_, peer.mac, 6);
  greetedHasMac_ = peer.hasMac;
  greetedBaseStops_.clear();
  greetedBlend_ = 0;
  gradientCache_.clear();
  if (greetedHasMac_ && meshLink_) {
    meshLink_->sendColorQuery(greetedMac_);
  }
  applyTuning(tuning);
  lampRoster.acknowledge(peer.mac);
  appGreeting_ = true;
  playOnce();
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
  markGreeted(peer.mac, uptimeMs());
}

void SocialBehavior::onColorInfo(const uint8_t srcMac[6],
                                 const std::vector<Color>& baseStops,
                                 const std::vector<Color>& /*shadeStops*/) {
  if (animationState == STOPPED) return;
  if (context_ && context_->ignoresPeers()) return;
  if (!greetedHasMac_ || std::memcmp(srcMac, greetedMac_, 6) != 0) return;
  if (baseStops.empty()) return;
  greetedBaseStops_ = baseStops;
}

void SocialBehavior::markGreeted(const uint8_t mac[6], uint64_t upMs) {
  std::array<uint8_t, 6> key;
  std::memcpy(key.data(), mac, 6);
  lastGreetedAtMs_[key] = upMs;
  if (lastGreetedAtMs_.size() > MAX_GREETED_TRACKED) {
    auto oldest = lastGreetedAtMs_.begin();
    for (auto i = lastGreetedAtMs_.begin(); i != lastGreetedAtMs_.end(); ++i) {
      if (i->second < oldest->second) oldest = i;
    }
    lastGreetedAtMs_.erase(oldest);
  }
  const SocialMode mode = config_ ? config_->lamp.socialMode : SocialMode::Ambivert;
  uint32_t cooldown = 0;
  switch (mode) {
    case SocialMode::Extrovert: cooldown = EXTROVERT_COOLDOWN_MS;     break;
    case SocialMode::Ambivert:  cooldown = AMBIVERT_BASE_COOLDOWN_MS; break;
    case SocialMode::Introvert: cooldown = INTROVERT_BASE_COOLDOWN_MS; break;
  }
  nextAcknowledgeTimeMs = upMs + cooldown;
}

}  // namespace lamp

#include "social.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cstring>

#include "components/network/mesh/mesh_link.hpp"

#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/ble/ble_control.hpp"
#include "core/personality_engine.hpp"
#include "behaviors/greetable.hpp"
#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/fs_ota.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "config/config.hpp"
#include "util/color.hpp"
#include "util/easing.hpp"
#include "util/fade.hpp"
#include "util/gradient.hpp"
#include "version.hpp"

// firmwareReceiver lives at file scope in lamp.cpp; the receiver
// header doesn't expose an extern itself (matching the established
// pattern in ota_indicator.cpp); declare it here so the receive-first
// gate in the OTA gossip tick can read its isInProgress().
extern lamp::FirmwareReceiver firmwareReceiver;

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
  gs.peerLampId = greetingPeerLampId_;
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

  // Fires INDEPENDENTLY of the social greeting state. The user's intent
  // (locked in the design): "personality controls the visual greeting,
  // not whether firmware propagates. Version updates outrank social
  // preference." So this loop runs even when:
  //   - The behavior is mid-greeting animation (animationState != STOPPED)
  //   - The greeting cooldown gate (just below) would skip greeting
  //   - The Introvert fatigue window blocks greeting
  //
  // The distributor's per-peer backoff (10 min after a failure, idempotent
  // on Idle re-trigger) handles dedup so calling considerPeerForOta on
  // every social tick is safe.
  //
  // Two throttles to keep this cheap:
  //   (1) Skip entirely while distributor.isInProgress(); the single-
  //       source mutex blocks concurrent sessions, so scanning during
  //       OTA finds nothing actionable.
  //   (2) Throttle the ESP-NOW vector snapshot to kOtaScanIntervalMs
  //       (500 ms). Lamps emit HELLO every 1-2s, so this still catches
  //       every fresh sighting while collapsing the 60 Hz tick rate.
  //
  // Iterates ESP-NOW-reachable peers (not BLE) because firmwareVersion is
  // only populated by ESP-NOW HELLO; BLE-only sightings carry no version.
  //
  // Receive-first policy: skip the outbound loop entirely if either
  //   (a) an OTA is currently RECEIVING. Finish receiving before
  //       burning Core 1 + airtime on outbound chunks that may be
  //       obsolete after the lamp reboots into the new image, OR
  //   (b) any reachable peer reports a higher firmware version than
  //       this lamp's. This lamp is about to be the receiver, so any
  //       outbound started now is just chunks to re-send under the
  //       new image.
  if (!firmwareDistributor.isInProgress() &&
      !::firmwareReceiver.isInProgress() &&
      !fs_ota::fsPathBusy() &&
      (lastOtaScanMs_ == 0 ||
       static_cast<int32_t>(now - lastOtaScanMs_) >=
           static_cast<int32_t>(kOtaScanIntervalMs))) {
    lastOtaScanMs_ = now;
    std::vector<RosterEntry> espNowPeers =
        lampRoster.getMesh(LAMP_PRUNE_TIME_MS);
    bool peerHigherSeen = false;
    for (const auto& p : espNowPeers) {
      if (!p.hasMac) continue;
      if (p.firmwareVersion == 0) continue;
      if (p.firmwareVersion > lamp::FIRMWARE_VERSION) {
        peerHigherSeen = true;
        break;
      }
    }
    if (!peerHigherSeen) {
      for (const auto& p : espNowPeers) {
        if (!p.hasMac) continue;
        if (p.firmwareVersion == 0) continue;
        firmwareDistributor.considerPeerForOta(p.mac, p.firmwareVersion,
                                                p.protocolVersion, now,
                                                p.fwChannel, p.maxChunk,
                                                p.espnowRssi);
      }
      // FS-image OTA: offer the local UI image to same-firmware-version peers
      // whose FS digest differs. fs_ota::considerPeer does the staleness +
      // busy gating internally.
      for (const auto& p : espNowPeers) {
        if (!p.hasMac) continue;
        fs_ota::considerPeer(p.mac, p.firmwareVersion, p.protocolVersion, now,
                             p.fwChannel, p.fsDigest, p.hasFsDigest, p.needsFs,
                             p.espnowRssi);
      }
    }
  }

  // onGreetingChange_ is the sole path to CHAR_STATE_NOTIFY; keeps behaviors
  // free of BLE includes.
  const bool greetingNowActive = (animationState != STOPPED);
  if (!greetingNowActive && greetingWasActive_) {
    greetingPeerLampId_.clear();
    greetedHasMac_ = false;
    if (onGreetingChange_) onGreetingChange_();
  }
  greetingWasActive_ = greetingNowActive;

  // Greeting / cooldown logic below only runs after the previous
  // greeting animation finishes.
  if (animationState != STOPPED) return;

  // A scan burst freshens BLE sightings while an app holds the GATT link;
  // populate the roster but stay quiet during the session.
  if (ble_control::isClientConnected()) return;

  // Wraparound-safe time comparison (millis() rolls over at ~49 days).
  // The re-greet check below uses the same idiom for consistency.
  if (static_cast<int32_t>(now - nextAcknowledgeTimeMs) < 0) return;

  const SocialMode mode = config_ ? config_->lamp.socialMode : SocialMode::Ambivert;

  // Introvert fatigue gate: skip if the lamp is in its post-fatigue rest window.
  if (mode == SocialMode::Introvert &&
      static_cast<int32_t>(now - tiredUntilMs_) < 0) {
    return;
  }

  uint32_t regreetWindowMs = 0;
  switch (mode) {
    case SocialMode::Extrovert: regreetWindowMs = 0; break;
    case SocialMode::Ambivert:  regreetWindowMs = AMBIVERT_REGREET_WINDOW_MS; break;
    case SocialMode::Introvert: regreetWindowMs = INTROVERT_REGREET_WINDOW_MS; break;
  }

  std::vector<RosterEntry> foundLamps =
      lampRoster.getUngreetedArrivals(LAMP_PRUNE_TIME_MS);

  for (auto it = foundLamps.rbegin(); it != foundLamps.rend(); ++it) {
    // Re-greet window: even if the RosterEntry's acknowledged flag was
    // reset (peer pruned + returned), enforce a per-peer cooldown.
    if (regreetWindowMs > 0) {
      auto last = lastGreetedAtMs_.find(it->name);
      if (last != lastGreetedAtMs_.end() && now - last->second < regreetWindowMs) {
        continue;
      }
    }

    const GreetingTuning tuning = personalityEngine.greetingFor(it->macStr());

#ifdef LAMP_DEBUG
    Serial.printf("[social] greet %s (mode=%u frames=%u pulse=%u count=%u)\n",
                  it->name, (unsigned)mode,
                  (unsigned)tuning.totalFrames,
                  (unsigned)tuning.pulseBackStrength,
                  (unsigned)tuning.pulseBackCount);
#endif
    greetingPeerLampId_ = it->macStr();
    lampRoster.acknowledge(it->name);
    foundLampColor = it->baseColor;
    std::memcpy(greetedMac_, it->mac, 6);
    greetedHasMac_ = it->hasMac;
    greetedBaseStops_.clear();
    greetedBlend_ = 0;
    gradientCache_.clear();
    if (greetedHasMac_ && meshLink_) {
      meshLink_->sendColorQuery(greetedMac_);
    }
    applyTuning(tuning);

    markGreeted(it->name, now);

    // Introvert: trim the fatigue window, enter "tired" after too many
    // greetings burned through recently.
    if (mode == SocialMode::Introvert) {
      recentGreetMs_.push_back(now);
      while (!recentGreetMs_.empty() &&
             now - recentGreetMs_.front() > INTROVERT_FATIGUE_WINDOW_MS) {
        recentGreetMs_.erase(recentGreetMs_.begin());
      }
      if (recentGreetMs_.size() >= INTROVERT_FATIGUE_COUNT) {
        tiredUntilMs_ = now + INTROVERT_TIRED_DURATION_MS;
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
    break;
  }
};

void SocialBehavior::triggerGreeting(const RosterEntry& peer) {
  const uint32_t now = millis();
  const GreetingTuning tuning = personalityEngine.greetingFor(peer.macStr());
  greetingPeerLampId_ = peer.macStr();
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
  lampRoster.acknowledge(peer.name);
  playOnce();
  greetingWasActive_ = true;
  if (onGreetingChange_) onGreetingChange_();
  markGreeted(peer.name, now);
}

void SocialBehavior::onColorInfo(const uint8_t srcMac[6],
                                 const std::vector<Color>& baseStops,
                                 const std::vector<Color>& /*shadeStops*/) {
  if (animationState == STOPPED) return;
  if (!greetedHasMac_ || std::memcmp(srcMac, greetedMac_, 6) != 0) return;
  if (baseStops.empty()) return;
  greetedBaseStops_ = baseStops;
}

void SocialBehavior::markGreeted(const std::string& peerName, uint32_t nowMs) {
  lastGreetedAtMs_[peerName] = nowMs;
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
  nextAcknowledgeTimeMs = nowMs + cooldown;
}

}  // namespace lamp

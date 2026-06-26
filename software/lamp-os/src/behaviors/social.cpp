#include "social.hpp"

#include <Arduino.h>
#include <algorithm>
#include <cstring>

#include "components/network/nearby_lamps.hpp"
#include "core/personality_engine.hpp"
#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "config/config.hpp"
#include "util/color.hpp"
#include "util/fade.hpp"
#include "version.hpp"

// firmwareReceiver lives at file scope in lamp.cpp — the receiver
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

}  // namespace

void SocialBehavior::draw() {
  // Waveform: ease-in → hold (steady or pulsed depending on
  // pulseBackStrength + pulseBackCount) → ease-out.
  const uint32_t easeIn  = easeInFrames;
  const uint32_t hold    = holdFrames;
  const uint32_t fadeOut = fadeOutFrames;

  // Resolve pulse parameters once per draw call.
  const bool pulseEnabled = (pulseBackStrength > 0 && pulseBackCount > 0 && hold > 0);
  uint32_t pulseSpan = 0;
  uint32_t cycleFrames = 0;
  if (pulseEnabled) {
    // Clamp the nominal cycle to the available hold so the dip+return
    // always completes inside the hold phase. Without this, a profile
    // whose hold < kSlowPulseCycleFrames would snap mid-breath when the
    // hold ends.
    const uint32_t nominalCycle = std::min<uint32_t>(kSlowPulseCycleFrames, hold);
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

  for (int i = 0; i < fb->pixelCount; i++) {
    const Color buf = fb->buffer[i];
    Color out;
    if (easeIn > 0 && frame < easeIn) {
      out = fade(buf, foundLampColor, easeIn - 1, frame);
    } else if (frame < easeIn + hold) {
      const uint32_t holdFrame = frame - easeIn;
      if (pulseEnabled && holdFrame < pulseSpan && cycleFrames > 0) {
        const Color dimmed = darken(foundLampColor, pulseBackStrength);
        const uint32_t cyclePos = holdFrame % cycleFrames;
        const uint32_t halfCycle = cycleFrames / 2;
        if (halfCycle > 0 && cyclePos < halfCycle) {
          out = fade(foundLampColor, dimmed, halfCycle - 1, cyclePos);
        } else if (halfCycle > 0) {
          out = fade(dimmed, foundLampColor, halfCycle - 1, cyclePos - halfCycle);
        } else {
          out = foundLampColor;
        }
      } else {
        out = foundLampColor;
      }
    } else if (fadeOut > 0 && frame < easeIn + hold + fadeOut) {
      // Phase 3 — ease out back to the underlying expression's pixel.
      const uint32_t fadeFrame = frame - (easeIn + hold);
      out = fade(foundLampColor, buf, fadeOut - 1, fadeFrame);
    } else {
      // Past the explicit window — leave buffer alone (playOnce will stop
      // us at `frames` regardless).
      out = buf;
    }
    fb->buffer[i] = out;
  }

  nextFrame();
};

void SocialBehavior::control() {
  const uint32_t now = millis();

  // -- Gossip OTA tick ---------------------------------------------------
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
  //   (1) Skip entirely while distributor.isInProgress() — the single-
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
  //   (a) we're currently RECEIVING an OTA — finish receiving before
  //       burning Core 1 + airtime on outbound chunks that may be
  //       obsolete the moment we reboot into the new image, OR
  //   (b) any reachable peer reports a higher firmware version than
  //       ours — we're about to be the receiver, so any outbound we
  //       start now is just chunks we'd have to re-send under the
  //       new image.
  if (!firmwareDistributor.isInProgress() &&
      !::firmwareReceiver.isInProgress() &&
      (lastOtaScanMs_ == 0 ||
       static_cast<int32_t>(now - lastOtaScanMs_) >=
           static_cast<int32_t>(kOtaScanIntervalMs))) {
    lastOtaScanMs_ = now;
    std::vector<NearbyLamp> espNowPeers =
        nearbyLamps.getReachableViaEspNow(LAMP_PRUNE_TIME_MS);
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
        if (p.firmwareVersion >= lamp::FIRMWARE_VERSION) continue;
        firmwareDistributor.considerPeerForOta(p.mac, p.firmwareVersion,
                                                p.protocolVersion, now);
      }
    }
  }

  // Greeting / cooldown logic below only runs when the previous
  // greeting animation finished. This early-return USED to be at the
  // top of control() — which silently gated the OTA tick above on the
  // greeting state, the opposite of the comment's stated intent.
  if (animationState != STOPPED) return;

  // Wraparound-safe time comparison (millis() rolls over at ~49 days).
  // The re-greet check below uses the same idiom for consistency.
  if (static_cast<int32_t>(now - nextAcknowledgeTimeMs) < 0) return;

  const SocialMode mode = config_ ? config_->lamp.socialMode : SocialMode::Ambivert;

  // Introvert fatigue gate — if we burnt out recently, take a breather.
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

  // Snapshot taken under nearbyLamps' mutex so iterating is safe against
  // the NimBLE scan task and the ESP-NOW recv task both writing.
  std::vector<NearbyLamp> foundLamps =
      nearbyLamps.getReachableViaBle(LAMP_PRUNE_TIME_MS);

  for (auto it = foundLamps.rbegin(); it != foundLamps.rend(); ++it) {
    // Re-greet window: even if the NearbyLamp's `acknowledged` flag was
    // reset (peer pruned + returned), enforce our own per-peer cooldown.
    if (regreetWindowMs > 0) {
      auto last = lastGreetedAtMs_.find(it->name);
      if (last != lastGreetedAtMs_.end() && now - last->second < regreetWindowMs) {
        continue;
      }
    }
    // Skip already-acknowledged (within the NearbyLamp lifetime) so we
    // don't greet the same peer twice in a single sighting.
    if (it->acknowledged) continue;

    const GreetingTuning tuning = personalityEngine.greetingFor(it->bdAddr);

#ifdef LAMP_DEBUG
    Serial.printf("[social] greet %s (mode=%u frames=%u pulse=%u count=%u)\n",
                  it->name.c_str(), (unsigned)mode,
                  (unsigned)tuning.totalFrames,
                  (unsigned)tuning.pulseBackStrength,
                  (unsigned)tuning.pulseBackCount);
#endif
    nearbyLamps.acknowledge(it->name);
    foundLampColor = it->baseColor;

    // Copy the engine's waveform into our draw-side fields. AnimatedBehavior's
    // `frames` drives playOnce / nextFrame — keep it in lockstep with totalFrames.
    easeInFrames      = tuning.easeInFrames;
    holdFrames        = tuning.holdFrames;
    fadeOutFrames     = tuning.fadeOutFrames;
    pulseBackStrength = tuning.pulseBackStrength;
    pulseBackCount    = tuning.pulseBackCount;
    frames            = tuning.totalFrames;

    // Record into our persistent (in-memory) greeting log.
    lastGreetedAtMs_[it->name] = now;
    if (lastGreetedAtMs_.size() > MAX_GREETED_TRACKED) {
      // LRU eviction — drop the entry with the smallest timestamp.
      auto oldest = lastGreetedAtMs_.begin();
      for (auto i = lastGreetedAtMs_.begin(); i != lastGreetedAtMs_.end(); ++i) {
        if (i->second < oldest->second) oldest = i;
      }
      lastGreetedAtMs_.erase(oldest);
    }

    // Per-mode cooldown.
    uint32_t cooldown = 0;
    switch (mode) {
      case SocialMode::Extrovert:
        cooldown = EXTROVERT_COOLDOWN_MS;
        break;
      case SocialMode::Ambivert:
        cooldown = AMBIVERT_BASE_COOLDOWN_MS;
        break;
      case SocialMode::Introvert:
        cooldown = INTROVERT_BASE_COOLDOWN_MS;
        break;
    }
    nextAcknowledgeTimeMs = now + cooldown;

    // Introvert: trim the fatigue window, enter "tired" if we've burned
    // through too many greetings recently.
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
    break;
  }
};

#ifdef LAMP_DEBUG
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
#endif

}  // namespace lamp

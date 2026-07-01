#include "core/personality_engine.hpp"

#include <algorithm>
#include <cmath>

#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_manager.hpp"

namespace lamp {

PersonalityEngine personalityEngine;

void PersonalityEngine::begin(Config* config,
                              ExpressionManager* expressionManager,
                              MeshLink* meshLink) {
  config_ = config;
  expressionManager_ = expressionManager;
  meshLink_ = meshLink;
  if (config_) {
    lastSocialMode_ = config_->lamp.socialMode;
  }
}

void PersonalityEngine::tick(uint32_t nowMs) {
  if (!config_) return;

  // SocialMode transitions cross dim regimes. Trip pendingApply on any
  // mode change while a non-trivial dim is committed so the lamp
  // re-evaluates brightness against the new floor (or releases dim
  // entirely when crossing into Extrovert).
  const SocialMode currentMode = config_->lamp.socialMode;
  if (currentMode != lastSocialMode_) {
    lastSocialMode_ = currentMode;
    if (currentMode == SocialMode::Extrovert) {
      // Hard release: floor is 1.0 in Extrovert. Reset the committed
      // level so the "never blank" guard stays correct and the next
      // mode-flip back picks up cleanly from the smoother.
      crowdDimFactor_ = 1.0f;
      lastCommittedLevel_ = 100;
      pendingApply_ = true;
    } else if (crowdDimFactor_ < 1.0f || lastCommittedLevel_ != 100) {
      // Floor changed; force a re-apply so brightness reflects the new
      // regime on the next loop tick. The smoother is left intact —
      // smoothedW_ / sampleBuf_ carry forward so a quick flip doesn't
      // re-stretch the deadband from zero.
      pendingApply_ = true;
    }
  }

  // Per-tick work that doesn't gate on the 1 Hz sample cadence —
  // closest-Smitten cycle should be reactive (a new closest Smitten peer
  // shouldn't wait up to 1s for the pulse). Cheap when nothing changed.
  const std::vector<NearbyLamp> peers = snapshotBlePeers_();
  tickClosestSmittenPulse_(nowMs, peers);

  // 1 Hz sample cadence — crowd-dim sampling lives here so the median
  // window is uniformly-spaced regardless of loop jitter. Reuses the
  // `peers` snapshot from above instead of re-calling snapshotBlePeers_()
  // — saves a vector copy + RSSI sort on every sample tick.
  if (nowMs - lastSampleMs_ >= kSamplePeriodMs || lastSampleMs_ == 0) {
    lastSampleMs_ = nowMs;
    sampleAndSmoothCrowd_(nowMs, peers);
  }
}

uint8_t PersonalityEngine::applyCrowdDim(uint8_t baseline) const {
  if (!config_) return baseline;
  if (config_->lamp.socialMode == SocialMode::Extrovert) return baseline;
  if (crowdDimFactor_ >= 0.999f) return baseline;
  const float scaled = static_cast<float>(baseline) * crowdDimFactor_;
  // Round + floor at 1 — never let personality blank the lamp.
  uint8_t out = static_cast<uint8_t>(scaled + 0.5f);
  if (out < 1) out = 1;
  return out;
}

float PersonalityEngine::crowdDimFactor() const {
  if (!config_) return 1.0f;
  if (config_->lamp.socialMode == SocialMode::Extrovert) return 1.0f;
  return crowdDimFactor_;
}

float PersonalityEngine::smoothedCrowdWeight() const {
  return smoothedW_;
}

CrowdComposition PersonalityEngine::crowdComposition() const {
  CrowdComposition c;
  if (!config_) return c;
  const std::vector<NearbyLamp> peers = snapshotBlePeers_();
  for (const auto& p : peers) {
    if (p.name.empty()) continue;
    const uint8_t d = p.bdAddr.empty() ? 3 : config_->getDisposition(p.bdAddr);
    switch (d) {
      case 1: if (c.salty   < 255) c.salty++;   break;
      case 2: if (c.wary    < 255) c.wary++;    break;
      case 4: if (c.fond    < 255) c.fond++;    break;
      case 5: if (c.smitten < 255) c.smitten++; break;
      case 3:
      default: if (c.neutral < 255) c.neutral++; break;
    }
  }
  return c;
}

namespace {

// Waveform profiles at 60 fps. Anchored on kProfileStandard
// (Ambivert greeting a Neutral peer) = 2s in / 16s hold / 2s out, the
// neutral baseline. Variations are deliberately subtle in total length
// (~17-24s for a real greeting); the body language is carried by
// ASYMMETRY, not duration: warmer/more-extrovert greetings pop in fast
// and linger on the way out (eager hello, reluctant goodbye), colder/
// introvert ones ease in slowly and leave quickly. Snubs are the one
// outlier — short by design so a brush-off reads as dismissal.
struct Profile {
  uint32_t total;
  uint32_t easeIn;
  uint32_t hold;
  uint32_t fadeOut;
  uint8_t  pulseBackStrength;
  uint8_t  pulseBackCount;
  bool     snub;
};

constexpr Profile kProfileMinimal           = {1020, 150,  780,  90,   0, 0, false};
constexpr Profile kProfileGentle            = {1110, 120,  840, 150,   0, 0, false};
constexpr Profile kProfileStandard          = {1200, 120,  960, 120,   0, 0, false};
constexpr Profile kProfileWarm              = {1290,  90, 1020, 180, 100, 1, false};
constexpr Profile kProfileEnthused          = {1350,  60, 1080, 210, 128, 2, false};
constexpr Profile kProfileEffusive          = {1425,  45, 1140, 240, 153,
                                                kPulseCountContinuous, false};
constexpr Profile kProfileSnub              = {270,  60, 150, 60, 255, 1, true};
constexpr Profile kProfilePartialSnub       = {360,  60, 210, 90, 128, 1, true};
constexpr Profile kProfileSnubQuick         = {330,  60, 180, 90, 255, 1, true};
constexpr Profile kProfilePartialSnubQuick  = {390,  60, 240, 90, 128, 1, true};

GreetingTuning toTuning(const Profile& p) {
  GreetingTuning t;
  t.totalFrames       = p.total;
  t.easeInFrames      = p.easeIn;
  t.holdFrames        = p.hold;
  t.fadeOutFrames     = p.fadeOut;
  t.pulseBackStrength = p.pulseBackStrength;
  t.pulseBackCount    = p.pulseBackCount;
  t.snub              = p.snub;
  return t;
}

// (SocialMode × Disposition) → Profile.
const Profile& profileFor(lamp::SocialMode mode, uint8_t disposition) {
  switch (disposition) {
    case 1:  // Salty
      switch (mode) {
        case lamp::SocialMode::Introvert: return kProfileSnub;
        case lamp::SocialMode::Ambivert:  return kProfileSnub;
        case lamp::SocialMode::Extrovert: return kProfileSnubQuick;
      }
      return kProfileSnub;
    case 2:  // Wary
      switch (mode) {
        case lamp::SocialMode::Introvert: return kProfilePartialSnub;
        case lamp::SocialMode::Ambivert:  return kProfilePartialSnub;
        case lamp::SocialMode::Extrovert: return kProfilePartialSnubQuick;
      }
      return kProfilePartialSnub;
    case 4:  // Fond
      switch (mode) {
        case lamp::SocialMode::Introvert: return kProfileGentle;
        case lamp::SocialMode::Ambivert:  return kProfileWarm;
        case lamp::SocialMode::Extrovert: return kProfileEnthused;
      }
      return kProfileWarm;
    case 5:  // Smitten
      switch (mode) {
        case lamp::SocialMode::Introvert: return kProfileWarm;
        case lamp::SocialMode::Ambivert:  return kProfileEnthused;
        case lamp::SocialMode::Extrovert: return kProfileEffusive;
      }
      return kProfileEnthused;
    case 3:  // Neutral (also the default for unknown peers)
    default:
      switch (mode) {
        case lamp::SocialMode::Introvert: return kProfileMinimal;
        case lamp::SocialMode::Ambivert:  return kProfileStandard;
        case lamp::SocialMode::Extrovert: return kProfileStandard;
      }
      return kProfileStandard;
  }
}

}  // namespace

GreetingTuning PersonalityEngine::greetingFor(const std::string& peerBdAddr) const {
  if (!config_) return toTuning(kProfileStandard);
  const SocialMode mode = config_->lamp.socialMode;
  // Empty bdAddr → Neutral profile. Defensive against any caller passing an
  // unpopulated bdAddr; also avoids accidentally matching a stray empty
  // key in dispositions_.
  if (peerBdAddr.empty()) return toTuning(profileFor(mode, 3));
  const uint8_t disp = config_->getDisposition(peerBdAddr);  // unknown → 3
  return toTuning(profileFor(mode, disp));
}

#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
void PersonalityEngine::setNearbyOverride(std::vector<NearbyLamp> peers) {
  nearbyOverride_ = std::move(peers);
  nearbyOverrideActive_ = true;
}

void PersonalityEngine::clearNearbyOverride() {
  nearbyOverride_.clear();
  nearbyOverrideActive_ = false;
}
#endif

// --- Crowd-dim internals -------------------------------------------------

float PersonalityEngine::floorForMode_(SocialMode mode) {
  switch (mode) {
    case SocialMode::Introvert: return kIntrovertFloor;
    case SocialMode::Ambivert:  return kAmbivertFloor;
    case SocialMode::Extrovert: return 1.0f;
  }
  return 1.0f;
}

float PersonalityEngine::weightForDisposition_(uint8_t d, SocialMode mode) const {
  // Ambivert: warm relationships (Fond, Smitten) don't add crowd pressure;
  // only Neutral-and-worse count. Introvert weights everything (Smitten
  // still 0, since favorites never crowd you).
  if (mode == SocialMode::Ambivert) {
    switch (d) {
      case 1: return 2.0f;   // Salty
      case 2: return 1.5f;   // Wary
      case 3: return 1.0f;   // Neutral
      case 4: return 0.0f;   // Fond — no crowd pressure in Ambivert
      case 5: return 0.0f;   // Smitten
      default: return 1.0f;  // unknown → Neutral
    }
  }
  // Introvert table (also the conservative default — Extrovert never
  // reaches this function because applyCrowdDim short-circuits).
  switch (d) {
    case 1: return 2.0f;   // Salty
    case 2: return 1.5f;   // Wary
    case 3: return 1.0f;   // Neutral
    case 4: return 0.5f;   // Fond
    case 5: return 0.0f;   // Smitten
    default: return 1.0f;  // unknown → Neutral
  }
}

float PersonalityEngine::computeWeightedCount_(
    const std::vector<NearbyLamp>& peers, SocialMode mode) const {
  if (!config_) return 0.0f;
  float w = 0.0f;
  for (const auto& p : peers) {
    if (p.name.empty()) continue;
    if (p.bdAddr.empty()) continue;
    const uint8_t d = config_->getDisposition(p.bdAddr);
    w += weightForDisposition_(d, mode);
  }
  return w;
}

float PersonalityEngine::dimFactorForCount_(float weightedCount, float floor) const {
  if (weightedCount <= 0.0f) return 1.0f;
  if (floor >= 1.0f) return 1.0f;
  // factor = max(floor, 1 - (1-floor) * log10(1+W) / log10(1+kCurveScaleW))
  //
  // kCurveScaleW is constexpr (= 10.0f), so log10(1+kCurveScaleW) = log10(11)
  // is also constant. Precompute the denominator at compile time so the only
  // float work left at runtime is one log10, one divide, and a few mul/adds.
  // Sample tick runs at 1 Hz so the savings are small, but free.
  static constexpr float kLog10OnePlusCurveScale = 1.0413927f;  // log10(11)
  const float numer = std::log10(1.0f + weightedCount);
  const float drop  = (1.0f - floor) * (numer / kLog10OnePlusCurveScale);
  float factor = 1.0f - drop;
  if (factor < floor) factor = floor;
  if (factor > 1.0f)  factor = 1.0f;
  return factor;
}

std::vector<NearbyLamp> PersonalityEngine::snapshotBlePeers_() const {
  // Gate must match setNearbyOverride() in the header — that method
  // compiles under (LAMP_TEST || LAMP_DEBUG), so the read side has to
  // honor the override in both build flavors. Without LAMP_DEBUG here,
  // the inject_nearby BLE test-action accepts the payload but the
  // engine silently keeps reading live BLE.
#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  if (nearbyOverrideActive_) return nearbyOverride_;
#endif
  return nearbyLamps.getReachableViaBle(LAMP_PRUNE_TIME_MS);
}

void PersonalityEngine::sampleAndSmoothCrowd_(
    uint32_t /*nowMs*/, const std::vector<NearbyLamp>& peers) {
  if (!config_) return;
  const SocialMode mode = config_->lamp.socialMode;
  const float rawW = computeWeightedCount_(peers, mode);

  // Insert into rolling buffer.
  sampleBuf_[sampleHead_] = rawW;
  sampleHead_ = (sampleHead_ + 1) % kSampleWindow;
  if (sampleCount_ < kSampleWindow) sampleCount_++;

  // Median of the active window.
  float sorted[kSampleWindow];
  for (size_t i = 0; i < sampleCount_; ++i) sorted[i] = sampleBuf_[i];
  std::sort(sorted, sorted + sampleCount_);
  const float median = (sampleCount_ % 2 == 1)
      ? sorted[sampleCount_ / 2]
      : 0.5f * (sorted[sampleCount_ / 2 - 1] + sorted[sampleCount_ / 2]);

  // EMA on top of the median.
  if (!emaSeeded_) {
    smoothedW_ = median;
    emaSeeded_ = true;
  } else {
    smoothedW_ = kEmaAlpha * median + (1.0f - kEmaAlpha) * smoothedW_;
  }

  // Compute target factor + target level at a nominal baseline of 100.
  const float floor = floorForMode_(mode);
  const float targetFactor = dimFactorForCount_(smoothedW_, floor);
  const uint8_t targetLevel = static_cast<uint8_t>(targetFactor * 100.0f + 0.5f);

  // Only commit when the change crosses the deadband. Extrovert mode skips
  // the commit (applyCrowdDim returns identity anyway, and we don't want
  // to trip pendingApply on smoothing wobble while dim is disengaged).
  const int delta = static_cast<int>(targetLevel) - static_cast<int>(lastCommittedLevel_);
  const int absDelta = delta < 0 ? -delta : delta;
  if (absDelta >= kDeadbandLevels) {
    crowdDimFactor_ = targetFactor;
    lastCommittedLevel_ = targetLevel;
    if (mode != SocialMode::Extrovert) {
      pendingApply_ = true;
    }
  }
}

// --- Smitten closest cycle ----------------------------------------------

void PersonalityEngine::firePulse_(const Color& color) {
  if (!expressionManager_ || !meshLink_) return;
  ExpressionInvocation inv;
  inv.type = "pulse";
  inv.colors = {color};
  inv.target = 3;  // BOTH
  inv.parameters["cycles"] = 2;
  uint8_t myMac[6] = {0};
  meshLink_->getMyMac(myMac);
  (void)expressionManager_->triggerInvocation(inv, myMac);
}

void PersonalityEngine::tickClosestSmittenPulse_(uint32_t nowMs,
                                                  const std::vector<NearbyLamp>& peers) {
  if (!config_) return;
  // peers from getReachableViaBle() are sorted by lastRssi descending —
  // closest is front. Filter for non-empty names.
  const NearbyLamp* closest = nullptr;
  for (const auto& p : peers) {
    if (!p.name.empty()) { closest = &p; break; }
  }
  if (!closest) {
    // No BLE peers at all — release closest state, including the cadence
    // clock. If a Smitten peer re-becomes closest later, the transition
    // path should start a fresh cadence from THAT moment, not inherit a
    // stale clock that could fire a pulse moments after the new transition.
    closestSmittenName_.clear();
    lastClosestPulseMs_ = 0;
    return;
  }
  if (closest->bdAddr.empty()) {
    closestSmittenName_.clear();
    lastClosestPulseMs_ = 0;
    return;
  }
  const uint8_t disp = config_->getDisposition(closest->bdAddr);
  if (disp != 5) {
    // Closest exists but isn't Smitten (or this peer's disposition just got
    // demoted). Same reset rationale as above — don't carry a stale cadence
    // clock through a Smitten ↔ non-Smitten flip.
    closestSmittenName_.clear();
    lastClosestPulseMs_ = 0;
    return;
  }
  // Transition (different closest name OR first time): fire immediately,
  // BUT only after a hysteresis check. Without hysteresis, two Smitten
  // peers with similar RSSI flap closest on dBm noise — each flip fires
  // a pulse, producing a strobe-light artifact at ~60Hz. Require the new
  // closest's RSSI to beat the previous closest's by ≥ kRssiHysteresisDb
  // before swapping. The previous closest is found in the current
  // snapshot (it may have moved further away, in which case its RSSI is
  // fresh); if it's no longer visible at all, transition without
  // hysteresis (the prev peer is gone).
  if (closest->name != closestSmittenName_) {
    if (!closestSmittenName_.empty()) {
      const NearbyLamp* prev = nullptr;
      for (const auto& p : peers) {
        if (p.name == closestSmittenName_) { prev = &p; break; }
      }
      if (prev != nullptr) {
        const int delta = static_cast<int>(closest->lastRssi) -
                          static_cast<int>(prev->lastRssi);
        if (delta < static_cast<int>(kRssiHysteresisDb)) {
          // New "closest" isn't decisively closer than the one we already
          // committed to. Keep the previous closest. Do NOT touch
          // lastClosestPulseMs_ — the cadence keeps running against the
          // previous transition.
          return;
        }
      }
    }
    closestSmittenName_ = closest->name;
    lastClosestPulseMs_ = nowMs;
    firePulse_(closest->baseColor);
    return;
  }
  // Same closest — sustained pulse cadence. Wraparound-safe gap idiom.
  const int32_t sinceLast = static_cast<int32_t>(nowMs - lastClosestPulseMs_);
  if (sinceLast >= static_cast<int32_t>(kClosestPulsePeriodMs)) {
    lastClosestPulseMs_ = nowMs;
    firePulse_(closest->baseColor);
  }
}

}  // namespace lamp

#include "core/personality_engine.hpp"

#include <algorithm>
#include <cmath>

#include "config/config.hpp"

namespace lamp {

PersonalityEngine personalityEngine;

void PersonalityEngine::begin(Config* config) {
  config_ = config;
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
      // regime on the next loop tick. The smoother is left intact;
      // smoothedW_ / sampleBuf_ carry forward so a quick flip doesn't
      // re-stretch the deadband from zero.
      pendingApply_ = true;
    }
  }

  // 1 Hz sample cadence. Crowd-dim sampling lives here so the median
  // window is uniformly-spaced regardless of loop jitter.
  if (nowMs - lastSampleMs_ >= kSamplePeriodMs || lastSampleMs_ == 0) {
    lastSampleMs_ = nowMs;
    blePeerCache_ = snapshotBlePeers_();
    sampleAndSmoothCrowd_(nowMs, blePeerCache_);
  }
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
  const std::vector<RosterEntry> peers = snapshotBlePeers_();
  for (const auto& p : peers) {
    if (p.name[0] == '\0') continue;
    const uint8_t d = p.hasMac ? config_->getDisposition(p.macStr()) : 3;
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

// Waveform profiles in compositor frames (~60 fps). Anchored on
// kProfileStandard (Ambivert greeting a Neutral peer) = ~2.8s in / 16s hold /
// 2.5s out, the neutral baseline. Every profile holds >= 5s so no greeting
// reads as a blink, every ramp is >= 2s so nothing snaps, and the ease-out
// lengthens with warmth for a reluctant goodbye. Disposition reads in the
// MOTION as much as the timing: warm greetings swell in and smooth back out;
// snubs hold a deep dim floor (pulseBackStrength) so a brush-off reads as a
// cold-shoulder, not a quick flash.
struct Profile {
  uint32_t total;
  uint32_t easeIn;
  uint32_t hold;
  uint32_t fadeOut;
  uint8_t  pulseBackStrength;
  uint8_t  pulseBackCount;
  uint16_t breathCycleFrames;
  bool     snub;
  Easing   easeInCurve;
  Easing   easeOutCurve;
  Easing   breathCurve;
};

// Warm-breath cycle lengths (frames at ~60 fps). Warmer disposition breathes
// faster; a 3 s floor keeps even the eagerest breath from reading as a
// flutter. Bench-tunable. Non-pulsing profiles carry the default; the field is
// inert when pulseBackStrength is 0.
constexpr uint16_t kDefaultBreathFrames  = 120;
constexpr uint16_t kWarmBreathFrames     = 300;  // ~5.0 s, slow + calm
constexpr uint16_t kEnthusedBreathFrames = 240;  // ~4.0 s
constexpr uint16_t kEffusiveBreathFrames = 180;  // ~3.0 s, eager

// 5s at ~60 fps; the shortest a greeting lingers. Bench-tunable.
constexpr uint32_t kMinHoldFrames = 300;

constexpr Profile kProfileMinimal           = {1080, 180,  780, 120, 0, 0, kDefaultBreathFrames, false,
                                                Easing::Smooth, Easing::Smooth, Easing::Float};
constexpr Profile kProfileGentle            = {1176, 156,  840, 180, 0, 0, kDefaultBreathFrames, false,
                                                Easing::Swell, Easing::Smooth, Easing::Float};
constexpr Profile kProfileStandard          = {1278, 168,  960, 150, 0, 0, kDefaultBreathFrames, false,
                                                Easing::Smooth, Easing::Smooth, Easing::Float};
// Warm pulse depths are a gentle glow-breath, not a flash. Bench-tunable.
constexpr uint8_t kWarmPulseDim     = 50;
constexpr uint8_t kEnthusedPulseDim = 65;
constexpr uint8_t kEffusivePulseDim = 80;

// Warm greetings breathe for the entire hold; depth + cycle speed carry the
// disposition (deeper + faster = more excited).
constexpr Profile kProfileWarm              = {1404, 144, 1020, 240, kWarmPulseDim,
                                                kPulseCountContinuous, kWarmBreathFrames, false,
                                                Easing::Swell, Easing::Smooth, Easing::Float};
constexpr Profile kProfileEnthused          = {1512, 132, 1080, 300, kEnthusedPulseDim,
                                                kPulseCountContinuous, kEnthusedBreathFrames, false,
                                                Easing::Swell, Easing::Smooth, Easing::Smooth};
constexpr Profile kProfileEffusive          = {1620, 120, 1140, 360, kEffusivePulseDim,
                                                kPulseCountContinuous, kEffusiveBreathFrames, false,
                                                Easing::Swell, Easing::Smooth, Easing::Smooth};
// Snub dim depth: 191 → ~25% brightness, 165 → ~35%. A real cold floor,
// never fully off. Bench-tunable.
constexpr uint8_t kFullSnubDim    = 191;
constexpr uint8_t kPartialSnubDim = 165;

constexpr Profile kProfileSnub              = {540, 120, kMinHoldFrames, 120, kFullSnubDim, 1, kDefaultBreathFrames, true,
                                                Easing::Smooth, Easing::Smooth, Easing::Float};
constexpr Profile kProfilePartialSnub       = {570, 120, kMinHoldFrames, 150, kPartialSnubDim, 1, kDefaultBreathFrames, true,
                                                Easing::Smooth, Easing::Smooth, Easing::Float};

GreetingTuning toTuning(const Profile& p) {
  GreetingTuning t;
  t.totalFrames       = p.total;
  t.easeInFrames      = p.easeIn;
  t.holdFrames        = p.hold;
  t.fadeOutFrames     = p.fadeOut;
  t.pulseBackStrength = p.pulseBackStrength;
  t.pulseBackCount    = p.pulseBackCount;
  t.breathCycleFrames = p.breathCycleFrames;
  t.snub              = p.snub;
  t.easeInCurve       = p.easeInCurve;
  t.easeOutCurve      = p.easeOutCurve;
  t.breathCurve       = p.breathCurve;
  return t;
}

// (SocialMode × Disposition) → Profile.
const Profile& profileFor(lamp::SocialMode mode, uint8_t disposition) {
  switch (disposition) {
    case 1:  // Salty
      return kProfileSnub;
    case 2:  // Wary
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

GreetingTuning PersonalityEngine::greetingFor(const std::string& peerLampId) const {
  if (!config_) return toTuning(kProfileStandard);
  const SocialMode mode = config_->lamp.socialMode;
  // Empty lampId → Neutral profile. Avoids accidentally matching a stray
  // empty key in dispositions_.
  if (peerLampId.empty()) return toTuning(profileFor(mode, 3));
  const uint8_t disp = config_->getDisposition(peerLampId);  // unknown → 3
  return toTuning(profileFor(mode, disp));
}

#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
void PersonalityEngine::setNearbyOverride(std::vector<RosterEntry> peers) {
  nearbyOverride_ = std::move(peers);
  nearbyOverrideActive_ = true;
}

void PersonalityEngine::clearNearbyOverride() {
  nearbyOverride_.clear();
  nearbyOverrideActive_ = false;
}
#endif

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
      case 4: return 0.0f;   // Fond, no crowd pressure in Ambivert
      case 5: return 0.0f;   // Smitten
      default: return 1.0f;  // unknown → Neutral
    }
  }
  // Introvert table (also the conservative default; in Extrovert
  // crowdDimFactor() returns 1.0 so the weighting is moot).
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
    const std::vector<RosterEntry>& peers, SocialMode mode) const {
  if (!config_) return 0.0f;
  float w = 0.0f;
  for (const auto& p : peers) {
    if (p.name[0] == '\0') continue;
    if (!p.hasMac) continue;
    const uint8_t d = config_->getDisposition(p.macStr());
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

std::vector<RosterEntry> PersonalityEngine::snapshotBlePeers_() const {
  // Gate must match setNearbyOverride() in the header; that method
  // compiles under (LAMP_TEST || LAMP_DEBUG), so the read side has to
  // honor the override in both build flavors. Without LAMP_DEBUG here,
  // the inject_nearby BLE test-action accepts the payload but the
  // engine silently keeps reading live BLE.
#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  if (nearbyOverrideActive_) return nearbyOverride_;
#endif
  return lampRoster.getNear(LAMP_PRUNE_TIME_MS);
}

void PersonalityEngine::sampleAndSmoothCrowd_(
    uint32_t /*nowMs*/, const std::vector<RosterEntry>& peers) {
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
  // the commit (crowdDimFactor() returns 1.0 anyway, and tripping
  // pendingApply on smoothing wobble while dim is disengaged is unwanted).
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

}  // namespace lamp

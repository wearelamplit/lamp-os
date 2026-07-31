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
    const uint8_t d = p.hasMac ? config_->getDisposition(p.mac) : 3;
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

GreetingTuning PersonalityEngine::greetingFor(const uint8_t mac[6]) const {
  if (!config_) return greetingTuningFor(SocialMode::Ambivert, 3);
  const SocialMode mode = config_->lamp.socialMode;
  return greetingTuningFor(mode, config_->getDisposition(mac));  // unknown → 3
}

GreetingTuning PersonalityEngine::greetingFor(const std::string& peerLampId) const {
  if (!config_) return greetingTuningFor(SocialMode::Ambivert, 3);
  const SocialMode mode = config_->lamp.socialMode;
  // Empty lampId → Neutral profile. Avoids accidentally matching a stray
  // empty key in dispositions_.
  if (peerLampId.empty()) return greetingTuningFor(mode, 3);
  const uint8_t disp = config_->getDisposition(peerLampId);  // unknown → 3
  return greetingTuningFor(mode, disp);
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
    const uint8_t d = config_->getDisposition(p.mac);
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

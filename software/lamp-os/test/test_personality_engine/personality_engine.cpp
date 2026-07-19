// Native-host unit tests for PersonalityEngine. Mirrors the production
// engine inline (same convention as test_transient_override) so the
// native rig doesn't need to link the firmware. Each test pins the
// observable contract (curve shape, mode-flip release, greeting matrix)
// so production refactors are free as long as the contract holds.

#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "util/easing.hpp"

namespace test {

using lamp::Easing;

// --- Time scaffolding -----------------------------------------------------
static uint32_t g_nowMs = 0;

// --- Mirrors of production types -----------------------------------------

enum class SocialMode : uint8_t {
  Introvert = 0,
  Ambivert  = 1,
  Extrovert = 2,
};

struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  Color() = default;
  Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW)
      : r(inR), g(inG), b(inB), w(inW) {}
  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && w == o.w;
  }
};

struct RosterEntry {
  std::string name;
  std::string lampId;
  Color baseColor;
  int8_t lastRssi = -127;
};

// Sentinel for pulseBackCount meaning "fill the entire hold window with
// back-to-back cycles" instead of a fixed cycle count.
static constexpr uint8_t kPulseCountContinuous = 0xFF;

// Mirror of lamp::GreetingTuning + the matrix profiles. Tests pin the
// observable shape — production refactor of internals is fine as long
// as these (mode × disposition) cells continue to map to the same
// (totalFrames, pulseBackStrength, pulseBackCount) outputs.
struct GreetingTuning {
  uint32_t totalFrames = 0;
  uint32_t easeInFrames = 0;
  uint32_t holdFrames = 0;
  uint32_t fadeOutFrames = 0;
  uint8_t  pulseBackStrength = 0;
  uint8_t  pulseBackCount    = 0;
  uint16_t breathCycleFrames = 120;
  bool     snub              = false;
  Easing   easeInCurve  = Easing::Smooth;
  Easing   easeOutCurve = Easing::Smooth;
  Easing   breathCurve  = Easing::Float;
};

struct CrowdComposition {
  uint8_t salty   = 0;
  uint8_t wary    = 0;
  uint8_t neutral = 0;
  uint8_t fond    = 0;
  uint8_t smitten = 0;
};

// Minimal Config stand-in: just enough to support socialMode + disposition
// lookup. Mirrors the public surface PersonalityEngine reads.
class FakeConfig {
 public:
  SocialMode socialMode = SocialMode::Ambivert;
  // Key is lampId, the mesh mac in uppercase colon-hex. The map doesn't
  // care what the key is, but tests uniformly key by lampId.
  std::map<std::string, uint8_t> dispositions;
  uint8_t getDisposition(const std::string& lampId) const {
    auto it = dispositions.find(lampId);
    return it == dispositions.end() ? /*kDispositionDefault=*/3 : it->second;
  }
};

// --- PersonalityEngine inline mirror -------------------------------------
//
// Mirrors the surface that effectiveBrightness() / loop() in lamp.cpp
// consumes, plus the greeting matrix exercised by the tests below.
class PersonalityEngine {
 public:
  static constexpr float kIntrovertFloor = 0.5f;
  static constexpr float kAmbivertFloor  = 0.7f;
  static constexpr float kCurveScaleW = 10.0f;
  static constexpr uint32_t kSamplePeriodMs = 1000;
  static constexpr size_t kSampleWindow = 4;
  static constexpr float kEmaAlpha = 0.15f;
  static constexpr uint8_t kDeadbandLevels = 2;

  void begin(FakeConfig* config) {
    config_ = config;
    if (config_) lastSocialMode_ = config_->socialMode;
  }

  void setNearbyOverride(std::vector<RosterEntry> peers) {
    nearbyOverride_ = std::move(peers);
  }

  void tick(uint32_t nowMs) {
    if (!config_) return;
    const SocialMode currentMode = config_->socialMode;
    if (currentMode != lastSocialMode_) {
      lastSocialMode_ = currentMode;
      if (currentMode == SocialMode::Extrovert) {
        crowdDimFactor_ = 1.0f;
        lastCommittedLevel_ = 100;
        pendingApply_ = true;
        pendingApplyCount_++;
      } else if (crowdDimFactor_ < 1.0f || lastCommittedLevel_ != 100) {
        pendingApply_ = true;
        pendingApplyCount_++;
      }
    }
    // Mirrors production: the roster snapshot is copied once per sample
    // period, not per tick.
    if (nowMs - lastSampleMs_ >= kSamplePeriodMs || lastSampleMs_ == 0) {
      lastSampleMs_ = nowMs;
      blePeerCache_ = nearbyOverride_;
      snapshotCount_++;
      sampleAndSmoothCrowd_(nowMs);
    }
  }

  uint8_t applyCrowdDim(uint8_t baseline) const {
    if (!config_) return baseline;
    if (config_->socialMode == SocialMode::Extrovert) return baseline;
    if (crowdDimFactor_ >= 0.999f) return baseline;
    const float scaled = static_cast<float>(baseline) * crowdDimFactor_;
    uint8_t out = static_cast<uint8_t>(scaled + 0.5f);
    if (out < 1) out = 1;
    return out;
  }

  // Greeting waveform matrix — pure read; production reads via Config.
  GreetingTuning greetingFor(const std::string& peerLampId) const {
    if (!config_)
      return profileToTuning_(150, 15, 51, 84, 0, 0, false, Easing::Smooth,
                              Easing::Smooth);
    // Empty lampId → Neutral; mirrors production guard.
    if (peerLampId.empty()) return profileForCell_(config_->socialMode, 3);
    const uint8_t disp = config_->getDisposition(peerLampId);  // unknown → 3
    return profileForCell_(config_->socialMode, disp);
  }

  bool consumePendingApply() {
    const bool was = pendingApply_;
    pendingApply_ = false;
    return was;
  }

  // Test-only inspectors
  float currentDimFactor() const { return crowdDimFactor_; }
  float currentSmoothedW() const { return smoothedW_; }
  int pendingApplyCount() const { return pendingApplyCount_; }
  int snapshotCount() const { return snapshotCount_; }

  // Production accessors mirrored for parity testing.
  float smoothedCrowdWeight() const { return smoothedW_; }

  CrowdComposition crowdComposition() const {
    CrowdComposition c;
    if (!config_) return c;
    for (const auto& p : nearbyOverride_) {
      if (p.name.empty()) continue;
      const uint8_t d = p.lampId.empty() ? 3 : config_->getDisposition(p.lampId);
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

  // Direct curve probe — pure function so tests can pin sanity points
  // without going through the sampling pipeline.
  float dimFactorForCount(float w, float floor) const {
    return dimFactorForCount_(w, floor);
  }

  // Per-mode weight probe so tests can pin the Ambivert truncation
  // (Fond/Smitten = 0) without rebuilding the smoothing pipeline.
  float weightForDisposition(uint8_t d, SocialMode mode) const {
    return weightForDisposition_(d, mode);
  }

 private:
  static GreetingTuning profileToTuning_(uint32_t total, uint32_t easeIn,
                                          uint32_t hold, uint32_t fadeOut,
                                          uint8_t pulseBack, uint8_t pulseCount,
                                          bool snub, Easing easeInCurve,
                                          Easing easeOutCurve,
                                          uint16_t breathCycle = 120,
                                          Easing breathCurve = Easing::Float) {
    GreetingTuning t;
    t.totalFrames = total;
    t.easeInFrames = easeIn;
    t.holdFrames = hold;
    t.fadeOutFrames = fadeOut;
    t.pulseBackStrength = pulseBack;
    t.pulseBackCount    = pulseCount;
    t.breathCycleFrames = breathCycle;
    t.snub              = snub;
    t.easeInCurve       = easeInCurve;
    t.easeOutCurve      = easeOutCurve;
    t.breathCurve       = breathCurve;
    return t;
  }
  static GreetingTuning profileForCell_(SocialMode mode, uint8_t disp) {
    const Easing sw = Easing::Swell, sm = Easing::Smooth, fl = Easing::Float;
    const uint8_t cont = kPulseCountContinuous;
    switch (disp) {
      case 1:  // Salty
        return profileToTuning_(540, 120, 300, 120, 191, 1, true, sm, sm);        // Snub
      case 2:  // Wary
        return profileToTuning_(570, 120, 300, 150, 165, 1, true, sm, sm);        // PartialSnub
      case 4:  // Fond
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1176, 156, 840, 180, 0, 0, false, sw, sm);            // Gentle
        if (mode == SocialMode::Ambivert)
          return profileToTuning_(1404, 144, 1020, 240, 50, cont, false, sw, sm, 300, fl); // Warm
        return profileToTuning_(1512, 132, 1080, 300, 65, cont, false, sw, sm, 240, sm);   // Enthused
      case 5:  // Smitten
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1404, 144, 1020, 240, 50, cont, false, sw, sm, 300, fl); // Warm
        if (mode == SocialMode::Ambivert)
          return profileToTuning_(1512, 132, 1080, 300, 65, cont, false, sw, sm, 240, sm); // Enthused
        return profileToTuning_(1620, 120, 1140, 360, 80, cont, false, sw, sm, 180, sm);   // Effusive
      case 3:
      default:  // Neutral / unknown
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1080, 180, 780, 120, 0, 0, false, sm, sm);  // Minimal
        return profileToTuning_(1278, 168, 960, 150, 0, 0, false, sm, sm);   // Standard
    }
  }

  static float floorForMode_(SocialMode mode) {
    switch (mode) {
      case SocialMode::Introvert: return kIntrovertFloor;
      case SocialMode::Ambivert:  return kAmbivertFloor;
      case SocialMode::Extrovert: return 1.0f;
    }
    return 1.0f;
  }

  float weightForDisposition_(uint8_t d, SocialMode mode) const {
    if (mode == SocialMode::Ambivert) {
      switch (d) {
        case 1: return 2.0f;
        case 2: return 1.5f;
        case 3: return 1.0f;
        case 4: return 0.0f;
        case 5: return 0.0f;
        default: return 1.0f;
      }
    }
    switch (d) {
      case 1: return 2.0f;
      case 2: return 1.5f;
      case 3: return 1.0f;
      case 4: return 0.5f;
      case 5: return 0.0f;
      default: return 1.0f;
    }
  }
  float computeWeightedCount_(const std::vector<RosterEntry>& peers,
                              SocialMode mode) const {
    if (!config_) return 0.0f;
    float w = 0.0f;
    for (const auto& p : peers) {
      if (p.name.empty()) continue;
      if (p.lampId.empty()) continue;
      w += weightForDisposition_(config_->getDisposition(p.lampId), mode);
    }
    return w;
  }
  float dimFactorForCount_(float w, float floor) const {
    if (w <= 0.0f) return 1.0f;
    if (floor >= 1.0f) return 1.0f;
    const float numer = std::log10(1.0f + w);
    const float denom = std::log10(1.0f + kCurveScaleW);
    const float drop  = (1.0f - floor) * (numer / denom);
    float f = 1.0f - drop;
    if (f < floor) f = floor;
    if (f > 1.0f)  f = 1.0f;
    return f;
  }
  void sampleAndSmoothCrowd_(uint32_t /*nowMs*/) {
    const SocialMode mode = config_->socialMode;
    const float rawW = computeWeightedCount_(blePeerCache_, mode);
    sampleBuf_[sampleHead_] = rawW;
    sampleHead_ = (sampleHead_ + 1) % kSampleWindow;
    if (sampleCount_ < kSampleWindow) sampleCount_++;
    float sorted[kSampleWindow];
    for (size_t i = 0; i < sampleCount_; ++i) sorted[i] = sampleBuf_[i];
    std::sort(sorted, sorted + sampleCount_);
    const float median = (sampleCount_ % 2 == 1)
        ? sorted[sampleCount_ / 2]
        : 0.5f * (sorted[sampleCount_ / 2 - 1] + sorted[sampleCount_ / 2]);
    if (!emaSeeded_) {
      smoothedW_ = median;
      emaSeeded_ = true;
    } else {
      smoothedW_ = kEmaAlpha * median + (1.0f - kEmaAlpha) * smoothedW_;
    }
    const float floor = floorForMode_(mode);
    const float targetFactor = dimFactorForCount_(smoothedW_, floor);
    const uint8_t targetLevel = static_cast<uint8_t>(targetFactor * 100.0f + 0.5f);
    const int delta = static_cast<int>(targetLevel) - static_cast<int>(lastCommittedLevel_);
    const int absDelta = delta < 0 ? -delta : delta;
    if (absDelta >= kDeadbandLevels) {
      crowdDimFactor_ = targetFactor;
      lastCommittedLevel_ = targetLevel;
      if (mode != SocialMode::Extrovert) {
        pendingApply_ = true;
        pendingApplyCount_++;
      }
    }
  }

  FakeConfig* config_ = nullptr;
  std::vector<RosterEntry> nearbyOverride_;
  std::vector<RosterEntry> blePeerCache_;
  int snapshotCount_ = 0;
  uint32_t lastSampleMs_ = 0;
  float sampleBuf_[kSampleWindow] = {0};
  size_t sampleHead_ = 0;
  size_t sampleCount_ = 0;
  float smoothedW_ = 0.0f;
  bool emaSeeded_ = false;
  float crowdDimFactor_ = 1.0f;
  uint8_t lastCommittedLevel_ = 100;
  bool pendingApply_ = false;
  int pendingApplyCount_ = 0;
  SocialMode lastSocialMode_ = SocialMode::Ambivert;
};

// --- Helpers for tests ---------------------------------------------------

// Synthesise a deterministic lampId-shaped string from any input string.
// Tests don't care about the exact bytes — only that the key is stable +
// unique per peer and shaped like an uppercase colon-hex mac.
static std::string lampIdFor(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  char buf[18];
  std::snprintf(buf, sizeof(buf), "AA:BB:%02X:%02X:%02X:%02X",
                (unsigned)((h >> 24) & 0xFF), (unsigned)((h >> 16) & 0xFF),
                (unsigned)((h >> 8)  & 0xFF), (unsigned)((h)       & 0xFF));
  return std::string(buf);
}

static std::vector<RosterEntry> peers(size_t n, uint8_t dispositionToSet,
                                     FakeConfig& cfg, const char* prefix = "p") {
  std::vector<RosterEntry> v;
  v.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    RosterEntry lamp;
    lamp.name = std::string(prefix) + std::to_string(i);
    lamp.lampId = lampIdFor(lamp.name);
    v.push_back(lamp);
    cfg.dispositions[lamp.lampId] = dispositionToSet;
  }
  return v;
}

// Run enough sample ticks to flush the median window + EMA convergence.
// Each tick advances g_nowMs by kSamplePeriodMs.
static void runSampleTicks(PersonalityEngine& eng, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    g_nowMs += PersonalityEngine::kSamplePeriodMs;
    eng.tick(g_nowMs);
  }
}

// Settle the smoother fully against a held nearby list. Returns the
// final committed dim factor.
static float settleAt(PersonalityEngine& eng, const std::vector<RosterEntry>& nearby) {
  eng.setNearbyOverride(nearby);
  runSampleTicks(eng, 40);  // way more than needed; EMA τ ≈ 6s = 6 ticks
  return eng.currentDimFactor();
}

// Reset clock + engine state between tests so each test starts clean.
static void resetEngine(PersonalityEngine& eng, FakeConfig& cfg) {
  g_nowMs = 0;
  cfg.socialMode = SocialMode::Introvert;
  cfg.dispositions.clear();
  eng = PersonalityEngine{};
  eng.begin(&cfg);
}

// --- Tests ---------------------------------------------------------------

// 1. test_crowd_weight_curve_neutral_peers — Introvert curve pinned.
void test_crowd_weight_curve_neutral_peers() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  // Pure curve probe — bypass sampling, hit dimFactorForCount directly.
  // Reference values computed from the formula
  //   factor = max(0.5, 1 - 0.5 * log10(1+W) / log10(11))
  const float intro = PersonalityEngine::kIntrovertFloor;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0000f, eng.dimFactorForCount(0.0f,  intro));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.7709f, eng.dimFactorForCount(2.0f,  intro));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6644f, eng.dimFactorForCount(4.0f,  intro));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5664f, eng.dimFactorForCount(7.0f,  intro));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5000f, eng.dimFactorForCount(10.0f, intro));
  // Past the floor — clamped, not negative.
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5000f, eng.dimFactorForCount(20.0f, intro));
}

// 2. test_crowd_weight_smitten_is_zero
void test_crowd_weight_smitten_is_zero() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  // 10 Smitten peers contribute W = 0 — no dim, in any mode.
  const float factor = settleAt(eng, peers(10, /*disposition=*/5, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.000f, factor);
  TEST_ASSERT_EQUAL_UINT8(100, eng.applyCrowdDim(100));
}

// 3. test_crowd_weight_salty_double
void test_crowd_weight_salty_double() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  // 5 Salty peers contribute W = 10 — hits the Introvert 50% floor.
  const float factor = settleAt(eng, peers(5, /*disposition=*/1, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.500f, factor);
  TEST_ASSERT_EQUAL_UINT8(50, eng.applyCrowdDim(100));
}

// 4. test_crowd_weight_mixed — Introvert weights everything; mixed crowd settles.
void test_crowd_weight_mixed() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  // 1 Salty (2.0) + 2 Wary (3.0) + 1 Neutral (1.0) + 1 Fond (0.5) + 1 Smitten (0.0)
  //   = 6.5 weighted (Introvert table)
  std::vector<RosterEntry> mix;
  auto add = [&](const std::string& n, uint8_t d) {
    RosterEntry lamp;
    lamp.name = n;
    lamp.lampId = lampIdFor(n);
    lamp.lastRssi = -50;
    mix.push_back(lamp);
    cfg.dispositions[lamp.lampId] = d;
  };
  add("salt1", 1);
  add("wary1", 2); add("wary2", 2);
  add("neut1", 3);
  add("fond1", 4);
  add("smit1", 5);
  const float factor = settleAt(eng, mix);
  // Expected: dimFactorForCount(6.5, 0.5) ≈ 0.5799; allow a little slack
  // for the deadband lag.
  TEST_ASSERT_FLOAT_WITHIN(0.025f, 0.5799f, factor);
}

// 5. test_dim_release_on_extrovert_transition — Introvert→Extrovert
// releases dim and trips pendingApply; flip back re-engages dim once
// the smoother re-converges.
void test_dim_release_on_extrovert_transition() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  // Settle a crowded state under Introvert.
  cfg.socialMode = SocialMode::Introvert;
  settleAt(eng, peers(10, 3, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, eng.currentDimFactor());
  TEST_ASSERT_EQUAL_UINT8(50, eng.applyCrowdDim(100));

  // Flip to Extrovert — engine releases dim and trips pendingApply.
  cfg.socialMode = SocialMode::Extrovert;
  (void)eng.consumePendingApply();
  g_nowMs += 1;
  eng.tick(g_nowMs);
  TEST_ASSERT_TRUE(eng.consumePendingApply());
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, eng.currentDimFactor());
  TEST_ASSERT_EQUAL_UINT8(100, eng.applyCrowdDim(100));

  // Flip back to Introvert with the same crowd present — dim re-engages
  // after the smoother re-converges.
  cfg.socialMode = SocialMode::Introvert;
  runSampleTicks(eng, 30);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, eng.currentDimFactor());
  TEST_ASSERT_EQUAL_UINT8(50, eng.applyCrowdDim(100));
}

// 5b. test_dim_introvert_to_ambivert_transition_trips_pending_apply —
// crossing the floor (0.5 → 0.7) is a regime change; pendingApply trips.
void test_dim_introvert_to_ambivert_transition_trips_pending_apply() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  cfg.socialMode = SocialMode::Introvert;
  settleAt(eng, peers(10, 3, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, eng.currentDimFactor());
  (void)eng.consumePendingApply();

  cfg.socialMode = SocialMode::Ambivert;
  g_nowMs += 1;
  eng.tick(g_nowMs);
  TEST_ASSERT_TRUE(eng.consumePendingApply());
}

// 6. test_dim_deadband_absorbs_alternation
void test_dim_deadband_absorbs_alternation() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  // Settle at a stable weighted count first (3 neutrals).
  settleAt(eng, peers(3, 3, cfg));
  const int baselineApplies = eng.pendingApplyCount();
  // Drain accumulated pendingApply flag.
  (void)eng.consumePendingApply();

  // Alternate snapshots between W=3 and W=4 (Neutrals). The median of a
  // [3,4,3,4] window is 3.5 → smoothed monotonically settles somewhere
  // between the two without crossing the deadband repeatedly.
  for (int i = 0; i < 10; ++i) {
    eng.setNearbyOverride(peers((i % 2) == 0 ? 3 : 4, 3, cfg));
    g_nowMs += PersonalityEngine::kSamplePeriodMs;
    eng.tick(g_nowMs);
  }
  const int alternationApplies = eng.pendingApplyCount() - baselineApplies;
  TEST_ASSERT_LESS_OR_EQUAL_INT(2, alternationApplies);
}

// 7. test_dim_ema_step_response
void test_dim_ema_step_response() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  // Cold start with no peers — first sample commits nothing significant
  // (factor 1.0 == default). Then step to 8 neutrals.
  settleAt(eng, {});
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, eng.currentDimFactor());
  (void)eng.consumePendingApply();

  // Step to 8 neutrals — dimFactorForCount(8, 0.5) ≈ 0.5418 at convergence.
  eng.setNearbyOverride(peers(8, 3, cfg));
  runSampleTicks(eng, 40);
  TEST_ASSERT_FLOAT_WITHIN(0.025f, 0.5418f, eng.currentDimFactor());
  TEST_ASSERT_GREATER_THAN_INT(0, eng.pendingApplyCount());
}

// 7c. test_brightness_multiplier_identity_in_extrovert —
// applyCrowdDim is pure identity in Extrovert regardless of internal state.
void test_brightness_multiplier_identity_in_extrovert() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Extrovert;
  TEST_ASSERT_EQUAL_UINT8(100, eng.applyCrowdDim(100));
  TEST_ASSERT_EQUAL_UINT8(42, eng.applyCrowdDim(42));
}

// --- Per-mode dim algorithm tests ---------------------------------------

// test_crowd_dim_ambivert_uses_70_percent_floor — W=10 under Ambivert
// lands at 0.7 (the Ambivert floor), not 0.5.
void test_crowd_dim_ambivert_uses_70_percent_floor() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Ambivert;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.7f,
                           eng.dimFactorForCount(10.0f,
                                                  PersonalityEngine::kAmbivertFloor));
  // 5 Salty peers contribute W = 10 (Salty weight = 2.0 in both modes) —
  // converges to the 0.7 floor under Ambivert.
  const float factor = settleAt(eng, peers(5, /*disposition=*/1, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.7f, factor);
  TEST_ASSERT_EQUAL_UINT8(70, eng.applyCrowdDim(100));
}

// test_crowd_dim_ambivert_ignores_fond_smitten — all-Fond crowd contributes
// W=0 under Ambivert, no dim. Mirror: all-Smitten is also W=0.
void test_crowd_dim_ambivert_ignores_fond_smitten() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Ambivert;
  // Direct probe of the weight table.
  TEST_ASSERT_EQUAL_FLOAT(0.0f, eng.weightForDisposition(4, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_FLOAT(0.0f, eng.weightForDisposition(5, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_FLOAT(1.0f, eng.weightForDisposition(3, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_FLOAT(1.5f, eng.weightForDisposition(2, SocialMode::Ambivert));
  TEST_ASSERT_EQUAL_FLOAT(2.0f, eng.weightForDisposition(1, SocialMode::Ambivert));
  // Integration: a crowd of 10 Fond peers → no dim under Ambivert.
  const float factor = settleAt(eng, peers(10, /*disposition=*/4, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, factor);
  TEST_ASSERT_EQUAL_UINT8(100, eng.applyCrowdDim(100));
}

// test_crowd_dim_extrovert_returns_baseline — no matter how many peers,
// Extrovert mode applyCrowdDim(baseline) == baseline.
void test_crowd_dim_extrovert_returns_baseline() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Extrovert;
  // Saturate the smoother with 20 Salty peers (W=40 well past the curve).
  settleAt(eng, peers(20, /*disposition=*/1, cfg));
  TEST_ASSERT_EQUAL_UINT8(100, eng.applyCrowdDim(100));
  TEST_ASSERT_EQUAL_UINT8(255, eng.applyCrowdDim(255));
  TEST_ASSERT_EQUAL_UINT8(1, eng.applyCrowdDim(1));
}

// test_crowd_dim_introvert_unchanged_curve — pin the Introvert curve
// values (W=10 → 0.5, W=5 → ~0.626) so the floor refactor doesn't
// accidentally regress Introvert behavior.
void test_crowd_dim_introvert_unchanged_curve() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Introvert;
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f,
                           eng.dimFactorForCount(0.0f, PersonalityEngine::kIntrovertFloor));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6264f,
                           eng.dimFactorForCount(5.0f, PersonalityEngine::kIntrovertFloor));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f,
                           eng.dimFactorForCount(10.0f, PersonalityEngine::kIntrovertFloor));
  // Integration: 5 Salty peers (W=10) → 0.5 under Introvert.
  const float factor = settleAt(eng, peers(5, /*disposition=*/1, cfg));
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.5f, factor);
  TEST_ASSERT_EQUAL_UINT8(50, eng.applyCrowdDim(100));
}

// 15. test_greeting_tuning_matrix — table-driven across all 15 cells.
void test_greeting_tuning_matrix() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  struct Row {
    SocialMode mode;
    uint8_t disposition;
    uint32_t expectedTotal;
    uint8_t  expectedPulseBack;
    uint8_t  expectedPulseCount;
    bool     expectedSnub;
  };
  const Row rows[] = {
    // Salty — Snub in every mode
    {SocialMode::Introvert, 1,  540, 191, 1, true},
    {SocialMode::Ambivert,  1,  540, 191, 1, true},
    {SocialMode::Extrovert, 1,  540, 191, 1, true},
    // Wary — PartialSnub in every mode
    {SocialMode::Introvert, 2,  570, 165, 1, true},
    {SocialMode::Ambivert,  2,  570, 165, 1, true},
    {SocialMode::Extrovert, 2,  570, 165, 1, true},
    // Neutral
    {SocialMode::Introvert, 3, 1080,   0, 0, false},
    {SocialMode::Ambivert,  3, 1278,   0, 0, false},
    {SocialMode::Extrovert, 3, 1278,   0, 0, false},
    // Fond — warm greetings breathe continuously
    {SocialMode::Introvert, 4, 1176,   0, 0, false},
    {SocialMode::Ambivert,  4, 1404,  50, kPulseCountContinuous, false},
    {SocialMode::Extrovert, 4, 1512,  65, kPulseCountContinuous, false},
    // Smitten
    {SocialMode::Introvert, 5, 1404,  50, kPulseCountContinuous, false},
    {SocialMode::Ambivert,  5, 1512,  65, kPulseCountContinuous, false},
    {SocialMode::Extrovert, 5, 1620,  80, kPulseCountContinuous, false},
  };

  const std::string peerAddr = lampIdFor("peer");
  cfg.dispositions[peerAddr] = 3;  // placeholder; we'll set per-row
  for (const auto& r : rows) {
    cfg.socialMode = r.mode;
    cfg.dispositions[peerAddr] = r.disposition;
    const GreetingTuning t = eng.greetingFor(peerAddr);
    TEST_ASSERT_EQUAL_UINT32(r.expectedTotal, t.totalFrames);
    TEST_ASSERT_EQUAL_UINT8(r.expectedPulseBack, t.pulseBackStrength);
    TEST_ASSERT_EQUAL_UINT8(r.expectedPulseCount, t.pulseBackCount);
    TEST_ASSERT_EQUAL(r.expectedSnub, t.snub);
    // No greeting blinks or snaps: >= 5s hold, >= 2s ramps at ~60 fps.
    TEST_ASSERT_TRUE(t.holdFrames >= 300);
    TEST_ASSERT_TRUE(t.easeInFrames >= 120);
    TEST_ASSERT_TRUE(t.fadeOutFrames >= 120);
    // total stays consistent with the phase sum.
    TEST_ASSERT_EQUAL_UINT32(t.easeInFrames + t.holdFrames + t.fadeOutFrames,
                             t.totalFrames);
  }
}

// 15b. test_greeting_curve_per_disposition — warm greetings swell in and
// smooth out; neutral and snub ramps are Smooth both ways.
void test_greeting_curve_per_disposition() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string peerAddr = lampIdFor("peer");
  auto curve = [&](SocialMode mode, uint8_t disp) {
    cfg.socialMode = mode;
    cfg.dispositions[peerAddr] = disp;
    return eng.greetingFor(peerAddr);
  };
  // Warm family: Swell in, Smooth out.
  for (auto cell : {curve(SocialMode::Ambivert, 4), curve(SocialMode::Extrovert, 5),
                    curve(SocialMode::Introvert, 4)}) {
    TEST_ASSERT_EQUAL(Easing::Swell, cell.easeInCurve);
    TEST_ASSERT_EQUAL(Easing::Smooth, cell.easeOutCurve);
  }
  // Neutral + snubs: Smooth both ways (dismissiveness rides the dim floor).
  for (auto cell : {curve(SocialMode::Ambivert, 3), curve(SocialMode::Ambivert, 1),
                    curve(SocialMode::Extrovert, 2)}) {
    TEST_ASSERT_EQUAL(Easing::Smooth, cell.easeInCurve);
    TEST_ASSERT_EQUAL(Easing::Smooth, cell.easeOutCurve);
  }
}

// 16. test_greeting_for_salty_is_snub_in_all_modes — Salty produces a
// Snub waveform in every mode that dims to a deep floor (~16% brightness)
// and lingers >= 5s.
void test_greeting_for_salty_is_snub_in_all_modes() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string nemesisAddr = lampIdFor("nemesis");
  cfg.dispositions[nemesisAddr] = 1;

  cfg.socialMode = SocialMode::Introvert;
  GreetingTuning t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(540, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(191, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
  TEST_ASSERT_TRUE(t.holdFrames >= 300);

  cfg.socialMode = SocialMode::Ambivert;
  t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(540, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(191, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
  TEST_ASSERT_TRUE(t.holdFrames >= 300);

  cfg.socialMode = SocialMode::Extrovert;
  t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(540, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(191, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
  TEST_ASSERT_TRUE(t.holdFrames >= 300);
}

// 16b. test_greeting_for_wary_is_partial_snub_in_all_modes — pulseBackStrength=165.
void test_greeting_for_wary_is_partial_snub_in_all_modes() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string acqAddr = lampIdFor("acquaintance");
  cfg.dispositions[acqAddr] = 2;
  for (auto mode : {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert}) {
    cfg.socialMode = mode;
    const GreetingTuning t = eng.greetingFor(acqAddr);
    TEST_ASSERT_EQUAL_UINT8(165, t.pulseBackStrength);
    TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
    TEST_ASSERT_TRUE(t.holdFrames >= 300);
  }
}

// 16c. test_greeting_for_smitten_extrovert_is_effusive_continuous
void test_greeting_for_smitten_extrovert_is_effusive_continuous() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string crushAddr = lampIdFor("crush");
  cfg.dispositions[crushAddr] = 5;
  cfg.socialMode = SocialMode::Extrovert;
  const GreetingTuning t = eng.greetingFor(crushAddr);
  TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, t.pulseBackCount);
  TEST_ASSERT_EQUAL_UINT8(80, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT16(180, t.breathCycleFrames);
  TEST_ASSERT_EQUAL(Easing::Smooth, t.breathCurve);
  TEST_ASSERT_EQUAL_UINT32(1620, t.totalFrames);
}

// 16d. test_greeting_for_smitten_ambivert_is_enthused_continuous
void test_greeting_for_smitten_ambivert_is_enthused_continuous() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string crushAddr = lampIdFor("crush");
  cfg.dispositions[crushAddr] = 5;
  cfg.socialMode = SocialMode::Ambivert;
  const GreetingTuning t = eng.greetingFor(crushAddr);
  TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, t.pulseBackCount);
  TEST_ASSERT_EQUAL_UINT8(65, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT16(240, t.breathCycleFrames);
  TEST_ASSERT_EQUAL(Easing::Smooth, t.breathCurve);
  TEST_ASSERT_EQUAL_UINT32(1512, t.totalFrames);
}

// 16e. test_greeting_profile_easeins_invert_with_warmth
// Design intent: among the warm/neutral greeting profiles, a warmer
// relationship eases in FASTER (smaller easeIn) — eager hello. Snubs are
// a separate gesture (uniform snappy ease-in) and aren't part of this
// gradient.
void test_greeting_profile_easeins_invert_with_warmth() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string peerAddr = lampIdFor("peer");
  auto easeFor = [&](SocialMode mode, uint8_t disp) -> uint32_t {
    cfg.socialMode = mode;
    cfg.dispositions[peerAddr] = disp;
    return eng.greetingFor(peerAddr).easeInFrames;
  };
  const uint32_t minimal  = easeFor(SocialMode::Introvert, 3);
  const uint32_t gentle   = easeFor(SocialMode::Introvert, 4);
  const uint32_t standard = easeFor(SocialMode::Ambivert,  3);
  const uint32_t warm     = easeFor(SocialMode::Introvert, 5);
  const uint32_t enthused = easeFor(SocialMode::Ambivert,  5);
  const uint32_t effusive = easeFor(SocialMode::Extrovert, 5);
  const uint32_t snub     = easeFor(SocialMode::Ambivert,  1);  // Salty → Snub
  // Warmth gradient: warmest pops in fastest, coldest neutral is slowest.
  TEST_ASSERT_TRUE(effusive < enthused);
  TEST_ASSERT_TRUE(enthused < warm);
  TEST_ASSERT_TRUE(warm     < standard);
  TEST_ASSERT_TRUE(standard < minimal);
  // Gentle (Introvert+Fond): warmer disposition than Standard but a less
  // eager personality, so it lands between Warm and Standard.
  TEST_ASSERT_TRUE(warm   < gentle);
  TEST_ASSERT_TRUE(gentle <= standard);
  // Snubs ease in snappily — faster than even the most hesitant neutral
  // greeting (Minimal), so a dismissal reads as curt, not reluctant.
  TEST_ASSERT_TRUE(snub < minimal);
}

// 17. test_greeting_for_unknown_peer_is_neutral — peer not in store
// defaults to the current mode's Neutral row.
void test_greeting_for_unknown_peer_is_neutral() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Ambivert;
  const GreetingTuning t = eng.greetingFor(lampIdFor("stranger"));
  // Ambivert × Neutral → standard (1278, 168, 960, 150, 0, 0) — the anchor.
  TEST_ASSERT_EQUAL_UINT32(1278, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(168, t.easeInFrames);
  TEST_ASSERT_EQUAL_UINT32(960, t.holdFrames);
  TEST_ASSERT_EQUAL_UINT32(150, t.fadeOutFrames);
  TEST_ASSERT_EQUAL_UINT8(0, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(0, t.pulseBackCount);
}

// --- Smoothed-W + crowd composition accessor tests ----------------------

void test_smoothed_crowd_weight_matches_internal() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  cfg.socialMode = SocialMode::Introvert;

  // 3 Salty peers — raw W = 3 × 2.0 = 6.0 every sample. Median holds at
  // 6.0, EMA seeds to 6.0 on first sample and stays there since every
  // subsequent input is also 6.0.
  eng.setNearbyOverride(peers(3, /*disposition=*/1, cfg));
  runSampleTicks(eng, 5);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 6.0f, eng.smoothedCrowdWeight());
  // Production accessor mirrors the test-only inspector.
  TEST_ASSERT_EQUAL_FLOAT(eng.currentSmoothedW(), eng.smoothedCrowdWeight());
}

void test_crowd_composition_counts_by_disposition() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  std::vector<RosterEntry> mix;
  auto add = [&](const std::string& n, uint8_t d) {
    RosterEntry lamp;
    lamp.name = n;
    lamp.lampId = lampIdFor(n);
    lamp.lastRssi = -50;
    mix.push_back(lamp);
    cfg.dispositions[lamp.lampId] = d;
  };
  add("salt1", 1);
  add("wary1", 2); add("wary2", 2);
  add("smit1", 5);

  eng.setNearbyOverride(mix);
  runSampleTicks(eng, 1);
  const CrowdComposition c = eng.crowdComposition();
  TEST_ASSERT_EQUAL_UINT8(1, c.salty);
  TEST_ASSERT_EQUAL_UINT8(2, c.wary);
  TEST_ASSERT_EQUAL_UINT8(0, c.neutral);
  TEST_ASSERT_EQUAL_UINT8(0, c.fond);
  TEST_ASSERT_EQUAL_UINT8(1, c.smitten);
}

void test_crowd_composition_empty_when_no_peers() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.setNearbyOverride({});
  const CrowdComposition c = eng.crowdComposition();
  TEST_ASSERT_EQUAL_UINT8(0, c.salty);
  TEST_ASSERT_EQUAL_UINT8(0, c.wary);
  TEST_ASSERT_EQUAL_UINT8(0, c.neutral);
  TEST_ASSERT_EQUAL_UINT8(0, c.fond);
  TEST_ASSERT_EQUAL_UINT8(0, c.smitten);
}

void test_crowd_composition_unknown_peer_counts_as_neutral() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);

  // Peer in snapshot but absent from dispositions map → unknown → Neutral.
  RosterEntry p;
  p.name = "stranger";
  p.lampId = lampIdFor("stranger");
  p.lastRssi = -50;
  eng.setNearbyOverride({p});
  // Intentionally do NOT set cfg.dispositions[p.lampId].

  const CrowdComposition c = eng.crowdComposition();
  TEST_ASSERT_EQUAL_UINT8(0, c.salty);
  TEST_ASSERT_EQUAL_UINT8(0, c.wary);
  TEST_ASSERT_EQUAL_UINT8(1, c.neutral);
  TEST_ASSERT_EQUAL_UINT8(0, c.fond);
  TEST_ASSERT_EQUAL_UINT8(0, c.smitten);
}

}  // namespace test

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test::test_crowd_weight_curve_neutral_peers);
  RUN_TEST(test::test_crowd_weight_smitten_is_zero);
  RUN_TEST(test::test_crowd_weight_salty_double);
  RUN_TEST(test::test_crowd_weight_mixed);
  RUN_TEST(test::test_dim_release_on_extrovert_transition);
  RUN_TEST(test::test_dim_introvert_to_ambivert_transition_trips_pending_apply);
  RUN_TEST(test::test_dim_deadband_absorbs_alternation);
  RUN_TEST(test::test_dim_ema_step_response);
  RUN_TEST(test::test_brightness_multiplier_identity_in_extrovert);
  RUN_TEST(test::test_crowd_dim_ambivert_uses_70_percent_floor);
  RUN_TEST(test::test_crowd_dim_ambivert_ignores_fond_smitten);
  RUN_TEST(test::test_crowd_dim_extrovert_returns_baseline);
  RUN_TEST(test::test_crowd_dim_introvert_unchanged_curve);
  RUN_TEST(test::test_greeting_tuning_matrix);
  RUN_TEST(test::test_greeting_curve_per_disposition);
  RUN_TEST(test::test_greeting_for_salty_is_snub_in_all_modes);
  RUN_TEST(test::test_greeting_for_wary_is_partial_snub_in_all_modes);
  RUN_TEST(test::test_greeting_for_smitten_extrovert_is_effusive_continuous);
  RUN_TEST(test::test_greeting_for_smitten_ambivert_is_enthused_continuous);
  RUN_TEST(test::test_greeting_profile_easeins_invert_with_warmth);
  RUN_TEST(test::test_greeting_for_unknown_peer_is_neutral);
  RUN_TEST(test::test_smoothed_crowd_weight_matches_internal);
  RUN_TEST(test::test_crowd_composition_counts_by_disposition);
  RUN_TEST(test::test_crowd_composition_empty_when_no_peers);
  RUN_TEST(test::test_crowd_composition_unknown_peer_counts_as_neutral);

  return UNITY_END();
}

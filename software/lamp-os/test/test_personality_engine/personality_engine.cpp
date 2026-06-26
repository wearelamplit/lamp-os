// Native-host unit tests for PersonalityEngine. Mirrors the production
// engine inline (same convention as test_transient_override) so the
// native rig doesn't need to link the firmware. Each test pins the
// observable contract (curve shape, hysteresis behavior, mode-flip
// release, greeting matrix, closest-Smitten cycle) so production
// refactors are free as long as the contract holds.

#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace test {

// --- Time scaffolding -----------------------------------------------------
static uint32_t g_nowMs = 0;
static uint32_t mockMillis() { return g_nowMs; }

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

struct NearbyLamp {
  std::string name;
  std::string bdAddr;
  Color baseColor;
  int8_t lastRssi = -127;
};

// Mock ExpressionManager for the closest-Smitten pulse tests. Records
// every call so assertions can pin the contract.

struct InvocationRecord {
  std::string type;
  Color color;
  uint32_t timeMs = 0;
};

class MockExpressionManager {
 public:
  std::vector<InvocationRecord> invocations;
  void triggerPulse(const Color& color) {
    invocations.push_back({"pulse", color, mockMillis()});
  }
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
  bool     snub              = false;
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
  // Mirrors production: key is BD_ADDR (uppercase colon-hex). The map
  // doesn't care what the key is, but tests now uniformly key by bdAddr.
  std::map<std::string, uint8_t> dispositions;
  uint8_t getDisposition(const std::string& bdAddr) const {
    auto it = dispositions.find(bdAddr);
    return it == dispositions.end() ? /*kDispositionDefault=*/3 : it->second;
  }
};

// --- PersonalityEngine inline mirror -------------------------------------
//
// Mirrors the surface that effectiveBrightness() / loop() in lamp.cpp
// consumes, plus the greeting matrix and closest-Smitten cycle exercised
// by the tests below.
class PersonalityEngine {
 public:
  static constexpr float kIntrovertFloor = 0.5f;
  static constexpr float kAmbivertFloor  = 0.7f;
  static constexpr float kCurveScaleW = 10.0f;
  static constexpr uint32_t kSamplePeriodMs = 1000;
  static constexpr size_t kSampleWindow = 4;
  static constexpr float kEmaAlpha = 0.15f;
  static constexpr uint8_t kDeadbandLevels = 2;

  void begin(FakeConfig* config,
             MockExpressionManager* expressionManager = nullptr) {
    config_ = config;
    expressionManager_ = expressionManager;
    if (config_) lastSocialMode_ = config_->socialMode;
  }

  void setNearbyOverride(std::vector<NearbyLamp> peers) {
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
    tickClosestSmittenPulse_(nowMs);
    if (nowMs - lastSampleMs_ >= kSamplePeriodMs || lastSampleMs_ == 0) {
      lastSampleMs_ = nowMs;
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
  GreetingTuning greetingFor(const std::string& peerBdAddr) const {
    if (!config_) return profileToTuning_(150, 15, 51, 84, 0, 0);
    // Empty bdAddr → Neutral; mirrors production guard.
    if (peerBdAddr.empty()) return profileForCell_(config_->socialMode, 3);
    const uint8_t disp = config_->getDisposition(peerBdAddr);  // unknown → 3
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

  // Production accessors mirrored for parity testing.
  float smoothedCrowdWeight() const { return smoothedW_; }

  CrowdComposition crowdComposition() const {
    CrowdComposition c;
    if (!config_) return c;
    for (const auto& p : nearbyOverride_) {
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
                                          bool snub = false) {
    GreetingTuning t;
    t.totalFrames = total;
    t.easeInFrames = easeIn;
    t.holdFrames = hold;
    t.fadeOutFrames = fadeOut;
    t.pulseBackStrength = pulseBack;
    t.pulseBackCount    = pulseCount;
    t.snub              = snub;
    return t;
  }
  static GreetingTuning profileForCell_(SocialMode mode, uint8_t disp) {
    switch (disp) {
      case 1:  // Salty
        if (mode == SocialMode::Extrovert)
          return profileToTuning_(330, 60, 180, 90, 255, 1, true);   // SnubQuick
        return profileToTuning_(270, 60, 150, 60, 255, 1, true);     // Snub
      case 2:  // Wary
        if (mode == SocialMode::Extrovert)
          return profileToTuning_(390, 60, 240, 90, 128, 1, true);   // PartialSnubQuick
        return profileToTuning_(360, 60, 210, 90, 128, 1, true);     // PartialSnub
      case 4:  // Fond
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1110, 120, 840, 150, 0, 0);        // Gentle
        if (mode == SocialMode::Ambivert)
          return profileToTuning_(1290, 90, 1020, 180, 100, 1);      // Warm
        return profileToTuning_(1350, 60, 1080, 210, 128, 2);        // Enthused
      case 5:  // Smitten
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1290, 90, 1020, 180, 100, 1);      // Warm
        if (mode == SocialMode::Ambivert)
          return profileToTuning_(1350, 60, 1080, 210, 128, 2);      // Enthused
        return profileToTuning_(1425, 45, 1140, 240, 153,
                                kPulseCountContinuous);               // Effusive
      case 3:
      default:  // Neutral / unknown
        if (mode == SocialMode::Introvert)
          return profileToTuning_(1020, 150, 780, 90, 0, 0);         // Minimal
        return profileToTuning_(1200, 120, 960, 120, 0, 0);          // Standard
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
  float computeWeightedCount_(const std::vector<NearbyLamp>& peers,
                              SocialMode mode) const {
    if (!config_) return 0.0f;
    float w = 0.0f;
    for (const auto& p : peers) {
      if (p.name.empty()) continue;
      if (p.bdAddr.empty()) continue;
      w += weightForDisposition_(config_->getDisposition(p.bdAddr), mode);
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
    const float rawW = computeWeightedCount_(nearbyOverride_, mode);
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

  // --- Closest-Smitten pulse cycle --------------------------------------

  void firePulse_(const Color& color) {
    if (expressionManager_) expressionManager_->triggerPulse(color);
  }

  void tickClosestSmittenPulse_(uint32_t nowMs) {
    if (!config_) return;
    // Sort a local copy by RSSI desc (mirrors production
    // getReachableViaBle behavior). nearbyOverride_ stays in test-author
    // order so test setup doesn't need to know about the sort.
    std::vector<NearbyLamp> sorted = nearbyOverride_;
    std::stable_sort(sorted.begin(), sorted.end(),
                      [](const NearbyLamp& a, const NearbyLamp& b) {
                        return a.lastRssi > b.lastRssi;
                      });
    const NearbyLamp* closest = nullptr;
    for (const auto& p : sorted) {
      if (!p.name.empty()) { closest = &p; break; }
    }
    if (!closest) {
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
      closestSmittenName_.clear();
      lastClosestPulseMs_ = 0;
      return;
    }
    if (closest->name != closestSmittenName_) {
      // Hysteresis (mirrors production): if the previous closest is still
      // visible, only swap when the new closest is ≥kRssiHysteresisDb
      // dBm stronger. Stops noise-driven flapping.
      if (!closestSmittenName_.empty()) {
        const NearbyLamp* prev = nullptr;
        for (const auto& p : sorted) {
          if (p.name == closestSmittenName_) { prev = &p; break; }
        }
        if (prev != nullptr) {
          const int delta = static_cast<int>(closest->lastRssi) -
                            static_cast<int>(prev->lastRssi);
          if (delta < static_cast<int>(kRssiHysteresisDb)) return;
        }
      }
      closestSmittenName_ = closest->name;
      lastClosestPulseMs_ = nowMs;
      firePulse_(closest->baseColor);
      return;
    }
    if (nowMs - lastClosestPulseMs_ >= kClosestPulsePeriodMs) {
      lastClosestPulseMs_ = nowMs;
      firePulse_(closest->baseColor);
    }
  }

  // Constants (mirror production).
  static constexpr uint32_t kClosestPulsePeriodMs = 45000;
  static constexpr uint8_t  kRssiHysteresisDb     = 3;

  FakeConfig* config_ = nullptr;
  MockExpressionManager*  expressionManager_  = nullptr;
  std::vector<NearbyLamp> nearbyOverride_;
  std::string closestSmittenName_;
  uint32_t lastClosestPulseMs_ = 0;
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

// Synthesise a deterministic BD_ADDR-shaped string from any input string.
// Tests don't care about the exact bytes — only that the key is stable +
// unique per peer and shaped like an uppercase colon-hex BD_ADDR.
static std::string bdAddrFor(const std::string& s) {
  uint32_t h = 2166136261u;
  for (unsigned char c : s) { h ^= c; h *= 16777619u; }
  char buf[18];
  std::snprintf(buf, sizeof(buf), "AA:BB:%02X:%02X:%02X:%02X",
                (unsigned)((h >> 24) & 0xFF), (unsigned)((h >> 16) & 0xFF),
                (unsigned)((h >> 8)  & 0xFF), (unsigned)((h)       & 0xFF));
  return std::string(buf);
}

static std::vector<NearbyLamp> peers(size_t n, uint8_t dispositionToSet,
                                     FakeConfig& cfg, const char* prefix = "p") {
  std::vector<NearbyLamp> v;
  v.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    NearbyLamp lamp;
    lamp.name = std::string(prefix) + std::to_string(i);
    lamp.bdAddr = bdAddrFor(lamp.name);
    v.push_back(lamp);
    cfg.dispositions[lamp.bdAddr] = dispositionToSet;
  }
  return v;
}

// Build a single named peer with color + RSSI (for closest tests).
static NearbyLamp makePeer(const char* name, uint8_t disp, FakeConfig& cfg,
                           Color color = Color(255, 0, 0, 0),
                           int8_t rssi = -50) {
  NearbyLamp p;
  p.name = name;
  p.bdAddr = bdAddrFor(name);
  p.baseColor = color;
  p.lastRssi = rssi;
  cfg.dispositions[p.bdAddr] = disp;
  return p;
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
static float settleAt(PersonalityEngine& eng, const std::vector<NearbyLamp>& nearby) {
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
  std::vector<NearbyLamp> mix;
  auto add = [&](const std::string& n, uint8_t d) {
    NearbyLamp lamp;
    lamp.name = n;
    lamp.bdAddr = bdAddrFor(n);
    lamp.lastRssi = -50;
    mix.push_back(lamp);
    cfg.dispositions[lamp.bdAddr] = d;
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
    // Salty — Snub (Introvert/Ambivert) or SnubQuick (Extrovert)
    {SocialMode::Introvert, 1,  270, 255, 1, true},
    {SocialMode::Ambivert,  1,  270, 255, 1, true},
    {SocialMode::Extrovert, 1,  330, 255, 1, true},
    // Wary — PartialSnub (Introvert/Ambivert) or PartialSnubQuick (Extrovert)
    {SocialMode::Introvert, 2,  360, 128, 1, true},
    {SocialMode::Ambivert,  2,  360, 128, 1, true},
    {SocialMode::Extrovert, 2,  390, 128, 1, true},
    // Neutral
    {SocialMode::Introvert, 3, 1020,   0, 0, false},
    {SocialMode::Ambivert,  3, 1200,   0, 0, false},
    {SocialMode::Extrovert, 3, 1200,   0, 0, false},
    // Fond
    {SocialMode::Introvert, 4, 1110,   0, 0, false},
    {SocialMode::Ambivert,  4, 1290, 100, 1, false},
    {SocialMode::Extrovert, 4, 1350, 128, 2, false},
    // Smitten
    {SocialMode::Introvert, 5, 1290, 100, 1, false},
    {SocialMode::Ambivert,  5, 1350, 128, 2, false},
    {SocialMode::Extrovert, 5, 1425, 153, kPulseCountContinuous, false},
  };

  const std::string peerAddr = bdAddrFor("peer");
  cfg.dispositions[peerAddr] = 3;  // placeholder; we'll set per-row
  for (const auto& r : rows) {
    cfg.socialMode = r.mode;
    cfg.dispositions[peerAddr] = r.disposition;
    const GreetingTuning t = eng.greetingFor(peerAddr);
    TEST_ASSERT_EQUAL_UINT32(r.expectedTotal, t.totalFrames);
    TEST_ASSERT_EQUAL_UINT8(r.expectedPulseBack, t.pulseBackStrength);
    TEST_ASSERT_EQUAL_UINT8(r.expectedPulseCount, t.pulseBackCount);
    TEST_ASSERT_EQUAL(r.expectedSnub, t.snub);
  }
}

// 16. test_greeting_for_salty_is_snub_in_all_modes — Salty produces a
// full-dim Snub waveform (or SnubQuick under Extrovert).
void test_greeting_for_salty_is_snub_in_all_modes() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string nemesisAddr = bdAddrFor("nemesis");
  cfg.dispositions[nemesisAddr] = 1;

  cfg.socialMode = SocialMode::Introvert;
  GreetingTuning t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(270, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(255, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);

  cfg.socialMode = SocialMode::Ambivert;
  t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(270, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(255, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);

  cfg.socialMode = SocialMode::Extrovert;
  t = eng.greetingFor(nemesisAddr);
  TEST_ASSERT_EQUAL_UINT32(330, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT8(255, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
}

// 16b. test_greeting_for_wary_is_partial_snub_in_all_modes — pulseBackStrength=128.
void test_greeting_for_wary_is_partial_snub_in_all_modes() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string acqAddr = bdAddrFor("acquaintance");
  cfg.dispositions[acqAddr] = 2;
  for (auto mode : {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert}) {
    cfg.socialMode = mode;
    const GreetingTuning t = eng.greetingFor(acqAddr);
    TEST_ASSERT_EQUAL_UINT8(128, t.pulseBackStrength);
    TEST_ASSERT_EQUAL_UINT8(1, t.pulseBackCount);
  }
}

// 16c. test_greeting_for_smitten_extrovert_is_effusive_continuous
void test_greeting_for_smitten_extrovert_is_effusive_continuous() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string crushAddr = bdAddrFor("crush");
  cfg.dispositions[crushAddr] = 5;
  cfg.socialMode = SocialMode::Extrovert;
  const GreetingTuning t = eng.greetingFor(crushAddr);
  TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, t.pulseBackCount);
  TEST_ASSERT_EQUAL_UINT32(1425, t.totalFrames);
}

// 16d. test_greeting_for_smitten_ambivert_is_enthused_two_pulses
void test_greeting_for_smitten_ambivert_is_enthused_two_pulses() {
  FakeConfig cfg;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  const std::string crushAddr = bdAddrFor("crush");
  cfg.dispositions[crushAddr] = 5;
  cfg.socialMode = SocialMode::Ambivert;
  const GreetingTuning t = eng.greetingFor(crushAddr);
  TEST_ASSERT_EQUAL_UINT8(2, t.pulseBackCount);
  TEST_ASSERT_EQUAL_UINT32(1350, t.totalFrames);
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
  const std::string peerAddr = bdAddrFor("peer");
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
  const GreetingTuning t = eng.greetingFor(bdAddrFor("stranger"));
  // Ambivert × Neutral → standard (1200, 120, 960, 120, 0, 0) — the anchor.
  TEST_ASSERT_EQUAL_UINT32(1200, t.totalFrames);
  TEST_ASSERT_EQUAL_UINT32(120, t.easeInFrames);
  TEST_ASSERT_EQUAL_UINT32(960, t.holdFrames);
  TEST_ASSERT_EQUAL_UINT32(120, t.fadeOutFrames);
  TEST_ASSERT_EQUAL_UINT8(0, t.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(0, t.pulseBackCount);
}

// --- Closest-Smitten cycle tests ----------------------------------------

// test_smitten_closest_pulse_fires_on_transition_and_every_45s
void test_smitten_closest_pulse_fires_on_transition_and_every_45s() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;

  // Single Smitten peer = the closest. First tick: closest-transition
  // fires 1 pulse.
  eng.setNearbyOverride({makePeer("crush", 5, cfg, Color(200, 0, 200, 0), -40)});
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());

  // 30s < 45s closest cadence — no new pulses expected.
  for (int t = 0; t < 30; ++t) {
    g_nowMs += 1000;
    eng.tick(g_nowMs);
  }
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());

  // Advance another 16s (total 46s since the transition). Cadence fires.
  for (int t = 0; t < 16; ++t) {
    g_nowMs += 1000;
    eng.tick(g_nowMs);
  }
  TEST_ASSERT_EQUAL_INT(2, (int)em.invocations.size());

  // Another 45s of ticks → another pulse.
  for (int t = 0; t < 45; ++t) {
    g_nowMs += 1000;
    eng.tick(g_nowMs);
  }
  TEST_ASSERT_EQUAL_INT(3, (int)em.invocations.size());
}

// test_smitten_closest_pulse_resets_when_closest_changes
void test_smitten_closest_pulse_resets_when_closest_changes() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;

  // Crush A is closest (higher RSSI = closer = less negative).
  const Color colorA = Color(255, 0, 0, 0);
  const Color colorB = Color(0, 0, 255, 0);
  eng.setNearbyOverride({
    makePeer("crushA", 5, cfg, colorA, -40),
    makePeer("crushB", 5, cfg, colorB, -55),
  });
  eng.tick(g_nowMs);
  // A is closest → closest-transition fires for A. B is Smitten but not
  // closest → no immediate pulse for B.
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());
  TEST_ASSERT_TRUE(em.invocations[0].color == colorA);

  // Now B becomes closest — engine should fire an immediate pulse in B's color.
  em.invocations.clear();
  eng.setNearbyOverride({
    makePeer("crushB", 5, cfg, colorB, -30),  // now closer
    makePeer("crushA", 5, cfg, colorA, -55),
  });
  g_nowMs += 100;  // small advance
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());
  TEST_ASSERT_TRUE(em.invocations[0].color == colorB);
}

// test_smitten_closest_pulse_does_not_fire_if_closest_is_non_smitten
void test_smitten_closest_pulse_does_not_fire_if_closest_is_non_smitten() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;

  // Neutral lamp is closest; Smitten is further away.
  eng.setNearbyOverride({
    makePeer("neut",  3, cfg, Color(80, 80, 80, 0), -35),  // closest
    makePeer("crush", 5, cfg, Color(200, 0, 200, 0), -65),
  });
  eng.tick(g_nowMs);
  // Closest is Neutral → no closest-pulse.
  TEST_ASSERT_EQUAL_INT(0, (int)em.invocations.size());

  // Tick every second for 50s — closest stays Neutral throughout, no pulse.
  for (int t = 0; t < 50; ++t) {
    g_nowMs += 1000;
    eng.tick(g_nowMs);
  }
  TEST_ASSERT_EQUAL_INT(0, (int)em.invocations.size());
}

// test_smitten_closest_picks_highest_rssi_regardless_of_insertion_order
// Production `getReachableViaBle()` had a long-standing bug: the header
// comment claimed the result was sorted by RSSI descending but the sort
// was never actually performed. This test mirrors the sort behavior —
// peer inserted LAST but with the strongest RSSI must be the chosen closest.
void test_smitten_closest_picks_highest_rssi_regardless_of_insertion_order() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;
  const Color colorWeak   = Color(255, 0, 0, 0);  // red, far away
  const Color colorStrong = Color(0, 0, 255, 0);  // blue, closest
  eng.setNearbyOverride({
    makePeer("far",  5, cfg, colorWeak,   -70),  // inserted first
    makePeer("near", 5, cfg, colorStrong, -35),  // inserted second
  });
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());
  TEST_ASSERT_TRUE(em.invocations[0].color == colorStrong);
}

// test_smitten_closest_cadence_resets_after_disposition_demotion
// Cadence clock must not carry across a Smitten→non-Smitten flip.
void test_smitten_closest_cadence_resets_after_disposition_demotion() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;
  const Color color = Color(255, 0, 255, 0);
  eng.setNearbyOverride({makePeer("crush", 5, cfg, color, -40)});
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());

  // Demote to Neutral. closestSmittenName_ should clear AND
  // lastClosestPulseMs_ should reset to 0.
  cfg.dispositions[bdAddrFor("crush")] = 3;
  g_nowMs += 10000;
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());

  // Re-promote within the 45s cadence window — a fresh transition fires.
  cfg.dispositions[bdAddrFor("crush")] = 5;
  g_nowMs += 1000;
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(2, (int)em.invocations.size());

  // 44s later: cadence has NOT yet elapsed since the fresh transition
  // (lastClosestPulseMs_ was reset to the re-promotion time, ~t=11000).
  g_nowMs += 44000;
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(2, (int)em.invocations.size());

  // 2s later (46s since re-promotion): cadence fires.
  g_nowMs += 2000;
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(3, (int)em.invocations.size());
}

// test_smitten_closest_hysteresis_absorbs_small_rssi_noise
// Two Smitten peers with nearly identical RSSI. Without hysteresis, ±1
// dBm noise (well below the 3 dB hysteresis) would flap "closest" every
// tick and strobe a pulse on every flip.
void test_smitten_closest_hysteresis_absorbs_small_rssi_noise() {
  FakeConfig cfg;
  MockExpressionManager em;
  PersonalityEngine eng;
  resetEngine(eng, cfg);
  eng.begin(&cfg, &em);
  cfg.socialMode = SocialMode::Ambivert;
  const Color colorA = Color(255, 0, 0, 0);
  const Color colorB = Color(0, 255, 0, 0);

  // Initial state: A is closest by 2 dB (sub-threshold). One transition fires.
  eng.setNearbyOverride({
    makePeer("A", 5, cfg, colorA, -50),
    makePeer("B", 5, cfg, colorB, -52),
  });
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());
  TEST_ASSERT_TRUE(em.invocations[0].color == colorA);

  // Now flap: B nudges 1 dB stronger, then A regains 1 dB, etc. Each
  // tick the "front" alternates between A and B, but the delta is only
  // 1-2 dB — below kRssiHysteresisDb=3.
  for (int i = 1; i <= 20; ++i) {
    const int8_t aRssi = (i % 2 == 0) ? -50 : -51;
    const int8_t bRssi = (i % 2 == 0) ? -51 : -50;
    eng.setNearbyOverride({
      makePeer("A", 5, cfg, colorA, aRssi),
      makePeer("B", 5, cfg, colorB, bRssi),
    });
    g_nowMs += 200;
    eng.tick(g_nowMs);
  }
  TEST_ASSERT_EQUAL_INT(1, (int)em.invocations.size());

  // A decisive 5 dB jump (B now -45 vs A -50) should transition.
  eng.setNearbyOverride({
    makePeer("A", 5, cfg, colorA, -50),
    makePeer("B", 5, cfg, colorB, -45),  // 5 dB stronger than A
  });
  g_nowMs += 200;
  eng.tick(g_nowMs);
  TEST_ASSERT_EQUAL_INT(2, (int)em.invocations.size());
  TEST_ASSERT_TRUE(em.invocations[1].color == colorB);
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

  std::vector<NearbyLamp> mix;
  auto add = [&](const std::string& n, uint8_t d) {
    NearbyLamp lamp;
    lamp.name = n;
    lamp.bdAddr = bdAddrFor(n);
    lamp.lastRssi = -50;
    mix.push_back(lamp);
    cfg.dispositions[lamp.bdAddr] = d;
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
  NearbyLamp p;
  p.name = "stranger";
  p.bdAddr = bdAddrFor("stranger");
  p.lastRssi = -50;
  eng.setNearbyOverride({p});
  // Intentionally do NOT set cfg.dispositions[p.bdAddr].

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
  RUN_TEST(test::test_greeting_for_salty_is_snub_in_all_modes);
  RUN_TEST(test::test_greeting_for_wary_is_partial_snub_in_all_modes);
  RUN_TEST(test::test_greeting_for_smitten_extrovert_is_effusive_continuous);
  RUN_TEST(test::test_greeting_for_smitten_ambivert_is_enthused_two_pulses);
  RUN_TEST(test::test_greeting_profile_easeins_invert_with_warmth);
  RUN_TEST(test::test_greeting_for_unknown_peer_is_neutral);
  RUN_TEST(test::test_smitten_closest_pulse_fires_on_transition_and_every_45s);
  RUN_TEST(test::test_smitten_closest_pulse_resets_when_closest_changes);
  RUN_TEST(test::test_smitten_closest_pulse_does_not_fire_if_closest_is_non_smitten);
  RUN_TEST(test::test_smitten_closest_picks_highest_rssi_regardless_of_insertion_order);
  RUN_TEST(test::test_smitten_closest_hysteresis_absorbs_small_rssi_noise);
  RUN_TEST(test::test_smitten_closest_cadence_resets_after_disposition_demotion);
  RUN_TEST(test::test_smoothed_crowd_weight_matches_internal);
  RUN_TEST(test::test_crowd_composition_counts_by_disposition);
  RUN_TEST(test::test_crowd_composition_empty_when_no_peers);
  RUN_TEST(test::test_crowd_composition_unknown_peer_counts_as_neutral);

  return UNITY_END();
}

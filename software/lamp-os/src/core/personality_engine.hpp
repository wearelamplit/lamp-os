#pragma once

// PersonalityEngine — personality runtime. Owns crowd-aware dimming,
// disposition-driven greeting tuning, and the closest-Smitten recurring
// pulse.
//
// Read-side surface:
//   - applyCrowdDim(baseline)  → pure value transform; multiplies the
//     baseline brightness by the smoothed crowd-dim factor. Mode-dependent:
//     Introvert floors at 0.5, Ambivert floors at 0.7 (Fond/Smitten don't
//     contribute), Extrovert returns baseline unchanged.
//   - greetingFor(bdAddr)      → returns a GreetingTuning describing the
//     waveform SocialBehavior should play for this peer. Lookup is keyed
//     by BD_ADDR because dispositions_ is BD_ADDR-keyed (renames preserve
//     the peer's tier).
//
// Write-side surface:
//   - tick(nowMs) is called once per loop iteration. Samples nearbyLamps
//     at 1 Hz, recomputes the crowd-dim factor with hysteresis, and runs
//     the closest-Smitten pulse cycle.
//
// Test injection: in LAMP_TEST builds, setNearbyOverride() replaces the
// live nearbyLamps snapshot with a caller-supplied vector. Production
// builds compile this method out via #ifdef.

#include <Arduino.h>
#include <cstdint>
#include <string>
#include <vector>

#include "components/network/mesh/nearby_lamps.hpp"
#include "config/config_types.hpp"
#include "util/color.hpp"

namespace lamp {

class Config;            // fwd-decl
class ExpressionManager; // fwd-decl
class MeshLink;          // fwd-decl (for myMac)

// Sentinel for GreetingTuning::pulseBackCount meaning "fill the entire
// hold window with back-to-back cycles" instead of a fixed cycle count.
constexpr uint8_t kPulseCountContinuous = 0xFF;

// A peer's greeting waveform parameters. Returned by greetingFor() and
// consumed by SocialBehavior's draw()/playOnce().
struct GreetingTuning {
  uint32_t totalFrames     = 0;
  uint32_t easeInFrames    = 0;
  uint32_t holdFrames      = 0;
  uint32_t fadeOutFrames   = 0;
  uint8_t  pulseBackStrength = 0;
  uint8_t  pulseBackCount    = 0;
  // Snub waveform: the ease-in fades shade → peer color while dimming
  // brightness to pulseBackStrength depth (255 = black), holds dark, and
  // reverses on the way out. Warm greetings (snub=false) instead reach
  // full peer color and use pulseBackStrength/Count for in-hold pulses.
  bool     snub              = false;
};

// Counts of currently-visible BLE peers grouped by disposition.
// Returned by crowdComposition() for custom lamps + expressions that
// want to react to disposition patterns (e.g., "any Salty? → react").
struct CrowdComposition {
  uint8_t salty   = 0;  // disposition == 1
  uint8_t wary    = 0;  // disposition == 2
  uint8_t neutral = 0;  // disposition == 3 (default for unknown peers)
  uint8_t fond    = 0;  // disposition == 4
  uint8_t smitten = 0;  // disposition == 5
};

class PersonalityEngine {
 public:
  // Wire dependencies. Call once during boot, after the wired modules
  // have themselves been begin()'d. ExpressionManager + MeshLink
  // power the closest-Smitten recurring pulse cycle.
  void begin(Config* config,
             ExpressionManager* expressionManager = nullptr,
             MeshLink* meshLink = nullptr);

  // Drive the engine. Called every loop iteration on Core 1. Most internal
  // work is gated behind a 1 Hz sample tick.
  void tick(uint32_t nowMs);

  // Pure value transform for effectiveBrightness(). Mode-aware:
  // Introvert floors at 0.5, Ambivert floors at 0.7 (Fond/Smitten weight=0),
  // Extrovert returns baseline unchanged. Always returns ≥ 1 (we never
  // blank a lamp from personality alone).
  uint8_t applyCrowdDim(uint8_t baseline) const;

  // Current target crowd-dim factor — what applyCrowdDim multiplies by
  // (Extrovert returns 1.0 regardless of internal state). Read-only window
  // into the smoother for the brightness chain, which interpolates
  // between successive targets over ~80 ms so deadband-crossing commits
  // don't snap in a single frame.
  float crowdDimFactor() const;

  // Smoothed weighted-crowd value W — the signal that feeds
  // crowdDimFactor() before the per-mode floor + log curve are applied.
  // Exposed so custom lamps can apply their own curve/floor/target axis.
  // Mode-dependent weighting still applies (Ambivert ignores Fond/Smitten).
  float smoothedCrowdWeight() const;

  // Snapshot of BLE-reachable peers grouped by disposition. Pure read;
  // safe from any Core 1 context. Counts cap at uint8_t max — fleet
  // is 22 lamps, no realistic overflow.
  CrowdComposition crowdComposition() const;

  // Per-peer greeting tuning. Returned at SocialBehavior::control() time
  // so the waveform is read in lockstep with the cooldown gate the
  // behavior already does. Pure read.
  GreetingTuning greetingFor(const std::string& peerBdAddr) const;

  // Was a SocialMode transition witnessed since the last consume that
  // changes the dim regime? Used by the wiring in lamp.cpp to trip
  // pendingApplyEffectiveBrightness once on the relevant transitions so
  // the dim cleanly releases / re-engages.
  bool consumePendingApply() {
    const bool was = pendingApply_;
    pendingApply_ = false;
    return was;
  }

  // Curve parameters — exposed as constexpr so tests can reference them
  // without re-deriving the numbers.
  static constexpr float kIntrovertFloor = 0.5f;
  static constexpr float kAmbivertFloor  = 0.7f;
  static constexpr float kCurveScaleW = 10.0f;   // ~W=10 hits the floor
  static constexpr uint32_t kSamplePeriodMs = 1000;
  static constexpr size_t kSampleWindow = 4;
  static constexpr float kEmaAlpha = 0.15f;
  static constexpr uint8_t kDeadbandLevels = 2;  // ≥ 2/100 change to commit

  // Smitten closest-peer recurring pulse.
  static constexpr uint32_t kClosestPulsePeriodMs = 60000;
  // Hysteresis margin (dBm) for closest-peer swaps. RSSI on ESP-NOW
  // typically wobbles ±3–5 dB even with a stationary peer; without this
  // guard, two Smitten peers with similar signal would flap "closest"
  // every tick and fire a pulse on each flip.
  static constexpr uint8_t  kRssiHysteresisDb     = 3;

  // Available in both LAMP_TEST and LAMP_DEBUG builds. Replaces the live
  // nearbyLamps.getReachableViaBle() snapshot used by the engine — lets
  // the developer simulate a crowd from one lamp + the Flutter app's
  // test-action button. Pass {} to drop back to live data.
#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  void setNearbyOverride(std::vector<NearbyLamp> peers);
  void clearNearbyOverride();
#endif

 private:
  // --- Crowd-dim machinery ------------------------------------------------

  // Weight a peer's crowd contribution by their disposition + current mode.
  // Introvert: Smitten 0, Fond 0.5, Neutral 1.0, Wary 1.5, Salty 2.0.
  // Ambivert : Smitten 0, Fond 0  , Neutral 1.0, Wary 1.5, Salty 2.0
  //            (warm relationships don't add crowd pressure in Ambivert).
  // Extrovert: caller skips this — applyCrowdDim returns baseline.
  float weightForDisposition_(uint8_t disposition, SocialMode mode) const;

  // Σ weight(disposition(peer), mode) over `peers`. Self-MAC filtering is
  // the caller's responsibility (nearbyLamps doesn't include self anyway).
  float computeWeightedCount_(const std::vector<NearbyLamp>& peers,
                              SocialMode mode) const;

  // factor = max(floor, 1 - (1-floor) * log10(1+W) / log10(1+kCurveScaleW))
  // Clamped to [floor, 1.0]. Pure function of W + floor; pinned in tests.
  float dimFactorForCount_(float weightedCount, float floor) const;

  // Per-mode dim floor lookup. Extrovert returns 1.0 (no dim).
  static float floorForMode_(SocialMode mode);

  // Pulls a fresh BLE-reachable snapshot (or the test override if active),
  // computes W, runs the rolling-median + EMA smoother, applies the
  // deadband, and updates crowdDimFactor_/lastCommittedLevel_ if needed.
  void sampleAndSmoothCrowd_(uint32_t nowMs,
                              const std::vector<NearbyLamp>& peers);

  // --- Smitten closest cycle ---------------------------------------------

  // Get the current BLE-reachable snapshot (or the test override). Pure
  // read; used by both the crowd-dim sampler and the closest-peer scan so
  // we share one snapshot per tick.
  std::vector<NearbyLamp> snapshotBlePeers_() const;

  // Smitten closest-peer pulse cycle: identifies the closest BLE peer
  // (peers are RSSI-sorted by nearby_lamps; peers.front() is highest).
  // If that peer is Smitten, fires a pulse on every transition AND every
  // kClosestPulsePeriodMs while they remain closest. A different peer
  // becoming closest resets the cadence.
  void tickClosestSmittenPulse_(uint32_t nowMs,
                                 const std::vector<NearbyLamp>& peers);

  // Fire one `pulse` ExpressionInvocation in `color` via expressionManager_.
  // No-op if expressionManager_/meshLink_ aren't wired. Receive-side
  // terminus — never cascades.
  void firePulse_(const Color& color);

  // --- State --------------------------------------------------------------

  Config* config_ = nullptr;
  ExpressionManager* expressionManager_ = nullptr;
  MeshLink* meshLink_ = nullptr;

  uint32_t lastSampleMs_ = 0;
  // Rolling buffer of W samples; we take the median to absorb the
  // occasional outlier from a peer briefly fading in/out at the edge.
  float sampleBuf_[kSampleWindow] = {0};
  size_t sampleHead_ = 0;
  size_t sampleCount_ = 0;  // grows from 0 to kSampleWindow once seeded
  float smoothedW_ = 0.0f;
  bool emaSeeded_ = false;

  // The factor we'd return from applyCrowdDim() right now. Updated in
  // sampleAndSmoothCrowd_ when the deadband is crossed.
  float crowdDimFactor_ = 1.0f;
  // Last brightness level we committed to (after dim factor applied to a
  // nominal baseline of 100). Used to gate re-publishing pendingApply.
  uint8_t lastCommittedLevel_ = 100;
  // Set by sampleAndSmoothCrowd_ on a meaningful change OR by a SocialMode
  // transition that changes the dim regime. Consumed by the loop wiring.
  bool pendingApply_ = false;
  SocialMode lastSocialMode_ = SocialMode::Ambivert;

  // Smitten closest-peer tracking. Empty name = no Smitten peer is
  // currently the closest BLE neighbour. We don't cache the peer's
  // color across ticks — tickClosestSmittenPulse_ reads it from the
  // live peer snapshot every fire, so a recolored peer's pulse
  // automatically tracks without bookkeeping.
  std::string closestSmittenName_;
  uint32_t lastClosestPulseMs_ = 0;

#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  std::vector<NearbyLamp> nearbyOverride_;
  bool nearbyOverrideActive_ = false;
#endif
};

extern PersonalityEngine personalityEngine;

}  // namespace lamp

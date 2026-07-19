#pragma once

// PersonalityEngine is the personality runtime. Owns crowd-aware dimming and
// disposition-driven greeting tuning.
//
// Read-side surface:
//   - crowdDimFactor()         → smoothed crowd-dim factor (0..1) the
//     brightness chain multiplies the baseline by. Mode-dependent: Introvert
//     floors at 0.5, Ambivert floors at 0.7 (Fond/Smitten don't contribute),
//     Extrovert returns 1.0.
//   - greetingFor(peerLampId)  → returns a GreetingTuning describing the
//     waveform SocialBehavior should play for this peer. Lookup is keyed
//     by lampId (mac) because dispositions_ is lampId-keyed (renames
//     preserve the peer's tier).
//
// Write-side surface:
//   - tick(nowMs) is called once per loop iteration. Samples lampRoster
//     at 1 Hz and recomputes the crowd-dim factor with hysteresis.
//
// Test injection: in LAMP_TEST builds, setNearbyOverride() replaces the
// live lampRoster snapshot with a caller-supplied vector. Production
// builds compile this method out via #ifdef.

#include <Arduino.h>
#include <cstdint>
#include <string>
#include <vector>

#include "components/network/mesh/lamp_roster.hpp"
#include "config/config_types.hpp"
#include "util/color.hpp"
#include "util/easing.hpp"

namespace lamp {

class Config;  // fwd-decl

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
  // Frames per warm-breath cycle (dim-down + brighten-back). Faster =
  // more eager. Only meaningful when pulseBackStrength > 0.
  uint16_t breathCycleFrames = 120;
  // Snub waveform: the ease-in fades shade → peer color while dimming
  // brightness to pulseBackStrength depth (255 = black), holds dark, and
  // reverses on the way out. Warm greetings (snub=false) instead reach
  // full peer color and use pulseBackStrength/Count for in-hold pulses.
  bool     snub              = false;
  // Motion of the color ramps. draw() routes the ease-in/ease-out
  // POSITION through applyEasing(curve, t), so disposition reads in the
  // arrival/departure curve, not just the timing.
  Easing   easeInCurve  = Easing::Smooth;
  Easing   easeOutCurve = Easing::Smooth;
  // Motion of the in-hold breath (warm profiles). Float dwells at the top +
  // bottom of each breath (calm); Smooth breathes continuously (eager).
  Easing   breathCurve  = Easing::Float;
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
  // have themselves been begin()'d.
  void begin(Config* config);

  // Drive the engine. Called every loop iteration on Core 1. Most internal
  // work is gated behind a 1 Hz sample tick.
  void tick(uint32_t nowMs);

  // Target crowd-dim factor (0..1) the brightness chain multiplies the
  // baseline by. Mode-aware: Introvert floors at 0.5, Ambivert floors at 0.7
  // (Fond/Smitten weight=0), Extrovert returns 1.0. Read-only window into the
  // smoother; the chain interpolates between successive targets over ~80 ms so
  // deadband-crossing commits don't snap in a single frame.
  float crowdDimFactor() const;

  // Smoothed weighted-crowd value W, the signal that feeds
  // crowdDimFactor() before the per-mode floor + log curve are applied.
  // Exposed so custom lamps can apply their own curve/floor/target axis.
  // Mode-dependent weighting still applies (Ambivert ignores Fond/Smitten).
  float smoothedCrowdWeight() const;

  // Snapshot of BLE-reachable peers grouped by disposition. Pure read;
  // safe from any Core 1 context. Counts cap at uint8_t max; the
  // BLE-reachable crowd can't approach 255, so no realistic overflow.
  CrowdComposition crowdComposition() const;

  // Per-peer greeting tuning. Returned at SocialBehavior::control() time
  // so the waveform is read in lockstep with the cooldown gate the
  // behavior already does. Pure read.
  GreetingTuning greetingFor(const std::string& peerLampId) const;

  // Was a SocialMode transition witnessed since the last consume that
  // changes the dim regime? Used by the wiring in lamp.cpp to trip
  // pendingApplyEffectiveBrightness once on the relevant transitions so
  // the dim cleanly releases / re-engages.
  bool consumePendingApply() {
    const bool was = pendingApply_;
    pendingApply_ = false;
    return was;
  }

  // Curve parameters exposed as constexpr so tests can reference them
  // without re-deriving the numbers.
  static constexpr float kIntrovertFloor = 0.5f;
  static constexpr float kAmbivertFloor  = 0.7f;
  static constexpr float kCurveScaleW = 10.0f;   // ~W=10 hits the floor
  static constexpr uint32_t kSamplePeriodMs = 1000;
  static constexpr size_t kSampleWindow = 4;
  static constexpr float kEmaAlpha = 0.15f;
  static constexpr uint8_t kDeadbandLevels = 2;  // ≥ 2/100 change to commit

  // Available in both LAMP_TEST and LAMP_DEBUG builds. Replaces the live
  // lampRoster.getNear() snapshot used by the engine, letting
  // the developer simulate a crowd from one lamp + the Flutter app's
  // test-action button. Pass {} to drop back to live data.
#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  void setNearbyOverride(std::vector<RosterEntry> peers);
  void clearNearbyOverride();
#endif

 private:
  // Weight a peer's crowd contribution by their disposition + current mode.
  // Introvert: Smitten 0, Fond 0.5, Neutral 1.0, Wary 1.5, Salty 2.0.
  // Ambivert : Smitten 0, Fond 0  , Neutral 1.0, Wary 1.5, Salty 2.0
  //            (warm relationships don't add crowd pressure in Ambivert).
  // Extrovert: crowdDimFactor() returns 1.0, so the weighting is moot.
  float weightForDisposition_(uint8_t disposition, SocialMode mode) const;

  // Σ weight(disposition(peer), mode) over `peers`. Self-MAC filtering is
  // the caller's responsibility (lampRoster doesn't include self anyway).
  float computeWeightedCount_(const std::vector<RosterEntry>& peers,
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
                              const std::vector<RosterEntry>& peers);

  // Get the current BLE-reachable snapshot (or the test override). Pure
  // read; called only at the sample cadence to refresh blePeerCache_.
  std::vector<RosterEntry> snapshotBlePeers_() const;

  Config* config_ = nullptr;

  uint32_t lastSampleMs_ = 0;
  // Roster snapshot for the sampler, refreshed at kSamplePeriodMs in
  // tick(); BLE scan reports only update the roster ~1/s anyway.
  std::vector<RosterEntry> blePeerCache_;
  // Rolling buffer of W samples; the median absorbs the occasional
  // outlier from a peer briefly fading in/out at the edge.
  float sampleBuf_[kSampleWindow] = {0};
  size_t sampleHead_ = 0;
  size_t sampleCount_ = 0;  // grows from 0 to kSampleWindow once seeded
  float smoothedW_ = 0.0f;
  bool emaSeeded_ = false;

  // The factor crowdDimFactor() returns right now. Updated in
  // sampleAndSmoothCrowd_ when the deadband is crossed.
  float crowdDimFactor_ = 1.0f;
  // Last committed brightness level (after dim factor applied to a
  // nominal baseline of 100). Used to gate re-publishing pendingApply.
  uint8_t lastCommittedLevel_ = 100;
  // Set by sampleAndSmoothCrowd_ on a meaningful change OR by a SocialMode
  // transition that changes the dim regime. Consumed by the loop wiring.
  bool pendingApply_ = false;
  SocialMode lastSocialMode_ = SocialMode::Ambivert;

#if defined(LAMP_TEST) || defined(LAMP_DEBUG)
  std::vector<RosterEntry> nearbyOverride_;
  bool nearbyOverrideActive_ = false;
#endif
};

extern PersonalityEngine personalityEngine;

}  // namespace lamp

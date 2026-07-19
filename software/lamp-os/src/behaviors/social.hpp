#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "behaviors/greetable.hpp"
#include "config/config_types.hpp"
#include "core/animated_behavior.hpp"
#include "util/color.hpp"
#include "util/easing.hpp"

namespace lamp {

class Config;  // fwd-decl; full include is heavy and only the .cpp uses it.
class MeshLink;  // fwd-decl; only the .cpp calls sendColorQuery.
struct GreetingTuning;  // fwd-decl; defined in core/personality_engine.hpp.

/**
 * social color exchange. Reads the unified lampRoster store directly,
 * gated on lastSeenNearMs so only short-range peers trigger greetings.
 *
 * Personality:
 *   - Extrovert: greet every reachable peer with EXTROVERT_COOLDOWN_MS
 *     between greetings. No per-peer re-greet window.
 *   - Ambivert (default): AMBIVERT_BASE_COOLDOWN_MS between greetings,
 *     won't re-greet the same peer within AMBIVERT_REGREET_WINDOW_MS.
 *   - Introvert: INTROVERT_BASE_COOLDOWN_MS between greetings. Won't
 *     re-greet the same peer within INTROVERT_REGREET_WINDOW_MS. After
 *     INTROVERT_FATIGUE_COUNT greetings inside INTROVERT_FATIGUE_WINDOW_MS,
 *     enters a tired state for INTROVERT_TIRED_DURATION_MS.
 *
 * Per-peer re-greet tracking is in-memory only (lastGreetedAtMs_) and
 * bounded to MAX_GREETED_TRACKED with LRU eviction. Survives the
 * LampRoster prune cycle so a peer leaving + returning doesn't re-greet
 * within the window.
 */
class SocialBehavior : public AnimatedBehavior, public Greetable {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  uint32_t nextAcknowledgeTimeMs = 0;
  Color foundLampColor;

  // Waveform parameters, copied from PersonalityEngine::greetingFor()
  // at greeting time. easeInFrames + holdFrames + fadeOutFrames should
  // equal the AnimatedBehavior `frames`; draw() guards anyway.
  // pulseBackStrength is the depth of the in-hold pulse dip; 0 disables,
  // 255 dips all the way to black. pulseBackCount sets the number of
  // breathCycleFrames cycles to run inside the hold; kPulseCountContinuous
  // fills the entire hold with back-to-back cycles. breathCycleFrames is
  // the per-breath length (dim-down + brighten-back).
  uint32_t easeInFrames = 30;
  uint32_t holdFrames = 60;
  uint32_t fadeOutFrames = 30;
  uint8_t  pulseBackStrength = 0;
  uint8_t  pulseBackCount    = 0;
  uint16_t breathCycleFrames = 120;
  // Snub waveform; see GreetingTuning::snub. When set, draw() folds the
  // pulseBackStrength dim into the ease-in/out (shade → dark-in-their-
  // color → shade) instead of running in-hold pulses.
  bool     snub              = false;
  // Motion of the ease-in / ease-out color ramps; see GreetingTuning.
  Easing   easeInCurve  = Easing::Smooth;
  Easing   easeOutCurve = Easing::Smooth;
  // Motion of the in-hold breath (warm profiles); see GreetingTuning.
  Easing   breathCurve  = Easing::Float;

  bool isSocialBehavior() const override { return true; }

  void draw() override;
  void control() override;

  // Greetable: play a greeting for the given peer immediately, bypassing
  // discovery and cooldown gates. Stamps re-greet tracking so the natural
  // cooldown applies on the next organic sighting.
  void triggerGreeting(const RosterEntry& peer) override;

  // Copy a greeting waveform from the engine into the draw-side fields
  // (including `frames`, which AnimatedBehavior's playOnce/nextFrame
  // drive). Single seam so adding a GreetingTuning field touches one
  // place, not every caller. foundLampColor is the peer color, set
  // separately by the caller.
  void applyTuning(const GreetingTuning& t);

  // Current greeting state. active while the animation plays; peer and kind
  // are populated for the duration. Cleared when the animation stops.
  GreetingState greetingState() const override;

  // Called when greeting starts or ends. Used by the framework wiring to
  // push CHAR_STATE_NOTIFY without coupling behaviors to the BLE layer.
  void setOnGreetingChangeCallback(std::function<void()> fn) { onGreetingChange_ = std::move(fn); }

  // Wires the live Config so control() can read the current socialMode.
  // No setter = behaves as Ambivert (the spec's pre-personality default).
  void setConfig(Config* config) { config_ = config; }

  void setMeshLink(MeshLink* m) { meshLink_ = m; }

  void onColorInfo(const uint8_t srcMac[6], const std::vector<Color>& baseStops,
                   const std::vector<Color>& shadeStops) override;

  // Stamp the per-peer re-greet timestamp + per-mode cooldown without
  // running the discovery / cooldown gates. Called by triggerGreeting so
  // forced greetings still enforce the natural cooldown on the next sighting.
  void markGreeted(const std::string& peerName, uint32_t nowMs);

  // Throttle the gossip-OTA peer scan to this cadence. Lamps emit HELLO
  // every 1-2s, so 500 ms catches every fresh sighting while saving the
  // ~60 Hz vector allocation that control() would otherwise do per
  // frame. Skipped entirely while distributor.isInProgress(); the
  // single-source mutex blocks a second concurrent session anyway, so
  // there's nothing useful to scan for.
  static constexpr uint32_t kOtaScanIntervalMs  = 500;

 private:
  static constexpr size_t MAX_GREETED_TRACKED = 32;
  // Must exceed the longest greeting animation (~23.75s for Effusive) or
  // the cooldown is inert; it's clocked from greeting start and the
  // animationState!=STOPPED gate already blocks until the animation ends,
  // so a value below the animation length leaves zero gap between
  // greetings. 26s keeps extroverts the eagerest mode (small post-greet
  // gap) while still pacing them.
  static constexpr uint32_t EXTROVERT_COOLDOWN_MS = 26000;
  static constexpr uint32_t AMBIVERT_BASE_COOLDOWN_MS = 30000;
  static constexpr uint32_t AMBIVERT_REGREET_WINDOW_MS = 300000;
  static constexpr uint32_t INTROVERT_BASE_COOLDOWN_MS = 60000;
  static constexpr uint32_t INTROVERT_REGREET_WINDOW_MS = 600000;
  static constexpr size_t   INTROVERT_FATIGUE_COUNT = 3;
  static constexpr uint32_t INTROVERT_FATIGUE_WINDOW_MS = 300000;
  static constexpr uint32_t INTROVERT_TIRED_DURATION_MS = 300000;

  Config* config_ = nullptr;
  std::map<std::string, uint32_t> lastGreetedAtMs_;
  std::vector<uint32_t> recentGreetMs_;
  uint32_t tiredUntilMs_ = 0;
  // Peer's lampId (mac), populated for the animation duration.
  std::string greetingPeerLampId_;
  std::function<void()> onGreetingChange_;
  // Tracks whether the animation was active last control() so stop-edge fires.
  bool greetingWasActive_ = false;

  MeshLink* meshLink_ = nullptr;
  uint8_t   greetedMac_[6] = {0};
  bool      greetedHasMac_ = false;
  std::vector<Color> greetedBaseStops_;

  static constexpr uint8_t kGradientBlendStep = 21;  // ~255 over ~12 frames
  uint8_t greetedBlend_ = 0;
  std::vector<Color> gradientCache_;

  // Last time the OTA peer-scan block in control() actually walked the
  // ESP-NOW roster. Throttled by kOtaScanIntervalMs to amortize the
  // vector allocation across many frames.
  uint32_t lastOtaScanMs_      = 0;
};

}  // namespace lamp

#pragma once

#include <map>
#include <string>
#include <vector>

#include "config/config_types.hpp"
#include "core/animated_behavior.hpp"
#include "util/color.hpp"

namespace lamp {

class Config;  // fwd-decl — full include is heavy and only the .cpp uses it.

/**
 * @brief social color exchange — reads the unified nearbyLamps store directly,
 *        gated on lastSeenViaBleMs so only short-range peers trigger greetings.
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
 * NearbyLamps prune cycle so a peer leaving + returning doesn't re-greet
 * within the window.
 */
class SocialBehavior : public AnimatedBehavior {
  using AnimatedBehavior::AnimatedBehavior;

 public:
  uint32_t nextAcknowledgeTimeMs = 0;
  Color foundLampColor;

  // Waveform parameters, copied from PersonalityEngine::greetingFor()
  // at greeting time. easeInFrames + holdFrames + fadeOutFrames should
  // equal the AnimatedBehavior `frames`; draw() guards anyway.
  // pulseBackStrength is the depth of the in-hold pulse dip — 0 disables;
  // 255 dips all the way to black. pulseBackCount sets the number of
  // ~kSlowPulseCycleFrames cycles to run inside the hold;
  // kPulseCountContinuous fills the entire hold with back-to-back cycles.
  uint32_t easeInFrames = 30;
  uint32_t holdFrames = 60;
  uint32_t fadeOutFrames = 30;
  uint8_t  pulseBackStrength = 0;
  uint8_t  pulseBackCount    = 0;

  // Per-cycle frame budget for the in-hold pulse loop (~750ms at 60fps).
  static constexpr uint32_t kSlowPulseCycleFrames = 45;

  void draw() override;
  void control() override;

  // Wires the live Config so control() can read the current socialMode.
  // No setter = behaves as Ambivert (the spec's pre-personality default).
  void setConfig(Config* config) { config_ = config; }

  // Throttle the gossip-OTA peer scan to this cadence. Lamps emit HELLO
  // every 1-2s, so 500 ms catches every fresh sighting while saving the
  // ~60 Hz vector allocation that control() would otherwise do per
  // frame. Skipped entirely while distributor.isInProgress() — the
  // single-source mutex blocks a second concurrent session anyway, so
  // there's nothing useful to scan for.
  static constexpr uint32_t kOtaScanIntervalMs  = 500;

#ifdef LAMP_DEBUG
  // Stamp the per-peer re-greet timestamp + per-mode cooldown without
  // running the discovery / cooldown gates. Used by the testGreet bench
  // path so a forced greeting still observes the natural cooldown the
  // next time the peer is heard via BLE adv.
  void markGreeted(const std::string& peerName, uint32_t nowMs);
#endif

 private:
  static constexpr size_t MAX_GREETED_TRACKED = 32;
  static constexpr uint32_t EXTROVERT_COOLDOWN_MS = 15000;
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

  // Last time the OTA peer-scan block in control() actually walked the
  // ESP-NOW roster. Throttled by kOtaScanIntervalMs to amortize the
  // vector allocation across many frames.
  uint32_t lastOtaScanMs_      = 0;
};

}  // namespace lamp

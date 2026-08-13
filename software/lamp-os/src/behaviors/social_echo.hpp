#pragma once

#include <cstdint>

#include "config/config_types.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_observer.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

class Config;
class ExpressionManager;
class MeshLink;

// Expression mirror. When a nearby lamp announces a triggered expression
// (MSG_EVENT → ExpressionObserverRegistry), a lamp with a warm disposition
// toward that peer probabilistically replays the same expression a fraction
// of a second later, weighted by disposition and social mode. The replay
// goes through triggerInvocation, which suppresses cascade, so two lamps
// never echo each other into a loop.

// Mirror-chance grid, all bench-tunable feel numbers.
// rate% = kMirrorBasePct[disposition] * kMirrorModeFactorX10[mode] / 10,
// clamped 0..100. Only Fond(4)/Smitten(5) ever mirror.
inline constexpr uint8_t kMirrorBasePct[6] = {0, 0, 0, 0, 20, 50};
// Introvert ×0.5, Ambivert ×1.0, Extrovert ×1.5 (fixed-point ×10).
inline constexpr uint8_t kMirrorModeFactorX10[3] = {5, 10, 15};
// Introvert-only floor between mirrors so an introvert doesn't chatter.
inline constexpr uint32_t kIntrovertMirrorCooldownMs = 600000;
// Replay lands 0.4-0.8 s after the announce: always just-after, never lockstep.
inline constexpr uint32_t kMirrorDelayFloorMs = 400;
inline constexpr uint32_t kMirrorJitterMs = 400;

// Resolved mirror chance (0..100) for a disposition + mode. Pure.
inline uint8_t mirrorRatePct(uint8_t disposition, SocialMode mode) {
  if (disposition >= 6) return 0;
  const uint8_t modeIdx = static_cast<uint8_t>(mode);
  if (modeIdx >= 3) return 0;
  const uint32_t rate = static_cast<uint32_t>(kMirrorBasePct[disposition]) *
                        kMirrorModeFactorX10[modeIdx] / 10;
  return rate > 100 ? 100 : static_cast<uint8_t>(rate);
}

// Pure mirror decision. `roll` is a 1..100 chance draw, `jitterRoll` a
// 0..kMirrorJitterMs draw. `everMirrored`/`lastMirrorMs` carry the
// introvert-cooldown state. Returns true and sets `outFireAt` when a replay
// should be scheduled; false to skip. Isolated from Config/MeshLink so it
// pins in a native test.
inline bool mirrorDecision(uint8_t disposition, SocialMode mode, uint8_t roll,
                           uint32_t jitterRoll, uint32_t nowMs,
                           bool everMirrored, uint32_t lastMirrorMs,
                           uint32_t& outFireAt) {
  if (disposition < 4) return false;
  const uint8_t rate = mirrorRatePct(disposition, mode);
  if (rate == 0) return false;
  if (roll > rate) return false;
  if (mode == SocialMode::Introvert && everMirrored &&
      nowMs - lastMirrorMs < kIntrovertMirrorCooldownMs) {
    return false;
  }
  const uint32_t jitter =
      jitterRoll > kMirrorJitterMs ? kMirrorJitterMs : jitterRoll;
  outFireAt = nowMs + kMirrorDelayFloorMs + jitter;
  return true;
}

class SocialEchoObserver : public IExpressionObserver {
 public:
  // config: disposition + socialMode. manager: triggerInvocation for the
  // replay. mesh: OTA-in-progress gate. All must outlive the observer.
  SocialEchoObserver(Config& config, ExpressionManager& manager,
                     MeshLink& mesh)
      : config_(config), manager_(manager), mesh_(mesh) {}

  void onPeerExpression(const uint8_t sourceMac[6],
                        const ExpressionInvocation& inv) override;

  // Fire any scheduled replay whose deadline has passed. Wire into the loop.
  void tick(uint32_t nowMs);

 private:
  struct Pending {
    ExpressionInvocation inv;
    uint8_t srcMac[6] = {};
    uint32_t fireAt = 0;
    bool used = false;
  };
  static constexpr size_t kMaxPending = 4;

  Config& config_;
  ExpressionManager& manager_;
  MeshLink& mesh_;
  FastRng rng_;
  Pending pending_[kMaxPending];
  uint32_t lastMirrorMs_ = 0;
  bool everMirrored_ = false;
};

}  // namespace lamp

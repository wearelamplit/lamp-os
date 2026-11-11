#pragma once

#include <cstdint>

#include "behaviors/copycat.hpp"
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
// of a second later, weighted by disposition and social mode. A copy short of
// ContagionTuning::chainMaxHops announces itself one hop further on, at a
// smaller chance per hop, so a chain fades out. The replay never cascades.
// Mechanism and tuning: docs/dev/expressions.md, "Expression mirror".

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

  ContagionTuning& tuning() { return copycat_.tuning; }

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
  Copycat copycat_;
};

}  // namespace lamp

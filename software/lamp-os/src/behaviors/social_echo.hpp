#pragma once

#include <cstdint>

#include "behaviors/copycat.hpp"
#include "behaviors/yawn_contagion.hpp"
#include "expressions/expression_invocation.hpp"
#include "expressions/expression_observer.hpp"
#include "util/fast_rng.hpp"

namespace lamp {

class Config;
class ExpressionManager;
class FondnessEngine;
class MeshLink;

// Expression mirror. When a nearby lamp announces a triggered expression
// (MSG_EVENT → ExpressionObserverRegistry), a lamp with a warm disposition
// toward that peer probabilistically replays the same expression a fraction
// of a second later, weighted by disposition and social mode. A copy short of
// ContagionTuning::chainMaxHops announces itself one hop further on, at a
// smaller chance per hop, so a chain fades out. The replay never cascades.
// A yawn takes its own contagion roll in place of the mirror's, and a tired
// introvert yawns now and then on its own. While the fondness engine is not
// tracking, a copy is the plain mirror: no chain, roll cooldown, crowd
// damping, self-echo guard or yawn rule.
// Mechanism and tuning: docs/dev/expressions.md, "Expression mirror" and
// "Yawn contagion".

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
  YawnTuning& yawnTuning() { return yawns_.tuning; }

  // Lamps in the near view, for crowd damping. Unset counts none.
  void setNearbyCount(size_t (*count)()) { nearbyCount_ = count; }
  // Whether the lamp is a tired introvert. Unset is never tired.
  void setTired(bool (*tired)()) { tired_ = tired; }
  // The engine whose tracking() gates everything past the plain mirror. Unset
  // never tracks.
  void setFondness(const FondnessEngine* fondness) { fondness_ = fondness; }

 private:
  struct Pending {
    ExpressionInvocation inv;
    uint8_t srcMac[6] = {};
    uint32_t fireAt = 0;
    bool used = false;
    bool yawnBack = false;
    // Booked while tracking, so it may announce by the chain rule.
    bool chained = false;
  };
  static constexpr size_t kMaxPending = 4;

  bool tracking() const;
  // Played within the own floor, or any yawn booked to play.
  bool yawnedRecently(uint32_t nowMs) const;
  // A yawn-back booked to play.
  bool yawnPending() const;
  // A yawn-back or a copycat copy of a yawn booked to play.
  bool yawnBooked() const;
  void catchYawn(const uint8_t sourceMac[6], const ExpressionInvocation& inv, uint32_t nowMs);
  void tickTired(uint32_t nowMs);
  // False when every pending slot is taken.
  bool book(const ExpressionInvocation& inv, const uint8_t sourceMac[6], const CopyDecision& d,
            bool yawnBack, bool chained);

  Config& config_;
  ExpressionManager& manager_;
  MeshLink& mesh_;
  FastRng rng_;
  Pending pending_[kMaxPending];
  Copycat copycat_;
  YawnContagion yawns_;
  size_t (*nearbyCount_)() = nullptr;
  bool (*tired_)() = nullptr;
  const FondnessEngine* fondness_ = nullptr;
};

}  // namespace lamp

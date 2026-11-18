#include "expressions/spore_tint.hpp"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

#include "expressions/expression_manager.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/shimmer/shimmer_expression.hpp"
#include "expressions/transient_admission.hpp"

namespace lamp {

static_assert(ShiftyExpression::maxCycleMs(SporeTint::kFadeS * kMsPerSecond,
                                           SporeTint::kHoldS * kMsPerSecond,
                                           SporeTint::kMaxFadeBackS * kMsPerSecond,
                                           kShiftyFillBloom) < TransientSlots::kMaxLifetimeMs,
              "the longest spore tint must complete before the transient lifetime cap reaps it");

bool SporeTint::tintFor(const ExpressionInvocation& burst, ExpressionInvocation& tint,
                        uint32_t& afterMs) {
  const uint32_t fadeBackS = getParam(burst.parameters, "fadeDuration", 0);
  const bool spore = burst.type == ShimmerExpression::classDescriptor().id &&
                     !burst.colors.empty() && fadeBackS > 0 &&
                     ShimmerExpression::burstParams(burst.parameters);
  if (!spore) return false;

  tint = ExpressionInvocation{};
  tint.type = ShiftyExpression::classDescriptor().id;
  tint.target = TARGET_BASE;
  tint.colors = burst.colors;
  tint.parameters.set("fillMode", kShiftyFillBloom);
  tint.parameters.set("fadeDuration", kFadeS);
  tint.parameters.set("shiftDurationMin", kHoldS);
  tint.parameters.set("shiftDurationMax", kHoldS);
  tint.parameters.set("durationMax", std::min(fadeBackS, kMaxFadeBackS));
  tint.parameters.set("opacity", kOpacityPct);
  afterMs = clampBurstMs(getParam(burst.parameters, "durationMax", 0));
  return true;
}

bool SporeTint::follow(const ExpressionInvocation& burst, const uint8_t srcMac[6],
                       uint32_t nowMs, bool tintPlaying) {
  if (booked_ || tintPlaying) return false;
  if (!tintFor(burst, tint_, afterMs_)) return false;
  std::memcpy(srcMac_, srcMac, 6);
  bookedAtMs_ = nowMs;
  booked_ = true;
  return true;
}

void SporeTint::tick(ExpressionManager& manager, uint32_t nowMs) {
  if (!booked_ || nowMs - bookedAtMs_ < afterMs_) return;
  if (manager.remoteTransientPlaying(ShimmerExpression::classDescriptor().id, srcMac_)) return;
  booked_ = false;
  manager.triggerInvocation(tint_, srcMac_);
}

bool playReceivedCommand(ExpressionManager& manager, SporeTint& sporeTint,
                         const ExpressionInvocation& inv, const uint8_t srcMac[6]) {
  if (!manager.triggerInvocation(inv, srcMac)) return false;
  sporeTint.follow(inv, srcMac, millis(),
                   manager.remoteTransientPlaying(ShiftyExpression::classDescriptor().id));
  return true;
}

}  // namespace lamp

#include "expressions/spore_tint.hpp"

#include <Arduino.h>

#include <algorithm>

#include "expressions/expression_manager.hpp"
#include "expressions/shifty/shifty_expression.hpp"
#include "expressions/shimmer/shimmer_expression.hpp"
#include "expressions/transient_admission.hpp"

namespace lamp {

namespace {
constexpr uint8_t kBloom = 3;
constexpr uint32_t kMsPerS = 1000;
}  // namespace

static_assert(ShiftyExpression::maxCycleMs(SporeTint::kFadeS * kMsPerS,
                                           SporeTint::kHoldS * kMsPerS,
                                           SporeTint::kMaxFadeBackS * kMsPerS, kBloom) <
                  TransientSlots::kMaxLifetimeMs,
              "the longest spore tint must complete before the transient lifetime cap reaps it");

bool SporeTint::follow(const ExpressionInvocation& burst, uint32_t nowMs,
                       ExpressionInvocation& tint, uint32_t& afterMs) {
  const uint32_t fadeBackS = getParam(burst.parameters, "fadeDuration", 0);
  const bool spore = burst.type == ShimmerExpression::classDescriptor().id &&
                     !burst.colors.empty() && fadeBackS > 0 &&
                     ShimmerExpression::burstParams(burst.parameters);
  if (!spore) return false;
  if (booked_ && nowMs - bookedAtMs_ < busyMs_) return false;

  tint = ExpressionInvocation{};
  tint.type = ShiftyExpression::classDescriptor().id;
  tint.target = TARGET_BASE;
  tint.colors = burst.colors;
  const uint32_t backS = std::min(fadeBackS, kMaxFadeBackS);
  tint.parameters.set("fillMode", kBloom);
  tint.parameters.set("fadeDuration", kFadeS);
  tint.parameters.set("shiftDurationMin", kHoldS);
  tint.parameters.set("shiftDurationMax", kHoldS);
  tint.parameters.set("durationMax", backS);
  tint.parameters.set("opacity", kOpacityPct);

  afterMs = clampBurstMs(getParam(burst.parameters, "durationMax", 0));
  booked_ = true;
  bookedAtMs_ = nowMs;
  busyMs_ = afterMs + ShiftyExpression::maxCycleMs(kFadeS * kMsPerS, kHoldS * kMsPerS,
                                                   backS * kMsPerS, kBloom);
  return true;
}

bool playReceivedCommand(ExpressionManager& manager, SporeTint& sporeTint,
                         const ExpressionInvocation& inv, const uint8_t srcMac[6]) {
  if (!manager.triggerInvocation(inv, srcMac)) return false;
  ExpressionInvocation tint;
  uint32_t afterMs = 0;
  if (sporeTint.follow(inv, millis(), tint, afterMs)) {
    enqueueDelayedInvocation(tint, srcMac, afterMs);
  }
  return true;
}

}  // namespace lamp

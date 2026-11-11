#include "behaviors/copycat.hpp"

#include <cstring>

namespace lamp {

uint8_t copyChancePct(uint8_t basePct, uint8_t frameHops, uint8_t meets,
                      const ContagionTuning& t) {
  if (frameHops >= t.chainMaxHops) return 0;
  const uint32_t counted = meets < kFamiliarBoostMeets ? meets : kFamiliarBoostMeets;
  uint32_t pct = static_cast<uint32_t>(basePct) * (100 + counted * t.familiarBoostPct) / 100;
  for (uint8_t h = 0; h < frameHops; ++h) pct = pct * t.chainHopPct / 100;
  return pct > 100 ? 100 : static_cast<uint8_t>(pct);
}

bool setContagionTuning(ContagionTuning& t, const char* key, uint32_t value) {
  if (std::strcmp(key, "copyRollCooldownMs") == 0) {
    if (value > kMaxCopyRollCooldownMs) return false;
    t.copyRollCooldownMs = value;
  } else if (std::strcmp(key, "chainMaxHops") == 0) {
    if (value < 1 || value > kMaxChainHops) return false;
    t.chainMaxHops = static_cast<uint8_t>(value);
  } else if (std::strcmp(key, "chainHopPct") == 0) {
    if (value > 100) return false;
    t.chainHopPct = static_cast<uint8_t>(value);
  } else if (std::strcmp(key, "familiarBoostPct") == 0) {
    if (value > kMaxFamiliarBoostPct) return false;
    t.familiarBoostPct = static_cast<uint8_t>(value);
  } else {
    return false;
  }
  return true;
}

CopyDecision Copycat::decide(const uint8_t mac[6], uint8_t disposition, SocialMode mode,
                             uint8_t frameHops, uint32_t nowMs, uint8_t chanceRoll,
                             uint32_t jitterRoll) {
  CopyDecision d;
  // TODO: pass the sender's meeting count once the disposition store keeps one.
  d.pct = copyChancePct(mirrorRatePct(disposition, mode), frameHops, /*meets=*/0, tuning);
  if (d.pct == 0) return d;
  d.hops = static_cast<uint8_t>(frameHops + 1);
  if (!rolls_.tryRoll(mac, nowMs, tuning.copyRollCooldownMs)) {
    d.outcome = CopyOutcome::Cooldown;
    return d;
  }
  d.outcome = mirrorDecision(d.pct, mode, chanceRoll, jitterRoll, nowMs, cooldown_.ever,
                             cooldown_.lastMs, d.fireAt)
                  ? CopyOutcome::Fire
                  : CopyOutcome::Skip;
  return d;
}

void Copycat::retire(uint32_t nowMs) {
  cooldown_.retire(nowMs);
  rolls_.retire(nowMs, tuning.copyRollCooldownMs);
}

}  // namespace lamp

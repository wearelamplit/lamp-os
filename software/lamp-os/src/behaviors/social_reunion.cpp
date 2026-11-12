#include "behaviors/social_reunion.hpp"

#include <cstring>

#include "expressions/expression.hpp"

namespace lamp {

bool isReunion(const PeerRecord& peer, const ReunionTuning& t) {
  return peer.disp == 5 && peer.unseenMin >= t.reunionMin;
}

GreetingTuning reunionGreeting(const GreetingTuning& base, uint8_t x) {
  GreetingTuning out = base;
  out.holdFrames = base.holdFrames * x;
  out.totalFrames = base.easeInFrames + out.holdFrames + base.fadeOutFrames;
  return out;
}

bool setReunionTuning(ReunionTuning& t, const char* key, uint32_t value) {
  if (std::strcmp(key, "reunionMin") == 0) {
    if (value > UINT16_MAX) return false;
    t.reunionMin = static_cast<uint16_t>(value);
  } else if (std::strcmp(key, "reunionX") == 0) {
    if (value < 1 || value > kMaxReunionX) return false;
    t.reunionX = static_cast<uint8_t>(value);
  } else if (std::strcmp(key, "hintCooldownMs") == 0) {
    if (value > kMaxHintCooldownMs) return false;
    t.hintCooldownMs = value;
  } else if (std::strcmp(key, "hintOpacity") == 0) {
    if (value < kMinHintOpacity || value > kMaxHintOpacity) return false;
    t.hintOpacity = static_cast<uint8_t>(value);
  } else {
    return false;
  }
  return true;
}

ExpressionInvocation colourHintInvocation(const Color& colour, uint8_t opacityPct) {
  ExpressionInvocation inv;
  inv.type = "breathing";
  inv.colors = {colour};
  inv.target = TARGET_SHADE;
  inv.parameters = {{"breathSpeed", kHintBreathSeconds}, {"opacity", opacityPct}};
  return inv;
}

bool ColourHint::roll(uint8_t disposition, size_t nearby, uint8_t chanceRoll, uint64_t nowMs,
                      const ReunionTuning& t, const ContagionTuning& contagion) {
  if (disposition < kHintMinDisposition) return false;
  if (rolled_ && nowMs - lastRollMs_ < t.hintCooldownMs) return false;
  rolled_ = true;
  lastRollMs_ = nowMs;
  // Hop 0 and no meetings leave only the crowd damping on a sure chance.
  return chanceRoll <= copyChancePct(100, 0, 0, nearby, contagion);
}

}  // namespace lamp

#include "behaviors/yawn_contagion.hpp"

#include <cstring>
#include <tuple>

#include <lampos/clock.hpp>

namespace lamp {

namespace {

constexpr std::array<const char*, 5> kBackPctKeys = {"yawn.salty", "yawn.wary", "yawn.neutral",
                                                     "yawn.fond", "yawn.smitten"};
static_assert(kBackPctKeys.size() == std::tuple_size_v<decltype(YawnTuning::backPct)>);

}  // namespace

uint8_t yawnBackPct(uint8_t disposition, SocialMode mode, uint8_t frameHops, uint8_t meets,
                    size_t nearby, const YawnTuning& y, const ContagionTuning& c) {
  if (disposition < 1 || disposition > y.backPct.size()) return 0;
  const uint8_t modeIdx = static_cast<uint8_t>(mode);
  if (modeIdx >= 3) return 0;
  uint32_t base = static_cast<uint32_t>(y.backPct[disposition - 1]) *
                  kMirrorModeFactorX10[modeIdx] / 10;
  if (base > 100) base = 100;
  return copyChancePct(static_cast<uint8_t>(base), frameHops, meets, nearby, c);
}

bool setYawnTuning(YawnTuning& t, const char* key, uint32_t value) {
  for (size_t i = 0; i < t.backPct.size(); ++i) {
    if (std::strcmp(key, kBackPctKeys[i]) == 0) {
      if (value > 100) return false;
      t.backPct[i] = static_cast<uint8_t>(value);
      return true;
    }
  }
  if (std::strcmp(key, "yawnRollCooldownMs") == 0) {
    if (value > kMaxYawnWindowMs) return false;
    t.yawnRollCooldownMs = value;
  } else if (std::strcmp(key, "yawnSelfFloorMs") == 0) {
    if (value > kMaxYawnWindowMs) return false;
    t.yawnSelfFloorMs = value;
  } else if (std::strcmp(key, "tiredYawnPct") == 0) {
    if (value > 100) return false;
    t.tiredYawnPct = static_cast<uint8_t>(value);
  } else if (std::strcmp(key, "tiredYawnEveryMs") == 0) {
    if (value < kMinTiredYawnEveryMs || value > kMaxYawnWindowMs) return false;
    t.tiredYawnEveryMs = value;
  } else {
    return false;
  }
  return true;
}

TiredRoll YawnContagion::tiredRoll(bool tired, uint32_t nowMs, uint8_t chanceRoll,
                                   bool yawnedRecently) {
  if (!tired) {
    wasTired_ = false;
    return TiredRoll::NotDue;
  }
  if (!wasTired_) {
    wasTired_ = true;
    lastTiredRollMs_ = nowMs;
    return TiredRoll::NotDue;
  }
  if (lampos::msSince(nowMs, lastTiredRollMs_) < tuning.tiredYawnEveryMs) return TiredRoll::NotDue;
  lastTiredRollMs_ = nowMs;
  if (yawnedRecently) return TiredRoll::SelfFloor;
  return chanceRoll <= tuning.tiredYawnPct ? TiredRoll::Fire : TiredRoll::Skip;
}

CopyDecision YawnContagion::catchYawn(const uint8_t mac[6], uint8_t disposition,
                                      SocialMode mode, uint8_t frameHops, uint8_t meets,
                                      uint32_t nowMs,
                                      uint8_t chanceRoll, uint32_t jitterRoll, size_t nearby,
                                      bool yawnedRecently, const ContagionTuning& contagion) {
  CopyDecision d;
  d.pct = yawnBackPct(disposition, mode, frameHops, meets, nearby, tuning, contagion);
  if (d.pct == 0) return d;
  d.hops = static_cast<uint8_t>(frameHops + 1);
  if (yawnedRecently) {
    d.outcome = CopyOutcome::SelfEcho;
    return d;
  }
  if (!rolls_.tryRoll(mac, nowMs, tuning.yawnRollCooldownMs)) {
    d.outcome = CopyOutcome::Cooldown;
    return d;
  }
  d.outcome = mirrorDecision(d.pct, mode, chanceRoll, jitterRoll, nowMs, /*everMirrored=*/false,
                             0, d.fireAt)
                  ? CopyOutcome::Fire
                  : CopyOutcome::Skip;
  return d;
}

}  // namespace lamp

#include "behaviors/social_echo.hpp"

#include <Arduino.h>

#include <cstring>

#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "core/fondness_engine.hpp"
#include "expressions/expression_manager.hpp"
#include "expressions/yawn/yawn_expression.hpp"
#include "util/uptime.hpp"

namespace lamp {

namespace {

#ifdef LAMP_DEBUG
const char* outcomeName(CopyOutcome o) {
  return o == CopyOutcome::Fire       ? "fired"
         : o == CopyOutcome::Cooldown ? "cooldown"
         : o == CopyOutcome::SelfEcho ? "self"
                                      : "skip";
}

void printMac(const uint8_t m[6]) {
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}
#endif

}  // namespace

bool SocialEchoObserver::tracking() const { return fondness_ && fondness_->tracking(); }

bool SocialEchoObserver::yawnPending() const {
  for (const auto& p : pending_) {
    if (p.used && p.yawnBack) return true;
  }
  return false;
}

bool SocialEchoObserver::yawnBooked() const {
  for (const auto& p : pending_) {
    if (p.used && p.inv.type == kYawnDescriptorData.id) return true;
  }
  return false;
}

bool SocialEchoObserver::yawnedRecently(uint32_t nowMs) const {
  return yawnBooked() ||
         manager_.playedWithin(kYawnDescriptorData.id, yawns_.tuning.yawnSelfFloorMs, nowMs);
}

uint8_t SocialEchoObserver::boostMeets(const DispositionStore& store, const uint8_t mac[6],
                                       uint64_t nowMs) {
  return store.engineEnabled() ? store.peer(mac, nowMs).meets : 0;
}

void SocialEchoObserver::onPeerExpression(const uint8_t sourceMac[6],
                                          const ExpressionInvocation& inv) {
  if (manager_.inHomeMode()) return;
  if (mesh_.isOtaInProgress()) return;
  if (manager_.isAnyTestActive()) return;

  const uint32_t now = millis();
  if (!tracking()) {
    const CopyDecision d =
        copycat_.mirror(config_.getDisposition(sourceMac), config_.lamp.socialMode, now,
                        static_cast<uint8_t>(rng_.range(1, 100)), rng_.range(0, kMirrorJitterMs));
    if (d.outcome == CopyOutcome::Fire &&
        book(inv, sourceMac, d, /*yawnBack=*/false, /*chained=*/false)) {
      copycat_.booked(now);
    }
    return;
  }
  if (inv.type == kYawnDescriptorData.id) {
    catchYawn(sourceMac, inv, now);
    return;
  }
  const uint8_t meets = boostMeets(config_.dispositions(), sourceMac, uptimeMs());
  const CopyDecision d = copycat_.decide(
      sourceMac, config_.getDisposition(sourceMac), config_.lamp.socialMode, inv.hops, now,
      static_cast<uint8_t>(rng_.range(1, 100)), rng_.range(0, kMirrorJitterMs),
      nearbyCount_ ? nearbyCount_() : 0,
      manager_.playedWithin(inv.type.c_str(), copycat_.tuning.copyRollCooldownMs, now), meets);
  if (d.outcome == CopyOutcome::NoChance) return;
#ifdef LAMP_DEBUG
  // Print::printf mallocs a line of 64 B or more. Two prints, the peer-sent type
  // cut to 32, keep each under it.
  Serial.printf("[fondness] roll %.32s from ", inv.type.c_str());
  printMac(sourceMac);
  Serial.printf(" hop %u p=%u%% %s\n", static_cast<unsigned>(d.hops),
                static_cast<unsigned>(d.pct), outcomeName(d.outcome));
#endif
  if (d.outcome != CopyOutcome::Fire) return;
  if (book(inv, sourceMac, d, /*yawnBack=*/false, /*chained=*/true)) copycat_.booked(now);
}

void SocialEchoObserver::catchYawn(const uint8_t sourceMac[6], const ExpressionInvocation& inv,
                                   uint32_t nowMs) {
  const CopyDecision d = yawns_.catchYawn(
      sourceMac, config_.getDisposition(sourceMac), config_.lamp.socialMode, inv.hops,
      boostMeets(config_.dispositions(), sourceMac, uptimeMs()), nowMs,
      static_cast<uint8_t>(rng_.range(1, 100)), rng_.range(0, kMirrorJitterMs),
      nearbyCount_ ? nearbyCount_() : 0,
      yawnedRecently(nowMs), copycat_.tuning);
  if (d.outcome == CopyOutcome::NoChance) return;
#ifdef LAMP_DEBUG
  Serial.print("[fondness] yawn from ");
  printMac(sourceMac);
  Serial.printf(" hop %u p=%u%% %s\n", static_cast<unsigned>(d.hops),
                static_cast<unsigned>(d.pct), outcomeName(d.outcome));
#endif
  if (d.outcome == CopyOutcome::Fire) {
    book(inv, sourceMac, d, /*yawnBack=*/true, /*chained=*/true);
  }
}

bool SocialEchoObserver::book(const ExpressionInvocation& inv, const uint8_t sourceMac[6],
                              const CopyDecision& d, bool yawnBack, bool chained) {
  Pending* slot = nullptr;
  for (auto& p : pending_) {
    if (!p.used) { slot = &p; break; }
  }
  if (!slot) return false;

  slot->inv = inv;
  slot->inv.hops = d.hops;
  std::memcpy(slot->srcMac, sourceMac, 6);
  slot->fireAt = d.fireAt;
  slot->used = true;
  slot->yawnBack = yawnBack;
  slot->chained = chained;
  return true;
}

void SocialEchoObserver::tickTired(uint32_t nowMs) {
  const bool tired = tired_ && tired_() && !mesh_.isOtaInProgress() &&
                     !manager_.isAnyTestActive() && tracking();
  const TiredRoll r = yawns_.tiredRoll(
      tired, nowMs, static_cast<uint8_t>(rng_.range(1, 100)), yawnedRecently(nowMs));
  if (r == TiredRoll::NotDue) return;
#ifdef LAMP_DEBUG
  Serial.printf("[fondness] yawn tired p=%u%% %s\n",
                static_cast<unsigned>(yawns_.tuning.tiredYawnPct),
                r == TiredRoll::Fire ? "fired" : r == TiredRoll::SelfFloor ? "self" : "skip");
#endif
  if (r != TiredRoll::Fire) return;
  ExpressionInvocation inv;
  inv.type = kYawnDescriptorData.id;
  uint8_t own[6];
  mesh_.getMyMac(own);
  manager_.replayAndAnnounce(inv, own, TransientSource::Local);
}

void SocialEchoObserver::tick(uint32_t nowMs) {
  copycat_.retire(nowMs);
  yawns_.retire(nowMs);
  tickTired(nowMs);
  // A copy booked before home mode began is dropped.
  if (manager_.inHomeMode()) {
    for (auto& p : pending_) p.used = false;
    return;
  }
  // A yawn-back booked before the engine went off is dropped.
  if (yawnPending() && !tracking()) {
    for (auto& p : pending_) {
      if (p.yawnBack) p.used = false;
    }
  }
  for (auto& p : pending_) {
    if (!p.used) continue;
    if (static_cast<int32_t>(nowMs - p.fireAt) < 0) continue;
    if (p.chained && tracking() && copycat_.announces(p.inv.hops)) {
      manager_.replayAndAnnounce(p.inv, p.srcMac);
    } else {
      manager_.triggerInvocation(p.inv, p.srcMac);
    }
    p.used = false;
  }
}

}  // namespace lamp

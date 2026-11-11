#include "behaviors/social_echo.hpp"

#include <Arduino.h>

#include <cstring>

#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "expressions/expression_manager.hpp"

namespace lamp {

void SocialEchoObserver::onPeerExpression(const uint8_t sourceMac[6],
                                          const ExpressionInvocation& inv) {
  if (manager_.inHomeMode()) return;
  if (mesh_.isOtaInProgress()) return;
  if (manager_.isAnyTestActive()) return;

  const uint32_t now = millis();
  const CopyDecision d =
      copycat_.decide(sourceMac, config_.getDisposition(sourceMac), config_.lamp.socialMode,
                      inv.hops, now, static_cast<uint8_t>(rng_.range(1, 100)),
                      rng_.range(0, kMirrorJitterMs));
  if (d.outcome == CopyOutcome::NoChance) return;
#ifdef LAMP_DEBUG
  const char* outcome = d.outcome == CopyOutcome::Fire       ? "fired"
                        : d.outcome == CopyOutcome::Cooldown ? "cooldown"
                                                             : "skip";
  Serial.printf("[fondness] roll %s from %02X:%02X:%02X:%02X:%02X:%02X hop %u p=%u%% %s\n",
                inv.type.c_str(), sourceMac[0], sourceMac[1], sourceMac[2], sourceMac[3],
                sourceMac[4], sourceMac[5], static_cast<unsigned>(d.hops),
                static_cast<unsigned>(d.pct), outcome);
#endif
  if (d.outcome != CopyOutcome::Fire) return;

  Pending* slot = nullptr;
  for (auto& p : pending_) {
    if (!p.used) { slot = &p; break; }
  }
  if (!slot) return;

  slot->inv = inv;
  slot->inv.hops = d.hops;
  std::memcpy(slot->srcMac, sourceMac, 6);
  slot->fireAt = d.fireAt;
  slot->used = true;
  copycat_.booked(now);
}

void SocialEchoObserver::tick(uint32_t nowMs) {
  copycat_.retire(nowMs);
  // A copy booked before home mode began is dropped.
  if (manager_.inHomeMode()) {
    for (auto& p : pending_) p.used = false;
    return;
  }
  for (auto& p : pending_) {
    if (!p.used) continue;
    if (static_cast<int32_t>(nowMs - p.fireAt) < 0) continue;
    if (copycat_.announces(p.inv.hops)) {
      manager_.replayAndAnnounce(p.inv, p.srcMac);
    } else {
      manager_.triggerInvocation(p.inv, p.srcMac);
    }
    p.used = false;
  }
}

}  // namespace lamp

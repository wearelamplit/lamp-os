#include "behaviors/social_echo.hpp"

#include <Arduino.h>

#include <cstring>

#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "expressions/expression_manager.hpp"
#include "util/bd_addr.hpp"

namespace lamp {

void SocialEchoObserver::onPeerExpression(const uint8_t sourceMac[6],
                                          const ExpressionInvocation& inv) {
  if (mesh_.isOtaInProgress()) return;
  if (manager_.isAnyTestActive()) return;

  char macStr[18];
  formatBdAddr(sourceMac, macStr);
  const uint8_t disp = config_.getDisposition(macStr);

  const uint32_t now = millis();
  uint32_t fireAt = 0;
  if (!mirrorDecision(disp, config_.lamp.socialMode,
                      static_cast<uint8_t>(rng_.range(1, 100)),
                      rng_.range(0, kMirrorJitterMs), now, everMirrored_,
                      lastMirrorMs_, fireAt)) {
    return;
  }

  Pending* slot = nullptr;
  for (auto& p : pending_) {
    if (!p.used) { slot = &p; break; }
  }
  if (!slot) return;

  slot->inv = inv;
  std::memcpy(slot->srcMac, sourceMac, 6);
  slot->fireAt = fireAt;
  slot->used = true;
  lastMirrorMs_ = now;
  everMirrored_ = true;
}

void SocialEchoObserver::tick(uint32_t nowMs) {
  for (auto& p : pending_) {
    if (!p.used) continue;
    if (static_cast<int32_t>(nowMs - p.fireAt) < 0) continue;
    manager_.triggerInvocation(p.inv, p.srcMac);
    p.used = false;
  }
}

}  // namespace lamp

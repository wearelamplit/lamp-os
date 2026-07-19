#pragma once

#include <cstdint>

namespace webapp {

// Boot-window expiry decision, inline so it's unit-testable without WiFi.
// `neverExpire` short-circuits (apBootMinutes==0 keeps the AP up); the finite
// path is millis()-wrap-safe via signed difference.
inline bool webappShouldTeardown(uint32_t now, uint32_t deadlineMs,
                                 bool neverExpire) {
  return !neverExpire && static_cast<int32_t>(now - deadlineMs) >= 0;
}

}  // namespace webapp

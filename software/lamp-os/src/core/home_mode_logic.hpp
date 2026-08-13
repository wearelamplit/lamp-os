#pragma once

namespace lamp {

// Pure predicate for the no-BT-client home-mode decision.
// Extracted for native testability (no BLE/WiFi deps).
inline bool effectiveHomeModeFromConfig(bool enabled, bool networkBound,
                                        bool ssidEmpty, bool ssidVisible) {
  if (!enabled) return false;
  if (!networkBound) return true;
  return !ssidEmpty && ssidVisible;
}

}  // namespace lamp

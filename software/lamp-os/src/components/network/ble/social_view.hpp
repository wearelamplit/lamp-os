#pragma once

#include <cstdint>

#include "components/network/ble/nearby_list.hpp"

namespace ble_control {

// Applies a CHAR_EDIT_SESSION write's 0x08 selector (the app on its Social
// page) and returns whether the Social view is active after it. A close goes
// through during OTA quiet, so leaving Social frees the nearby list then too;
// a reconnect replays the app's last flags, so only a close after an open
// ends the list's hold. An open waits out OTA quiet like the edit surfaces.
inline bool applySocialViewWrite(bool active, uint8_t surface, bool open, bool quiet,
                                 NearbyList& nearby) {
  if (!(surface & 0x08)) return active;
  if (!open) {
    if (active) nearby.endHold();
    return false;
  }
  return quiet ? active : true;
}

}  // namespace ble_control

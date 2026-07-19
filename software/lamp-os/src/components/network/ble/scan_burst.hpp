#pragma once
#include <cstdint>

namespace ble_control {

// On-demand central-scan burst gate. While an app holds the GATT link the
// scan is otherwise hard-off, so BLE-only legacy lamps never surface in the
// roster; a nearby-read arms one short burst to catch them. Returns true when
// a fresh burst may start: a request is pending, none is in flight, a client
// is connected, and the last burst is at least minIntervalMs old.
inline bool scanBurstReady(bool requested, bool active, bool connected,
                           uint32_t now, uint32_t lastBurstMs,
                           uint32_t minIntervalMs) {
  if (!connected || !requested || active) return false;
  if (lastBurstMs != 0 && (now - lastBurstMs) < minIntervalMs) return false;
  return true;
}

}  // namespace ble_control

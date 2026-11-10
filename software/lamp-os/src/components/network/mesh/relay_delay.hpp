#pragma once

#include <cstdint>

namespace lamp {

// Delay in [minMs, maxMs] before the lamp with `ownMac` relays the frame
// (sourceMac, msgType, seq). Deterministic, spread evenly over the range, and
// the offset between any two relayers changes from frame to frame, so no pair
// of lamps relays in step on every frame.
uint32_t relayDelayMs(const uint8_t ownMac[6], const uint8_t sourceMac[6],
                      uint8_t msgType, uint16_t seq, uint32_t minMs,
                      uint32_t maxMs);

}  // namespace lamp

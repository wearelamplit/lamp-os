#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class WispRoster;

// Build a whole MSG_WISP_STATE frame into `buf`: the roster's painted claims
// as they show at `nowMs`, with their W plane, when `painting`; no entries
// otherwise (lamps find themselves absent and ease home). Returns the frame
// length, 0 on build failure.
size_t buildStateFrame(WispRoster& roster, uint8_t* buf, size_t bufLen,
                       uint16_t seq, const uint8_t srcMac[6],
                       uint8_t brightness, uint32_t driftRateMs,
                       uint8_t presenceFlags, bool painting, uint32_t nowMs);

}  // namespace wisp

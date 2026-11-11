#include "status/state_frame.hpp"

#include "fleet/wisp_roster.hpp"
#include "wire/lamp_protocol.hpp"

namespace wisp {

size_t buildStateFrame(WispRoster& roster, uint8_t* buf, size_t bufLen,
                       uint16_t seq, const uint8_t srcMac[6],
                       uint8_t brightness, uint32_t driftRateMs,
                       uint8_t presenceFlags, bool painting, uint32_t nowMs) {
  uint8_t entries[lamp_protocol::WISP_STATE_MAX_ENTRIES *
                  lamp_protocol::WISP_STATE_ENTRY_SIZE];
  uint8_t w[lamp_protocol::WISP_STATE_MAX_ENTRIES *
            lamp_protocol::WISP_STATE_W_SIZE];
  const size_t count =
      painting
          ? roster.snapshotStateForBroadcast(entries, sizeof(entries), w,
                                             sizeof(w), nowMs)
          : 0;
  return lamp_protocol::buildWispState(
      buf, bufLen, seq, srcMac, brightness, driftRateMs, presenceFlags,
      count > 0 ? entries : nullptr, count > 0 ? w : nullptr,
      static_cast<uint8_t>(count));
}

}  // namespace wisp

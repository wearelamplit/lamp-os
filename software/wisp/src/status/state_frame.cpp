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
  // static: keeps the record scratch off the 8 KB loop stack; the loop task is
  // the only builder.
  static uint8_t records[lamp_protocol::WISP_STATE_DRIFT_FADE_MAX_RECORDS *
                         lamp_protocol::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
  WispRoster::DriftFadeRecords fades;
  fades.buf = records;
  fades.cap = sizeof(records);
  const size_t count =
      painting
          ? roster.snapshotStateForBroadcast(entries, sizeof(entries), w,
                                             sizeof(w), nowMs, &fades)
          : 0;
  const size_t len = lamp_protocol::buildWispState(
      buf, bufLen, seq, srcMac, brightness, driftRateMs, presenceFlags,
      count > 0 ? entries : nullptr, count > 0 ? w : nullptr,
      static_cast<uint8_t>(count));
  if (len == 0 || count == 0) return len;
  return lamp_protocol::appendWispStateDriftFade(buf, bufLen, len, records,
                                                 fades.count, fades.partial);
}

}  // namespace wisp

#include "components/network/mesh/pending_slots.hpp"

#include <cstring>

namespace lamp {

void fillPendingWispState(PendingWispState& slot,
                          const lamp_protocol::ParsedWispState& ws,
                          const uint8_t selfMac[6]) {
  std::memcpy(slot.sourceMac, ws.sourceMac, 6);
  slot.count = ws.count;
  const size_t n = ws.entries ? ws.count : 0;
  if (n) {
    std::memcpy(slot.entries, ws.entries,
                n * lamp_protocol::WISP_STATE_ENTRY_SIZE);
  }
  if (n && ws.w) {
    std::memcpy(slot.w, ws.w, n * lamp_protocol::WISP_STATE_W_SIZE);
  } else {
    std::memset(slot.w, 0, n * lamp_protocol::WISP_STATE_W_SIZE);
  }
  slot.fade = lamp_protocol::WispDriftFadeLookup::None;
  const uint8_t* own =
      selfMac ? lamp_protocol::findWispStateEntry(ws.entries, ws.count, selfMac)
              : nullptr;
  if (own) {
    const auto idx = static_cast<uint8_t>((own - ws.entries) /
                                          lamp_protocol::WISP_STATE_ENTRY_SIZE);
    slot.fade = lamp_protocol::lookupWispDriftFade(ws.trailer, ws.trailerLen,
                                                   idx, slot.fadeRecord);
  }
}

bool wispStateColorsFor(const PendingWispState& slot, const uint8_t mac[6],
                        Color& base, Color& shade) {
  const uint8_t* e =
      lamp_protocol::findWispStateEntry(slot.entries, slot.count, mac);
  if (!e) return false;
  const size_t i = static_cast<size_t>(e - slot.entries) /
                   lamp_protocol::WISP_STATE_ENTRY_SIZE;
  const uint8_t* w = &slot.w[i * lamp_protocol::WISP_STATE_W_SIZE];
  base = Color(e[6], e[7], e[8], w[0]);
  shade = Color(e[9], e[10], e[11], w[1]);
  return true;
}

}  // namespace lamp

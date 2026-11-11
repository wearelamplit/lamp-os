#include "components/network/mesh/pending_slots.hpp"

#include <cstring>

namespace lamp {

OwnWispDriftFade decodeOwnWispDriftFade(const lamp_protocol::ParsedWispState& ws,
                                        const uint8_t selfMac[6]) {
  OwnWispDriftFade own;
  const uint8_t* e =
      lamp_protocol::findWispStateEntry(ws.entries, ws.count, selfMac);
  if (e) {
    own.index = static_cast<uint8_t>((e - ws.entries) /
                                     lamp_protocol::WISP_STATE_ENTRY_SIZE);
    own.fade = lamp_protocol::lookupWispDriftFade(
        ws.trailer, ws.trailerLen, own.index, own.record, &own.recordWritten);
  }
  own.trailer = lamp_protocol::walkWispDriftFade(ws.trailer, ws.trailerLen,
                                                 [](const uint8_t*) {});
  return own;
}

void fillPendingWispState(PendingWispState& slot,
                          const lamp_protocol::ParsedWispState& ws,
                          const OwnWispDriftFade& own) {
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
  slot.fade = own.fade;
  if (own.recordWritten) slot.fadeRecord = own.record;
  slot.ownIndex = own.index;
  slot.fadePartial = own.trailer == lamp_protocol::WispDriftFadeTrailer::Partial;
  std::memset(slot.targeted, 0, sizeof(slot.targeted));
  if (own.trailer == lamp_protocol::WispDriftFadeTrailer::Malformed) return;
  lamp_protocol::walkWispDriftFade(ws.trailer, ws.trailerLen, [&](const uint8_t* r) {
    const uint8_t i = r[0];
    if (i >= n || i == own.index) return;
    uint8_t* e = &slot.entries[i * lamp_protocol::WISP_STATE_ENTRY_SIZE];
    uint8_t* w = &slot.w[i * lamp_protocol::WISP_STATE_W_SIZE];
    std::memcpy(e + 6, r + 1, 3);
    w[0] = r[4];
    std::memcpy(e + 9, r + 5, 3);
    w[1] = r[8];
    slot.targeted[i / 8] |= static_cast<uint8_t>(1u << (i % 8));
  });
}

void fillPendingWispClaim(PendingWispClaim& slot,
                          const lamp_protocol::ParsedWispClaim& wc) {
  std::memcpy(slot.sourceMac, wc.sourceMac, 6);
  const uint8_t count =
      wc.count > lamp_protocol::kMaxWispClaimEntries
          ? static_cast<uint8_t>(lamp_protocol::kMaxWispClaimEntries)
          : wc.count;
  slot.count = count;
  for (uint8_t i = 0; i < count; ++i) {
    // An entry is lampMac[6] + int8 rssi; the slot keeps only the MAC.
    std::memcpy(slot.lampMacs[i],
                wc.entries + static_cast<size_t>(i) *
                                 lamp_protocol::WISP_CLAIM_ENTRY_SIZE,
                6);
  }
}

void fillPendingCommand(PendingCommand& slot,
                        const lamp_protocol::ParsedCommand& p) {
  std::memcpy(slot.sourceMac, p.sourceMac, 6);
  slot.payloadLen = static_cast<uint16_t>(p.payloadLen);
  std::memcpy(slot.payload, p.payload, p.payloadLen);
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

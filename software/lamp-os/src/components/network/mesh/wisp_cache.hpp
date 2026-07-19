#pragma once

#include <cstdint>
#include <string>

#include "components/network/protocol/lamp_protocol.hpp"

namespace lamp {

/**
 * Display-slot wisp cache: the single wisp whose hello/status/palette
 *        the lamp serves to the app. Admission is sticky: a rival wisp's
 *        broadcasts are rejected while the current wisp is fresh (last
 *        hello or adoption within the sticky window),
 *        except a claim frame naming this lamp, which takes the slot when
 *        the current wisp does not claim it. The paint-hold and
 *        brightness-floor obey gates key on LampRoster's dedicated painter
 *        fields, not this slot.
 */
struct WispCache {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t lastHelloMs = 0;
  // Stamped when the slot is adopted; counts toward slot freshness so a
  // just-adopted wisp (claim takeover, first paint) is sticky before its
  // first hello is cached. Never served to the app.
  uint32_t slotAdoptedMs = 0;
  uint32_t wispVersion = 0;
  uint8_t flags = 0;
  // +1 for trailing NUL; on-wire slot is 8 bytes opaque (not ASCII-enforced).
  char paletteIdPrefix[9] = {0};
  char carriedFwChannel[9] = {0};
  uint32_t carriedFwVersion = 0;
  // Last wispStatus JSON broadcast for this wisp (verbatim
  // payload). Served on CHAR_WISP_STATUS reads merged with the hello
  // fields above. Empty until the first wispStatus has been seen.
  std::string lastStatusJson;
  uint32_t lastStatusMs = 0;
  // Latest MSG_WISP_PALETTE from this wisp. Served base64-encoded as
  // getWispStatusReadJson()'s `palette` field on READ only (NOTIFY omits
  // it; MTU truncation would corrupt it). Interleaved R,G,B,W per color;
  // 200 bytes = 50 colors * 4.
  uint8_t manualPaletteRgbw[200] = {0};
  uint8_t manualPaletteCount = 0;
  uint32_t lastPaletteMs = 0;
};

}  // namespace lamp

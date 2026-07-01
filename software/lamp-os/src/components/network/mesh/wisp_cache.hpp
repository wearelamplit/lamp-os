#pragma once

#include <cstdint>
#include <string>

#include "components/network/protocol/lamp_protocol.hpp"

namespace lamp {

/**
 * @brief Wisp presence cache from MSG_WISP_HELLO. Single global slot; most
 *        recent hello wins. The brightness-floor check in MeshLink
 *        reads this to decide whether a below-floor brightness override is
 *        allowed (requires a recent hello from the same MAC).
 */
struct WispCache {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t lastHelloMs = 0;
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
  // it; MTU truncation would corrupt it). 150 bytes = 50 colors * 3.
  uint8_t manualPaletteRgb[150] = {0};
  uint8_t manualPaletteCount = 0;
  uint32_t lastPaletteMs = 0;
  // Latest MSG_WISP_CLAIM roster. Served via CHAR_WISP_CLAIMS as a binary
  // blob so the app can filter its painted-lamps list.
  uint8_t claimedLampMacs[lamp_protocol::kMaxWispClaimEntries][6] = {};
  uint8_t claimedCount = 0;
  uint32_t lastClaimMs = 0;
};

}  // namespace lamp

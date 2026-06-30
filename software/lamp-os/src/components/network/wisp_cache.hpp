#pragma once

#include <cstdint>
#include <string>

namespace lamp {

/**
 * @brief Wisp presence cache populated from MSG_WISP_HELLO. Single global
 *        slot — the most recent hello wins. The brightness-floor check in
 *        MeshLink reads from this struct to decide whether an incoming
 *        brightness-override below kBrightnessOverrideMin is allowed (yes
 *        if a recent hello from the same MAC is on file).
 */
struct WispCache {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t lastHelloMs = 0;
  uint32_t wispVersion = 0;
  uint8_t flags = 0;
  // +1 for trailing NUL so logging the string is safe; the on-wire slot
  // is 8 bytes opaque so we don't enforce ASCII.
  char paletteIdPrefix[9] = {0};
  char carriedFwChannel[9] = {0};
  uint32_t carriedFwVersion = 0;
  // Last wispStatus JSON broadcast for this wisp (verbatim
  // payload). Served on CHAR_WISP_STATUS reads merged with the hello
  // fields above. Empty until the first wispStatus has been seen.
  std::string lastStatusJson;
  uint32_t lastStatusMs = 0;
  // Latest MSG_WISP_PALETTE the lamp has heard from this wisp. The wisp
  // emits this alongside wispStatus every 30 s + on-change so the app's
  // wisp editor can read the canonical palette through any connected
  // lamp. Served base64-encoded as getWispStatusReadJson()'s `palette`
  // field, on the READ leg only (the NOTIFY leg omits it — MTU).
  // Capacity matches lamp_protocol::kMaxWispPaletteColors * 3 = 150 bytes.
  uint8_t manualPaletteRgb[150] = {0};
  uint8_t manualPaletteCount = 0;
  uint32_t lastPaletteMs = 0;
};

}  // namespace lamp

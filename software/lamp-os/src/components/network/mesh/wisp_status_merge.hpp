#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lamp {

// NimBLE caps a single characteristic value at 512 B (BLE_ATT_ATTR_MAX_LEN in
// the vendored ble_att.h); a longer setValue stores a zero-length value.
inline constexpr size_t kWispStatusCharMaxLen = 512;

// Everything the served wispStatus JSON merges, copied out of the display
// slot so the build runs outside the roster mutex.
struct WispStatusMergeInput {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t wispVersion = 0;
  uint8_t flags = 0;
  uint32_t lastHelloMs = 0;
  char helloPaletteIdPrefix[9] = {0};
  std::string statusJson;
  uint32_t lastStatusMs = 0;
  uint32_t lastConfigMs = 0;
  bool haveLampState = false;
  bool controllingBase = false;
  bool controllingShade = false;
  std::string baseWispColor;
  std::string shadeWispColor;
};

// The merged JSON served on CHAR_WISP_STATUS and the `wispstatus` page
// section: the wisp's cached wispStatus plus the lamp's own hello, staleness
// and wisp-control fields. "{}" when nothing is known. With maxLen > 0 the
// lamp-side diagnostic fields no app reads are shed until the document fits;
// it can still exceed maxLen when the wisp's own payload does. Empty when the
// heap could not hold the document; may throw std::bad_alloc.
std::string mergeWispStatusJson(const WispStatusMergeInput& in, size_t maxLen);

// Replace `cached` with the payload when its bytes differ, returning whether
// they did. The wisp re-sends an unchanged config through a change burst; only
// a real change should move configLastSeenMs and cost the app a re-read.
bool storeIfChanged(std::string& cached, const char* json, size_t len);

// True when `json` parses as a JSON object. A malformed wispConfig is dropped
// before it can replace the cached one or move configLastSeenMs.
bool isJsonObject(const char* json, size_t len);

}  // namespace lamp

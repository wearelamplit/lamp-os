#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

struct WispStatusFields {
  int          currentZone;
  const char*  zoneSource;
  const int*   observedZones;
  size_t       observedCount;
  bool         wifiConnected;
  bool         auroraConnected;
  const char*  paletteIdPrefix;
  uint32_t     lastSeenMs;
  const char*  source;
  uint8_t      offR, offG, offB, offW;
  bool         hasOffColor;
  uint8_t      shuffleSeed;
  uint32_t     driftIntervalMs;
  uint8_t      driftFadePct;
  const char*  name;
  bool         hasPassword;
  const char*  ledType;
  uint16_t     pixelCount;
  uint8_t      rangeStep;
  uint32_t     opSeq;
  uint8_t      brightness = 100;
};

// Serialize a wispStatus JSON into `out` (capacity `outCap`). A guaranteed
// core always fits under `cap`; every other field is added only if the
// document still fits (fields matching the app parser's defaults are
// omitted). Never returns 0 when outCap > cap. See docs/dev/networking.md
// for the field priority order.
size_t buildWispStatusJson(const WispStatusFields& f, char* out,
                           size_t outCap, size_t cap);

}  // namespace wisp

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// Live state: what the wisp is doing right now. Rides wispStatus.
struct WispStatusFields {
  bool         wifiConnected;
  bool         sourceLive;
  const char*  paletteIdPrefix;
  const char*  source;
  const char*  oscRole;  // omitted at "primary"
  uint8_t      offR, offG, offB, offW;
  bool         hasOffColor;
  const char*  name;
  bool         hasPassword;
  bool         wifiChannelMismatch = false;
  int          wifiApChannel = 0;
  const char*  ethIp = nullptr;  // omitted when Ethernet is not up
};

// Settings the app edits, plus opSeq, which confirms a sealed op landed.
// Rides wispConfig.
struct WispConfigFields {
  const char*  ledType = "GRB";
  uint16_t     pixelCount = 30;
  uint8_t      brightness = 100;
  uint32_t     driftIntervalMs = 120000;
  uint8_t      driftFadePct = 50;
  uint8_t      shuffleSeed = 0;
  uint32_t     opSeq = 0;
};

// Serialize a wispStatus JSON into `out` (capacity `outCap`). A guaranteed
// core always fits under `cap`; every other field is added only if the
// document still fits (fields matching the app parser's defaults are
// omitted). `configCopy`, when given, adds the config fields at the lowest
// priority for lamps that predate wispConfig. lastSeenMs is the wisp's
// uptime at the call (wisp::uptimeMs()). Never returns 0 when outCap > cap. See docs/dev/networking.md for the field priority order.
size_t buildWispStatusJson(const WispStatusFields& f,
                           const WispConfigFields* configCopy, char* out,
                           size_t outCap, size_t cap);

// Serialize a wispConfig JSON into `out`. Every field is always present, so
// a received wispConfig is complete and an absent key never stands for a
// default. Returns 0 when `outCap` is too small.
size_t buildWispConfigJson(const WispConfigFields& f, char* out,
                           size_t outCap);

}  // namespace wisp

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
  uint8_t      offR, offG, offB;
  bool         hasOffColor;
  uint8_t      shuffleSeed;
  uint32_t     driftIntervalMs;
  uint8_t      driftFadePct;
  // Emitted before the droppable fields; max 20 chars enforced by WispConfig::setName.
  const char*  name;
  bool         hasPassword;
};

// Serialize a wispStatus JSON into `out` (capacity `outCap`), truncating
// observedZones greedily so the result never exceeds `cap` bytes.
// Returns the serialized length, or 0 on failure (alloc or oversize with
// zero zones).
size_t buildWispStatusJson(const WispStatusFields& f, char* out,
                           size_t outCap, size_t cap);

}  // namespace wisp

// LampScanner — low-duty BLE scan for the legacy (pre-mesh) lamp beacon.
//
// Stamps a recency timestamp whenever an old lamp is seen so the controller
// can gate softAP + stage-beacon serving on actual old-lamp presence and stay
// pure-mesh otherwise. NimBLE must already be initialized (StageBeacon::begin)
// before begin(); this class never init/deinits NimBLE.
//
// The result callback runs on the NimBLE host task; sawOldLampWithin() reads
// from the loop task. The single 32-bit recency timestamp is the only
// cross-task state (aligned 32-bit access is atomic on the C6).

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

// Old lamp = lamp magic (42069, LE) in bytes 0-1 AND an 8-byte payload, the
// legacy [magic, baseRGB, shadeRGB] shape with no mesh-capability byte. The
// 6- and 9-byte shapes carry the mesh capability bit, so they are mesh lamps.
bool isOldLampBeacon(const uint8_t* data, size_t len);

class LampScanner {
 public:
  // Configure the shared NimBLE scan object (active, low duty). NimBLE must be
  // initialized first. Does not start scanning.
  void begin();

  // Continuous low-duty scan. The result callback stamps old-lamp recency.
  void startScan();
  void stopScan();

  // True if an old lamp was seen within the last ms. False if never seen.
  bool sawOldLampWithin(uint32_t ms) const;

 private:
  volatile uint32_t lastOldLampSeenMs_ = 0;
  // Guards the "never seen" case so a fresh boot with millis() below the
  // recency window doesn't read the zero-init timestamp as a recent hit.
  volatile bool sawOldLamp_ = false;
};

}  // namespace wisp

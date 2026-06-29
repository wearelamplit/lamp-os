// WispZoneSelector — process-local zone-selection state for the wisp.
//
// Extracted from main.cpp because StatusBeacon needs to read
// currentZone / source / observedZones to emit wispStatus broadcasts.
// Only `currentZone` is persistent (and that lives in WispConfig); everything
// here is RAM.
//
// THREADING: observe() and copyObserved() are thread-safe via internal
// portMUX. Other methods (currentZone, source, etc.) are read-only after
// construction as far as the loop task is concerned — scalar reads that
// tolerate a torn snapshot because all mutators (latchFirstSeen / setFromOp /
// clearFromOp / setFromNvs) run on the loop task. The portMUX guards
// observedZones_ / observedCount_ because copyObserved() (timer task) must
// not see a half-shifted array during the FIFO memmove in observe().
//
// The `ZoneSource` discriminator tells the app pane where the current
// selection came from. The string form is camelCase to match the
// `char:"wispOp"` JSON naming convention on the wire.

#pragma once

#include <cstddef>
#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#define WISP_ZONE_PORTMUX_TYPE       portMUX_TYPE
#define WISP_ZONE_PORTMUX_INIT       portMUX_INITIALIZER_UNLOCKED
#define WISP_ZONE_PORTMUX_ENTER(mux) portENTER_CRITICAL(mux)
#define WISP_ZONE_PORTMUX_EXIT(mux)  portEXIT_CRITICAL(mux)
#else
struct WispZoneNullMux {};
#define WISP_ZONE_PORTMUX_TYPE       WispZoneNullMux
#define WISP_ZONE_PORTMUX_INIT       {}
#define WISP_ZONE_PORTMUX_ENTER(mux) ((void)(mux))
#define WISP_ZONE_PORTMUX_EXIT(mux)  ((void)(mux))
#endif

namespace wisp {

enum class ZoneSource : uint8_t { None, FirstSeen, Nvs, AppOp };

// camelCase wire form. The serial dump and JSON broadcast both use this so
// there's one mapping to chase.
const char* zoneSourceName(ZoneSource s);

// 16 matches Aurora's per-notification states cap; oldest-eviction FIFO
// keeps the set bounded without leaking memory if a noisy Aurora keeps
// rotating zone ids. The wispStatus JSON also caps at 16 entries, so this
// upper bound is the single source of truth for the budget.
constexpr size_t kMaxObservedZones = 16;

// Sane upper bound for zone ids sourced from the mesh, NVS, or app ops.
// 4 digits is generous for any real venue stage-index count and bounds
// the JSON digit width, restoring headroom above the greedy-cap guarantee.
constexpr int kMaxZoneId = 9999;
inline bool isValidZone(int z) { return z >= 0 && z <= kMaxZoneId; }

class ZoneSelector {
 public:
  int currentZone() const { return currentZone_; }
  ZoneSource source() const { return source_; }
  size_t observedCount() const { return observedCount_; }

  void observe(int zone);

  // Snapshot up to `outCap` observed zones into `out`. Returns the count
  // written. Thread-safe — takes the internal mux so this never reads a
  // half-shifted array mid-memmove inside observe().
  size_t copyObserved(int* out, size_t outCap) const;

  // Returns true if the first-seen latch actually changed state (caller can
  // log accordingly). No-op when a Nvs/AppOp selection is already in force.
  bool latchFirstSeen(int zone);

  void setFromOp(int zone);
  void clearFromOp();

  // Seed from NVS at boot. Used by main.cpp before the recv path is alive.
  void setFromNvs(int zone);

 private:
  int currentZone_ = -1;
  ZoneSource source_ = ZoneSource::None;
  int    observedZones_[kMaxObservedZones];
  size_t observedCount_ = 0;

  mutable WISP_ZONE_PORTMUX_TYPE observedMux_ = WISP_ZONE_PORTMUX_INIT;
};

}  // namespace wisp

// WispZoneSelector — process-local zone-selection state for the wisp.
//
// Extracted from main.cpp because StatusBeacon needs to read
// currentZone / source / observedZones to emit wispStatus broadcasts.
// Only `currentZone` is persistent (and that lives in WispConfig); everything
// here is RAM.
//
// THREADING: observe() and copyObserved() are thread-safe via internal
// portMUX; other methods (currentZone, source, etc.) are read-only after
// construction *as far as the loop task is concerned* — they are scalar
// reads that tolerate a torn snapshot under the assumption that mutators
// (latchFirstSeen / setFromOp / clearFromOp / setFromNvs) all run on the
// loop task too. StatusBeacon::emitStatus reads currentZone/source from
// the timer task; the worst case is one stale heartbeat, which the next
// triggerOnChange will correct. The observed-vector path is the only one
// that needs the mux because vector relocation can corrupt the snapshot.
//
// The `ZoneSource` discriminator tells the app pane where the current
// selection came from. The string form is camelCase to match the
// `char:"wispOp"` JSON naming convention on the wire.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

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

class ZoneSelector {
 public:
  int currentZone() const { return currentZone_; }
  ZoneSource source() const { return source_; }
  const std::vector<int>& observed() const { return observedZones_; }

  void observe(int zone);

  // Snapshot up to `outCap` observed zones into `out`. Returns the count
  // written. Thread-safe — takes the internal mux. Use this from any task
  // other than the loop task (e.g. StatusBeacon's timer-service heartbeat)
  // to avoid iterator invalidation if observe() reallocates concurrently.
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
  std::vector<int> observedZones_;  // FIFO, uniqued on insert

  // Guards observedZones_ so cross-task copyObserved() reads can't race a
  // loop-task observe() that erases/pushes/reallocates the vector.
  mutable WISP_ZONE_PORTMUX_TYPE observedMux_ = WISP_ZONE_PORTMUX_INIT;
};

}  // namespace wisp

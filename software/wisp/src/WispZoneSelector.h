// WispZoneSelector — process-local zone-selection state for the wisp.
// StatusBeacon reads currentZone/source/observedZones to emit wispStatus.
// Only currentZone is persistent (in WispConfig); the rest is RAM.
//
// Mutators (latchFirstSeen/setFromOp/clearFromOp/setFromNvs) run on the loop
// task. observe()/copyObserved() are mux-guarded because vector relocation
// can corrupt a cross-task snapshot; the scalar reads (currentZone/source)
// tolerate a torn read from the timer task, worst case one stale heartbeat
// that the next triggerOnChange corrects.
//
// ZoneSource tells the app pane where the selection came from; the string
// form is camelCase to match the char:"wispOp" JSON convention.

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

// 16 matches Aurora's per-notification states cap; FIFO eviction bounds the
// set if a noisy Aurora rotates zone ids. Single source of truth for the
// wispStatus observedZones budget.
constexpr size_t kMaxObservedZones = 16;

class ZoneSelector {
 public:
  int currentZone() const { return currentZone_; }
  ZoneSource source() const { return source_; }
  const std::vector<int>& observed() const { return observedZones_; }

  void observe(int zone);

  // Snapshot up to outCap observed zones into out; returns count written.
  // Takes the mux; use from any non-loop task to avoid iterator invalidation
  // if observe() reallocates.
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

// WispZoneSelector — in-RAM zone-selection state (persistence lives in WispConfig).
//
// observe() and copyObserved() are guarded by portMUX: copyObserved() runs on
// the timer task and must not read a half-shifted array during observe()'s
// FIFO memmove. All mutators run on the loop task; scalar reads tolerate torn
// snapshots from that task.

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

const char* zoneSourceName(ZoneSource s);

// Matches Aurora's per-notification cap; also the wispStatus JSON zone-array cap.
constexpr size_t kMaxObservedZones = 16;

// 4-digit bound caps JSON digit width and keeps frame size predictable.
constexpr int kMaxZoneId = 9999;
inline bool isValidZone(int z) { return z >= 0 && z <= kMaxZoneId; }

class ZoneSelector {
 public:
  int currentZone() const { return currentZone_; }
  ZoneSource source() const { return source_; }
  size_t observedCount() const { return observedCount_; }

  void observe(int zone);

  size_t copyObserved(int* out, size_t outCap) const;

  // Returns true if state changed. No-op when Nvs/AppOp selection is in force.
  bool latchFirstSeen(int zone);

  void setFromOp(int zone);
  void clearFromOp();
  void setFromNvs(int zone);

 private:
  int currentZone_ = -1;
  ZoneSource source_ = ZoneSource::None;
  int    observedZones_[kMaxObservedZones];
  size_t observedCount_ = 0;

  mutable WISP_ZONE_PORTMUX_TYPE observedMux_ = WISP_ZONE_PORTMUX_INIT;
};

}  // namespace wisp

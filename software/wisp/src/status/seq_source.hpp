// One monotonic seq for every wisp frame across both beacons. Receivers dedup
// on (srcMac, msgType, seq), so PresenceBeacon and StatusEmitter must draw from
// this single counter or their frames collide in the dedup ring.

#pragma once

#include <cstdint>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#define WISP_SEQ_PORTMUX_TYPE       portMUX_TYPE
#define WISP_SEQ_PORTMUX_INIT       portMUX_INITIALIZER_UNLOCKED
#define WISP_SEQ_PORTMUX_ENTER(mux) portENTER_CRITICAL(mux)
#define WISP_SEQ_PORTMUX_EXIT(mux)  portEXIT_CRITICAL(mux)
#else
struct WispSeqNullMux {};
#define WISP_SEQ_PORTMUX_TYPE       WispSeqNullMux
#define WISP_SEQ_PORTMUX_INIT       {}
#define WISP_SEQ_PORTMUX_ENTER(mux) ((void)(mux))
#define WISP_SEQ_PORTMUX_EXIT(mux)  ((void)(mux))
#endif

namespace wisp {

// Bump and frame build stay under the same mux so the two beacon tasks never share a seq.
struct SeqSource {
  WISP_SEQ_PORTMUX_TYPE mux = WISP_SEQ_PORTMUX_INIT;
  uint16_t counter = 0;

  // Caller holds mux.
  uint16_t next() { return counter++; }
};

}  // namespace wisp

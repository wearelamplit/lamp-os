#pragma once

#include <cstdint>

// HELLO broadcast interval. Airtime scales N²/interval as the fleet grows,
// so this is the steady-state mesh-traffic knob. Must stay well under
// LAMP_PRUNE_TIME_MS so a couple of lost beacons don't prune a live peer
// (prune tolerates 4 missed emits at 60s).
#define LAMP_HELLO_INTERVAL_MS 60000

static_assert(LAMP_HELLO_INTERVAL_MS == 60000,
              "LAMP_HELLO_INTERVAL_MS is the fleet airtime budget (N²/interval) "
              "coupled to LAMP_PRUNE_TIME_MS; changing it must re-validate both "
              "the airtime budget and the prune window.");

// Boot burst: the first LAMP_HELLO_BURST_WINDOW_MS of uptime emit HELLO every
// LAMP_HELLO_BURST_INTERVAL_MS so a missed first HELLO refills the roster in
// seconds; airtime is elevated only in this window, then settles to the steady
// 60 s budget. Bench-tunable.
#define LAMP_HELLO_BURST_WINDOW_MS 30000
#define LAMP_HELLO_BURST_INTERVAL_MS 5000

namespace lamp {

// millis() is uptime-since-boot, so uptimeMs < window naturally IS the boot
// window.
inline uint32_t helloIntervalMs(uint32_t uptimeMs) {
  return uptimeMs < LAMP_HELLO_BURST_WINDOW_MS ? LAMP_HELLO_BURST_INTERVAL_MS
                                               : LAMP_HELLO_INTERVAL_MS;
}

}  // namespace lamp

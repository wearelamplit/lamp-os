#pragma once

// Wisp paint timing shared by lamp (ColorOverride watchdog) and wisp
// (per-lamp keep-alive scheduler). One source of truth so the two sides'
// timeouts can't drift apart across a build.

#include <cstdint>

namespace lamp_protocol {

// Lamp colour-override restore failsafe (ColorOverride::tick). Reverts a
// surface if no paint lands within this window of the wisp going silent.
constexpr uint32_t kPaintWatchdogMs = 100000;

// Wisp per-lamp re-paint deadline: how long the wisp lets a paint target go
// un-refreshed before it re-sends it on the reliable unicast channel.
constexpr uint32_t kPaintKeepaliveMs = 30000;

// Floor on a keep-alive/recovery re-paint's fade so it never snaps.
constexpr uint32_t kMinReaffirmFadeMs = 1500;

// One lost keep-alive paint must not trip the lamp's watchdog.
static_assert(kPaintWatchdogMs >= 2 * kPaintKeepaliveMs,
              "kPaintWatchdogMs must tolerate one lost keep-alive paint");

}  // namespace lamp_protocol

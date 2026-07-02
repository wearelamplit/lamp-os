#pragma once

#include <cstdint>

#include "components/network/protocol/lamp_protocol.hpp"
#include "util/color.hpp"

namespace lamp {

// App-level Core0→Core1 hand-off DTOs, split out of mesh_link.hpp so
// consumers that only need the payload shapes (e.g. the pending-slot
// aggregate) don't have to pull in the whole ESP-NOW transport.
//
// POD-by-construction so PendingTypedSlot<T>'s portMUX-protected memcpy
// post/drain has well-defined semantics across the WiFi-task -> loop-task
// hand-off. MeshLink::handleRecv populates these on the WiFi task
// (Core 0); standard_lamp's loop drain reads them on Core 1 and dispatches
// into the ColorOverride / BrightnessOverride / NearbyLamps modules.
//
// Colors use the Color struct directly (4 bytes/pixel, RGBW) since the loop
// drain hands them to ColorOverride::apply which expects `const Color*`.

struct PendingOverrideColors {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint32_t fadeDurationMs;
  uint8_t numColors;
  Color colors[lamp_protocol::kMaxOverrideColorsPerFrame];
};

struct PendingRestoreColors {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint32_t fadeDurationMs;
};

struct PendingOverrideBrightness {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint16_t fadeDurationMs;
  uint8_t brightness;
};

struct PendingRestoreBrightness {
  uint8_t sourceMac[6];
  lamp_protocol::OverrideSurface surface;
  lamp_protocol::OverrideSource sourceKind;
  uint16_t fadeDurationMs;
};

struct PendingWispHello {
  uint8_t sourceMac[6];
  uint32_t wispVersion;
  uint8_t flags;
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN];
  char carriedFwChannel[lamp_protocol::WISP_HELLO_FW_CHANNEL_LEN];
  uint32_t carriedFwVersion;
};

// MSG_WISP_PALETTE pending slot. Holds the wisp's broadcast manualPalette
// until the Core 1 drain forwards it into NearbyLamps::cacheWispPalette.
// Sized to kMaxWispPaletteColors * 3 = 150 bytes (matches the on-wire cap).
struct PendingWispPalette {
  uint8_t sourceMac[6];
  uint8_t count;  // 0..kMaxWispPaletteColors
  uint8_t rgb[lamp_protocol::kMaxWispPaletteColors *
              lamp_protocol::WISP_PALETTE_ENTRY_SIZE];
};

// MSG_WISP_CLAIM pending slot. Holds the wisp's claimed-lamp roster until
// the Core 1 drain forwards it into NearbyLamps::cacheWispClaim.
struct PendingWispClaim {
  uint8_t sourceMac[6];
  uint8_t count;
  uint8_t lampMacs[lamp_protocol::kMaxWispClaimEntries][6];
};

// MSG_EVENT pending slot. MeshLink's WiFi-task recv path does the
// stagger-list lookup (own MAC -> delayMs) and memcpys the result here;
// the Core 1 drain calls ExpressionManager::tryHandleExpressionEvent
// (JSON parse + cascade-config check + dedup + trigger). Buffer sized to
// maxEventPayloadFor(0) = 234, the best-case payload with no stagger
// entries. Lower stagger counts get larger payloads, and the slot must
// hold whatever the parser accepted or frames get silently dropped at the
// recv-side memcpy boundary.
struct PendingEvent {
  uint8_t  sourceMac[6];
  uint16_t delayMs;          // already resolved by recv-side stagger lookup
  uint16_t payloadLen;
  uint8_t  payload[lamp_protocol::maxEventPayloadFor(0)];
};

}  // namespace lamp

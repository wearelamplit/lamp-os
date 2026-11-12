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
// into the ColorOverride / BrightnessOverride / LampRoster modules.
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
// until the Core 1 drain forwards it into LampRoster::cacheWispPalette.
// Interleaved R,G,B,W per color (W = 0 for frames without a W plane);
// kMaxWispPaletteColors * 4 = 200 bytes.
struct PendingWispPalette {
  uint8_t sourceMac[6];
  uint8_t count;  // 0..kMaxWispPaletteColors
  uint8_t rgbw[lamp_protocol::kMaxWispPaletteColors * 4];
};

// MSG_WISP_CLAIM pending slot. Holds the wisp's claimed-lamp roster until
// the Core 1 drain forwards it into LampRoster::cacheWispClaim.
struct PendingWispClaim {
  uint8_t sourceMac[6];
  uint8_t count;
  uint8_t lampMacs[lamp_protocol::kMaxWispClaimEntries][6];
};

// MSG_WISP_STATE pending slot. Holds the declarative per-lamp state entries
// until the Core 1 drain scans for this lamp's own MAC and feeds the wisp
// LayerStack. sourceMac gates the drain to this lamp's display wisp so a
// rival's frame can't ease it home. Brightness / drift-rate globals are not
// carried; the render path consumes base/shade only. `w` holds baseW,shadeW
// per entry in entry order, zero for frames without a W plane. `fade` is this
// lamp's own drift fade lookup, its record in `fadeRecord`, and `ownIndex` its
// entry (kNoOwnEntry when absent). Every other entry with a drift fade record
// holds that record's target in place of the frame's colour, its bit set in
// `targeted`; this lamp's own entry keeps the frame's colour for the render.
// `fadePartial` says the frame carries records for only some fading entries.
struct PendingWispState {
  static constexpr uint8_t kNoOwnEntry = 0xFF;
  static_assert(lamp_protocol::WISP_STATE_MAX_ENTRIES < kNoOwnEntry,
                "an entry index must not read as kNoOwnEntry");
  uint8_t sourceMac[6];
  uint8_t count;
  uint8_t entries[lamp_protocol::WISP_STATE_MAX_ENTRIES *
                  lamp_protocol::WISP_STATE_ENTRY_SIZE];
  uint8_t w[lamp_protocol::WISP_STATE_MAX_ENTRIES *
            lamp_protocol::WISP_STATE_W_SIZE];
  lamp_protocol::WispDriftFadeLookup fade;
  lamp_protocol::WispDriftFade fadeRecord;
  uint8_t ownIndex;
  bool fadePartial;
  uint8_t targeted[(lamp_protocol::WISP_STATE_MAX_ENTRIES + 7) / 8];

  // Whether entry `i` reads as its drift fade's target: a record's target
  // copied over it, or this lamp's own record.
  bool entryTargeted(uint8_t i) const {
    if (i == ownIndex) return fade == lamp_protocol::WispDriftFadeLookup::Record;
    return (targeted[i / 8] >> (i % 8)) & 1u;
  }
};

// This lamp's drift fade in a STATE frame. `record` holds what the trailer
// lookup wrote, and `recordWritten` says whether it wrote it. `index` is this
// lamp's entry, kNoOwnEntry when absent, and `trailer` the trailer's form.
struct OwnWispDriftFade {
  lamp_protocol::WispDriftFadeLookup fade = lamp_protocol::WispDriftFadeLookup::None;
  bool recordWritten = false;
  lamp_protocol::WispDriftFade record{};
  uint8_t index = PendingWispState::kNoOwnEntry;
  lamp_protocol::WispDriftFadeTrailer trailer =
      lamp_protocol::WispDriftFadeTrailer::Complete;
};

// Look up `selfMac`'s drift fade in a parsed MSG_WISP_STATE (None for a null
// MAC). Reads the frame, so call it while the recv buffer is valid.
OwnWispDriftFade decodeOwnWispDriftFade(const lamp_protocol::ParsedWispState& ws,
                                        const uint8_t selfMac[6]);

// Copy a parsed MSG_WISP_STATE into the slot, W plane, drift fade targets and
// `own` included. Copies only, bounded by the frame, so it may run under
// portMUX. `own` is decodeOwnWispDriftFade's result for the same frame.
void fillPendingWispState(PendingWispState& slot,
                          const lamp_protocol::ParsedWispState& ws,
                          const OwnWispDriftFade& own);

// Copy a parsed MSG_WISP_CLAIM's lamp MACs into the slot, at most
// kMaxWispClaimEntries of them.
void fillPendingWispClaim(PendingWispClaim& slot,
                          const lamp_protocol::ParsedWispClaim& wc);

// This lamp's RGBW base/shade from the slot. False when `mac` has no entry.
bool wispStateColorsFor(const PendingWispState& slot, const uint8_t mac[6],
                        Color& base, Color& shade);

// MSG_COMMAND pending slot. Carries an ExpressionInvocation JSON payload
// from the WiFi recv task to the Core 1 drain. sourceMac is used for cascade
// coalescing.
struct PendingCommand {
  uint8_t  sourceMac[6];
  uint16_t payloadLen;
  uint8_t  payload[lamp_protocol::COMMAND_MAX_PAYLOAD];
};

// Copy a parsed MSG_COMMAND's source and payload into the slot. parseCommand
// bounds payloadLen to COMMAND_MAX_PAYLOAD.
void fillPendingCommand(PendingCommand& slot,
                        const lamp_protocol::ParsedCommand& p);

struct PendingColorQuery {
  uint8_t sourceMac[6];
};

struct PendingColorInfo {
  uint8_t sourceMac[6];
  uint8_t baseCount;
  uint8_t baseStops[lamp_protocol::COLOR_INFO_MAX_STOPS * 4];
  uint8_t shadeCount;
  uint8_t shadeStops[lamp_protocol::COLOR_INFO_MAX_STOPS * 4];
};

// MSG_EVENT pending slot. Carries an expression-fired announce payload from
// the WiFi recv task to the Core 1 drain.
struct PendingEvent {
  uint8_t  sourceMac[6];
  uint16_t payloadLen;
  uint8_t  payload[lamp_protocol::EVENT_MAX_PAYLOAD];
};

}  // namespace lamp

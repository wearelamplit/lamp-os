#pragma once

// Wire format for lamp <-> lamp ESP-NOW broadcast (HELLO, CONTROL_OP,
// wisp/transient-override/event message families).

#include <cstdint>
#include <cstring>

// portMUX is FreeRTOS-only. The header is also indirectly mirrored in
// native unit tests — guard the include so a hypothetical native compile
// of THIS header doesn't break, and provide a no-op fallback.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#define LAMP_PROTOCOL_PORTMUX_TYPE        portMUX_TYPE
#define LAMP_PROTOCOL_PORTMUX_INIT        portMUX_INITIALIZER_UNLOCKED
#define LAMP_PROTOCOL_PORTMUX_ENTER(mux)  portENTER_CRITICAL(mux)
#define LAMP_PROTOCOL_PORTMUX_EXIT(mux)   portEXIT_CRITICAL(mux)
#else
struct LampProtocolNullMux {};
#define LAMP_PROTOCOL_PORTMUX_TYPE        LampProtocolNullMux
#define LAMP_PROTOCOL_PORTMUX_INIT        {}
#define LAMP_PROTOCOL_PORTMUX_ENTER(mux)  ((void)(mux))
#define LAMP_PROTOCOL_PORTMUX_EXIT(mux)   ((void)(mux))
#endif

namespace lamp_protocol {

constexpr uint8_t MAGIC_0 = 'L';
constexpr uint8_t MAGIC_1 = 'M';

// =============================================================================
// Protocol versioning
// =============================================================================
//
// The version byte at `data[2]` of every frame discriminates wire
// formats. inspect() gates incoming frames against this byte before any
// field read so an older parser running against a newer-format frame
// can't underflow.
//
// Two constants intentionally — not one. Splitting "what we emit" from
// "what we accept on receive" is what makes cross-version rollouts
// possible at all. With a single PROTOCOL_VERSION constant a fleet
// migration becomes impossible: old lamps drop the new version, new
// lamps drop the old version, and there's no path from any one to any
// other except a manual USB re-flash of every device.
//
//   PROTOCOL_VERSION_EMIT   — what we put on the wire (broadcast HELLO,
//                             default outbound frames). Held at the
//                             OLDEST supported version so older fleet
//                             members can still see us. New features
//                             landed via TLV trailers ride along
//                             regardless.
//
//   PROTOCOL_VERSION_RX_MAX — newest version we know how to parse.
//                             inspect() accepts data[2] in
//                             [EMIT, RX_MAX] inclusive. We can grow our
//                             receive range ahead of our emit range so
//                             new-version peers see us responsive
//                             before we start emitting the new version
//                             ourselves.
//
// -----------------------------------------------------------------------------
// Upgrade workflow
// -----------------------------------------------------------------------------
//
// Bumps come in two shapes — TLV-additive (most of them, by design) and
// genuine layout-breaking. Treat them differently:
//
// (a) TLV-additive bump — adding a new field that fits inside an
//     existing HELLO/WISP_HELLO TLV trailer:
//       - Bump PROTOCOL_VERSION_RX_MAX to the new value.
//       - That's it. Old parsers ignore unknown TLV types by length;
//         new parsers handle the new type. No new code paths needed.
//
// (b) Layout-breaking bump — a fixed-offset field changes width,
//     position, or meaning in any of these messages: MSG_FW_OFFER,
//     MSG_FW_CHUNK, MSG_FW_DONE, MSG_FW_ACCEPT, MSG_FW_REQ,
//     MSG_FW_RESULT, MSG_HELLO (fixed part), MSG_WISP_HELLO (fixed
//     part). Steps:
//       1. Bump PROTOCOL_VERSION_RX_MAX. Don't yet touch EMIT.
//       2. Add a `legacy_v0xNN::` namespace next to lamp_protocol's
//          top-level builders/parsers (NN = the OLD version we're
//          leaving behind). Move the existing implementation of the
//          7 OTA-related functions into it (see below). The new
//          version's implementation becomes the top-level one.
//       3. Modify the top-level builders for the 7 OTA messages to
//          take an explicit `version` argument; default it to
//          PROTOCOL_VERSION_EMIT. The distributor passes the target
//          peer's version when known (learned from their HELLO).
//       4. Ship the transitional release. Fleet receives + propagates
//          OTAs. Old peers stay reachable because OFFER/CHUNK/DONE
//          are routed through `legacy_v0xNN::` for them.
//       5. After the fleet is fully migrated, the NEXT release bumps
//          PROTOCOL_VERSION_EMIT and deletes the `legacy_v0xNN::`
//          namespace.
//
// -----------------------------------------------------------------------------
// Legacy OTA support — required surface
// -----------------------------------------------------------------------------
//
// A `legacy_v0xNN::` namespace only needs the messages required to OTA
// an older-version peer. Everything else can be skipped — once the OTA
// completes the peer reboots into the new firmware and speaks the new
// protocol natively. The required surface, in both directions:
//
//   parse legacy_v0xNN::parseHello       (incoming, learn peer + version)
//   parse legacy_v0xNN::parseFwAccept    (incoming, OTA handshake)
//   parse legacy_v0xNN::parseFwReq       (incoming, chunk retransmit)
//   parse legacy_v0xNN::parseFwResult    (incoming, OTA outcome)
//   build legacy_v0xNN::buildFwOffer     (outgoing, initiate OTA)
//   build legacy_v0xNN::buildFwChunk     (outgoing, send bytes)
//   build legacy_v0xNN::buildFwDone      (outgoing, signal end of stream)
//
// What the legacy namespace does NOT need:
//   - Override colors / paint distribution / cascade gossip relays
//   - Wisp claims / wisp paint / wisp HELLO
//   - Event messages / dedup ring entries
//   - Any non-OTA message type
//
// All of those just don't get sent to the legacy peer in their old
// format. The old peer never sees them. After the OTA they land at the
// new version and the new code paths handle everything natively.
//
// -----------------------------------------------------------------------------
// Current state
// -----------------------------------------------------------------------------
//
// EMIT=0x04, RX_MAX=0x05. v0x04 is the broadly-deployed fleet version;
// v0x05 added a TLV trailer to HELLO/WISP_HELLO. Because that addition
// is forward-compatible (old parsers stop at the name field), v0x04
// and v0x05 wire formats are byte-identical for every other message
// type. No `legacy_v0x04::` namespace exists yet — we can emit at
// 0x04 and v0x05 peers' inspect would accept it iff their inspect range
// extends down to 0x04 (which this build does; pre-existing v0x05-only
// lamps may not).

// What we emit by default on broadcast frames (HELLO etc).
constexpr uint8_t PROTOCOL_VERSION_EMIT   = 0x05;

// inspect() accepts frames in [RX_MIN, RX_MAX]. RX_MIN is the oldest
// version we still know how to parse on receive. Set BELOW EMIT to
// keep seeing old-fleet HELLOs — that lets us populate
// NearbyLamp.protocolVersion for those peers, which then drives
// per-peer OTA-OFFER routing at THEIR version. When a future release
// drops legacy receive support, raise RX_MIN to current EMIT.
constexpr uint8_t PROTOCOL_VERSION_RX_MIN = 0x04;
constexpr uint8_t PROTOCOL_VERSION_RX_MAX = 0x05;

// Back-compat alias for code paths that still reference a single
// "PROTOCOL_VERSION". The intent is for callers to migrate to EMIT
// (when building) or check the range (when validating); this alias
// keeps the obvious thing working during the migration.
constexpr uint8_t PROTOCOL_VERSION = PROTOCOL_VERSION_EMIT;

enum MsgType : uint8_t {
  MSG_HELLO               = 0x01,
  // Forwarded BLE control write. Payload is JSON tagged with a `char` field
  // naming the local control surface to invoke (brightness, shadeColors,
  // baseColors, knockout, expressionOp, settings, ...). The local
  // pending-slot post functions handle the routing; downstream drain in
  // loop() runs unchanged.
  MSG_CONTROL_OP          = 0x03,
  // --- Wisp + transient overrides + events ---
  MSG_WISP_HELLO          = 0x20,  // wisp presence beacon
  MSG_OVERRIDE_COLORS     = 0x21,
  MSG_RESTORE_COLORS      = 0x22,
  MSG_OVERRIDE_BRIGHTNESS = 0x23,
  MSG_RESTORE_BRIGHTNESS  = 0x24,
  // Wisp-to-wisp claim broadcast. Carries `[(lampMac, rssi)]` entries
  // for every lamp the sending wisp currently claims, at the RSSI the
  // wisp hears that lamp. Gossip-relayed by lamps the same way
  // MSG_WISP_HELLO is, so the shared claim view propagates across the
  // mesh regardless of whether two wisps can directly hear each other.
  // Lamps don't otherwise act on this message — it's purely wisp-
  // coordination.
  MSG_WISP_CLAIM          = 0x25,
  // Wisp's manualPalette broadcast. Carries up to kMaxWispPaletteColors
  // RGB triples packed binary. Lamps cache the latest, gossip-relay once
  // per (mac, seq), and serve it back to apps inside the wispStatus BLE
  // characteristic JSON as a base64 blob. Replaces the previous design
  // where the app held a per-lampId SharedPreferences copy of the palette
  // — that caused per-lamp drift when the same operator edited via one
  // lamp and viewed via another. Cadence: piggybacked on the 30 s
  // emitStatus() tick, plus an on-change emit from WispOpDispatcher.
  MSG_WISP_PALETTE        = 0x26,
  MSG_EVENT               = 0x30,
};

// Explicit reserve of the high bit on msgType. Previously FLAG_LOCAL_ONLY
// rode there for the cascade-locality hack; that path is retired
// (cascade now uses MSG_EVENT broadcast). The bit is reserved for
// future protocol changes; inspect() no longer masks it so any future
// reuse will surface immediately as an unrecognised msgType byte.
constexpr uint8_t kReservedMsgTypeHighBit = 0x80;

// v0x03 mesh-deploy lock-in: parallel reservation of the high bit on the
// numStaggerEntries field (data[13] of MSG_EVENT). Plausible future use:
// a "scope flag" on the stagger semantics (e.g., room-scope vs. fleet-
// scope cascades, or "broadcast-everyone-fires-tail-jittered" vs the
// current "fires per its delayMs"). parseEvent rejects any frame that
// sets this bit so a future receiver gains an unambiguous escape hatch:
// v0x03 peers loudly drop the frame, not silently reinterpret it.
// why: forward-compat reservation per validated plan §"Layer 3".
constexpr uint8_t kStaggerCountReservedHighBit = 0x80;

// Single-source-of-truth caps.
constexpr size_t kMaxOverrideColorsPerFrame = 8;   // ESP-NOW 250-byte cap math
constexpr size_t kMaxStaggerEntries         = 12;  // ESP-NOW 250-byte cap math
constexpr uint8_t kBrightnessOverrideMin    = 5;   // anti-defeat floor

// Surface byte values used by the override/restore family. `BaseAndShade`
// means numColors=2 carries a pair: colors[0] for base, colors[1] for
// shade — one frame, two surfaces, distinct colors. The wisp's paint
// distributor uses BaseAndShade to halve ESP-NOW frame count per peer
// per cycle (was Base+Shade as two separate frames).
enum class OverrideSurface : uint8_t {
  Base         = 0x01,
  Shade        = 0x02,
  BaseAndShade = 0x03,
};

// Discriminator for who originated an override. 0x10..0xFE is user-
// defined for forward compat; Any (0xFF) is an internal sentinel used
// by the watchdog and force-restore paths to bypass source-discriminated
// drop logic — never appears on the wire.
enum class OverrideSource : uint8_t {
  None     = 0x00,
  Wisp     = 0x01,
  Any      = 0xFF,
};

// MSG_EVENT discriminator. 0x02..0x0F are reserved for built-ins so the
// firmware can add new well-known events without colliding with
// user-defined ones in the 0x10..0xFF range.
enum class EventKind : uint8_t {
  ExpressionTriggered = 0x01,
};

constexpr size_t HEADER_SIZE = 6;
// HELLO fixed prefix: header(6) + sourceMac(6) + shade(4) + base(4) + firmwareVersion(4).
// Name length byte + name bytes follow this prefix.
// 6 (header: magic+ver+type+seq) + 6 (sourceMac) + 4 (shade RGBW) +
// 4 (base RGBW) + 4 (firmwareVersion LE) = 24 bytes. Previously declared
// as 23 — an off-by-one that made buildHello report 1 fewer byte than
// it actually wrote (the firmwareVersion's MSB landed at offset 23 but
// HELLO_FIXED_SIZE said the prefix ended at offset 22). Effect on the
// wire: the last byte of the name field was truncated and parseHello
// read stack garbage into its last slot ("jacko" → "jackx" in the
// roster). Both lamp and wisp must run the same value here — protocol
// is verbatim-mirrored.
constexpr size_t HELLO_FIXED_SIZE = 24;
constexpr size_t HELLO_MAX_NAME = 32;
// HELLO TLV trailer (v0x05+). After the variable name field, the frame
// carries:
//   [tlv_count 1 byte] [type 1][len 1][value len bytes] x tlv_count
// Each TLV's len byte limits its value to 255 bytes. Parsers skip
// unknown types by advancing 2 + len, so future TLV types can land
// without bumping PROTOCOL_VERSION.
//
// HELLO_MAX_SIZE jumped 57 → 128 to give the trailer room for ~7 TLVs
// of average size 10 bytes (1 byte type + 1 byte len + ~8 byte value),
// plus the existing fixed + name fields. ESP-NOW MTU is 250, so we
// still have ~120 bytes of headroom over the new ceiling.
constexpr uint8_t HELLO_TLV_OTA_STATE = 0x01;  // value: 1 byte, 0=idle 1=sending 2=receiving
// value: 16 bytes, the lamp's `{type}-{channel}` identity (zero-padded),
// same string as the LSIG footer + MSG_FW_OFFER channel field. Lets the
// distributor skip OFFERs at a peer of a different lamp-type or channel
// instead of relying solely on the receiver's silent-drop.
constexpr uint8_t HELLO_TLV_FW_CHANNEL = 0x02;
constexpr size_t  HELLO_FW_CHANNEL_LEN = 16;  // == FW_CHANNEL_LEN

// value: 8 bytes, a prefix of the lamp's FS-image manifest digest (the
// fs_signature.cpp logical-content digest, NOT a raw-partition SHA). Lets the
// FS distributor decide whether a same-firmware-version peer has a stale UI
// image (peerFsDigest != myFsDigest) without offering blindly. Absent on
// lamps built with LAMP_FS_OTA_ENABLED=0 → distributor treats absent as "don't
// offer FS" (an FS-disabled peer can't receive it anyway).
constexpr uint8_t HELLO_TLV_FS_STATE  = 0x03;
constexpr size_t  HELLO_FS_DIGEST_LEN = 8;  // == FW_SHA256_PREFIX_LEN

// Compact OTA-state enum carried in HELLO_TLV_OTA_STATE. Maps to:
//   firmwareDistributor.isInProgress() → kOtaStateSending
//   firmwareReceiver.isInProgress()    → kOtaStateReceiving
//   neither                            → kOtaStateIdle
// Receiver wins when both somehow report true (matches the local
// otaPulse precedence used by SocialBehavior pre-pulse-removal).
constexpr uint8_t kOtaStateIdle      = 0;
constexpr uint8_t kOtaStateSending   = 1;
constexpr uint8_t kOtaStateReceiving = 2;

constexpr size_t HELLO_MAX_SIZE = 128;  // see TLV trailer note above

// MSG_CONTROL_OP frame: header(6) + targetMac(6) + sourceMac(6) + payloadLen(2) + payload(N).
// ESP-NOW max frame is 250 bytes; subtract the 20-byte fixed prefix.
constexpr size_t CONTROL_FIXED       = HEADER_SIZE + 6 + 6 + 2;
constexpr size_t CONTROL_MAX_PAYLOAD = 250 - CONTROL_FIXED;  // 230
constexpr size_t CONTROL_MAX_SIZE    = CONTROL_FIXED + CONTROL_MAX_PAYLOAD;

// --- Wisp / override / event wire-format sizes ---
// MSG_WISP_HELLO: header(6) + sourceMac(6) + wispVersion(4) + flags(1)
//                 + paletteIdPrefix(8) + carriedFwChannel(16) + carriedFwVersion(4)
//               = 45 bytes fixed.
constexpr size_t WISP_HELLO_PALETTE_ID_PREFIX_LEN = 8;
constexpr size_t WISP_HELLO_FW_CHANNEL_LEN        = 16;
constexpr size_t WISP_HELLO_FIXED_SIZE            = HEADER_SIZE + 6 + 4 + 1 +
                                                    WISP_HELLO_PALETTE_ID_PREFIX_LEN +
                                                    WISP_HELLO_FW_CHANNEL_LEN + 4;  // 45
constexpr uint8_t WISP_HELLO_FLAG_PAINT_MODE        = 0x01;
constexpr uint8_t WISP_HELLO_FLAG_WIFI_CONNECTED    = 0x02;
constexpr uint8_t WISP_HELLO_FLAG_AURORA_CONNECTED  = 0x04;
// WISP_HELLO TLV trailer (v0x05+). Same shape as HELLO's:
//   [tlv_count 1] [type 1][len 1][value len] x tlv_count
// Bumped 45 → 96 to leave room for ~6-7 future TLVs without ever
// needing another PROTOCOL_VERSION bump. The wisp doesn't currently
// emit any TLVs (it always sends tlv_count=0), but the parser tolerates
// + skips unknowns identically to HELLO, so future additions land
// cleanly on both sides.
constexpr size_t WISP_HELLO_MAX_SIZE              = 96;
// TLV-type registry for WISP_HELLO. Currently empty; reserved 0x01-0x1F
// for future use. Shares its 1-byte type-space namespace with HELLO,
// which is why HELLO_TLV_OTA_STATE (also 0x01) is fine here — these
// types appear in separate frames and HELLO_TLV_OTA_STATE doesn't
// make sense for a wisp.

// MSG_WISP_CLAIM: header(6) + sourceMac(6) + count(1) + entries[count*7].
// Each entry: lampMac(6) + signed int8 rssi(1) = 7 bytes.
// ESP-NOW frame cap 250 bytes; (250 - 13) / 7 = 33 entries max. We cap
// at 32 to align with the wisp's LampInventory::MAX_LAMPS — a wisp can
// never have more entries to advertise than its inventory holds anyway.
constexpr size_t WISP_CLAIM_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_CLAIM_ENTRY_SIZE   = 6 + 1;                // 7
constexpr size_t kMaxWispClaimEntries    = 32;
// MSG_WISP_PALETTE: header(6) + sourceMac(6) + count(1) + rgb[count*3].
// Each entry is 3 bytes (R, G, B) — no W channel; the wisp's manualPalette
// is RGB-only. Cap kMaxWispPaletteColors at 50 to keep the frame well under
// the 250-byte ESP-NOW limit: 13 + 50*3 = 163 bytes. Aurora palettes can be
// larger than 50; the wisp truncates when emitting and logs once on
// truncation so an operator notices the shape mismatch.
constexpr size_t WISP_PALETTE_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PALETTE_ENTRY_SIZE   = 3;                    // R, G, B
constexpr size_t kMaxWispPaletteColors     = 50;
constexpr size_t WISP_PALETTE_MAX_SIZE     = WISP_PALETTE_FIXED_PREFIX +
                                              kMaxWispPaletteColors *
                                              WISP_PALETTE_ENTRY_SIZE;  // 163
constexpr size_t WISP_CLAIM_MAX_SIZE     = WISP_CLAIM_FIXED_PREFIX +
                                            kMaxWispClaimEntries *
                                            WISP_CLAIM_ENTRY_SIZE;  // 237

// MSG_OVERRIDE_COLORS fixed prefix:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2) + numColors(1)
// = 23 bytes; colors[numColors * 4] follow. Min numColors=1 → 27 total.
constexpr size_t OVERRIDE_COLORS_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2 + 1;  // 23
constexpr size_t OVERRIDE_COLORS_MAX_SIZE   = OVERRIDE_COLORS_FIXED_SIZE +
                                              kMaxOverrideColorsPerFrame * 4;       // 55

// MSG_RESTORE_COLORS / MSG_RESTORE_BRIGHTNESS:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2)
// = 22 bytes fixed (no payload tail).
constexpr size_t RESTORE_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2;  // 22

// MSG_OVERRIDE_BRIGHTNESS:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(2) + brightness(1)
// = 23 bytes fixed.
constexpr size_t OVERRIDE_BRIGHTNESS_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 2 + 1;  // 23

// MSG_EVENT fixed prefix when numStaggerEntries=0 and payloadLen=0:
//   header(6) + sourceMac(6) + eventKind(1) + numStaggerEntries(1)
//   + payloadLen(2)
// = 16 bytes minimum. (Spec'd as 17 in the plan; the math here is the
// authoritative one — header is 6, not 7.)
constexpr size_t EVENT_FIXED_SIZE      = HEADER_SIZE + 6 + 1 + 1 + 2;  // 16
constexpr size_t EVENT_STAGGER_ENTRY   = 6 + 2;                        // mac + delayMs
// Worst-case JSON tail when the wire frame carries a fully-populated
// stagger list (12 peers): 250 - 16 - 12*8 = 138. Kept as a hard upper
// bound the rest of the codebase can rely on for buffer sizing.
// Callers building or validating an event with a known stagger count
// should use maxEventPayloadFor(n) instead — small meshes get a larger
// payload window because fewer bytes are reserved for stagger entries.
constexpr size_t EVENT_MAX_PAYLOAD     = 250 - EVENT_FIXED_SIZE -
                                         (kMaxStaggerEntries * EVENT_STAGGER_ENTRY);
constexpr size_t EVENT_MAX_SIZE        = EVENT_FIXED_SIZE +
                                         kMaxStaggerEntries * EVENT_STAGGER_ENTRY +
                                         EVENT_MAX_PAYLOAD;  // 250

// Dynamic payload budget. The stagger list only occupies n * 8 bytes on
// the wire, so on small meshes (typical home setup: 1–4 lamps) the
// payload window is much larger than the worst-case 138 B. Clamps at the
// 12-peer ceiling so a stale numStagger can't grant a larger-than-frame
// budget. Before this helper, ExpressionManager::maybeCascade dropped any
// invocation > 138 B even on a 1-peer mesh — which silently broke glitchy
// (~151 B real payload) in the field.
constexpr size_t maxEventPayloadFor(uint8_t numStaggerEntries) {
  const size_t n = numStaggerEntries > kMaxStaggerEntries
                       ? kMaxStaggerEntries
                       : static_cast<size_t>(numStaggerEntries);
  return 250 - EVENT_FIXED_SIZE - (n * EVENT_STAGGER_ENTRY);
}

// MAX_PACKET_SIZE: receiver buffer sizing. CONTROL_MAX_SIZE has historically
// been the biggest (250); EVENT_MAX_SIZE is also 250; the override family
// is well under. Use a max() over the candidates so a future shrink of any
// one doesn't silently shrink the buffer.
constexpr size_t MAX_PACKET_SIZE =
    (CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE) >
            EVENT_MAX_SIZE
        ? (CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE)
        : EVENT_MAX_SIZE;

struct ParsedHello {
  uint16_t seq;
  uint8_t sourceMac[6];
  uint8_t shade[4];
  uint8_t base[4];
  // Semver packed (major<<16)|(minor<<8)|patch. Zero on pre-Phase-A peers
  // (which never reach parseHello() anyway because their PROTOCOL_VERSION
  // mismatches and inspect() rejects them).
  uint32_t firmwareVersion;
  uint8_t nameLen;
  char name[HELLO_MAX_NAME + 1];  // null-terminated copy
  // TLV-derived fields. Defaults apply when the corresponding TLV is
  // absent from the frame, so a sender that doesn't yet emit a given
  // TLV looks indistinguishable from "default value" to the receiver.
  uint8_t otaState = kOtaStateIdle;  // HELLO_TLV_OTA_STATE
  // HELLO_TLV_FW_CHANNEL — the peer's `{type}-{channel}` identity. Empty
  // when the peer doesn't emit the TLV (older firmware); the distributor
  // treats empty as "unknown → offer anyway and let the receiver gate".
  char fwChannel[HELLO_FW_CHANNEL_LEN + 1] = {0};
  // HELLO_TLV_FS_STATE — prefix of the peer's FS-image manifest digest.
  // hasFsDigest=false when absent (FS-disabled or older peer) → the FS
  // distributor won't offer to that peer.
  bool    hasFsDigest = false;
  uint8_t fsDigest[HELLO_FS_DIGEST_LEN] = {0};
};

struct ParsedControlOp {
  uint16_t seq;
  uint8_t targetMac[6];
  uint8_t sourceMac[6];
  uint16_t payloadLen;
  const uint8_t* payload;  // points into the recv buffer; caller must not retain past this call
};

// --- Wisp / override / event parsed structs ---

struct ParsedWispHello {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint32_t wispVersion;          // packed semver, same convention as ParsedHello
  uint8_t  flags;                // WISP_HELLO_FLAG_* bitfield
  // utf-8 bytes. Not necessarily null-terminated in-buffer; copied into a
  // larger-by-one storage so the parser can write a trailing '\0' for
  // easy logging. Trailing nulls inside the on-wire 8-byte slot are
  // preserved as-is (caller may treat as opaque ID prefix).
  char     paletteIdPrefix[WISP_HELLO_PALETTE_ID_PREFIX_LEN + 1];
  char     carriedFwChannel[WISP_HELLO_FW_CHANNEL_LEN + 1];
  uint32_t carriedFwVersion;
};

struct ParsedWispClaim {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointer into the recv buffer; caller must not retain past this call.
  // count * WISP_CLAIM_ENTRY_SIZE bytes, each entry being
  // (lampMac[6] + signed int8 rssi).
  const uint8_t* entries;
};

struct ParsedWispPalette {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointer into the recv buffer; caller must not retain past this call.
  // `count * 3` bytes of packed R, G, B.
  const uint8_t* rgb;
};

struct ParsedOverrideColors {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
  uint8_t         numColors;
  uint8_t         colors[kMaxOverrideColorsPerFrame][4];  // RGBW per entry
};

struct ParsedRestoreColors {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
};

struct ParsedOverrideBrightness {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
  uint8_t         brightness;   // 0..100, validated against kBrightnessOverrideMin..100
};

struct ParsedRestoreBrightness {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint16_t        fadeDurationMs;
};

struct ParsedEvent {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  EventKind      eventKind;            // typed for built-ins
  uint8_t        eventKindRaw;         // raw byte for forward-compat / user-defined
  uint8_t        numStaggerEntries;
  struct StaggerEntry {
    uint8_t  mac[6];
    uint16_t delayMs;
  } staggerEntries[kMaxStaggerEntries];
  uint16_t       payloadLen;
  const uint8_t* payload;  // points into the recv buffer; caller must not retain past this call
};

// --- Internal helpers (header-internal; not part of the public ABI). ---
namespace detail {

inline bool isValidOverrideSurfaceByte(uint8_t b) {
  return b == static_cast<uint8_t>(OverrideSurface::Base) ||
         b == static_cast<uint8_t>(OverrideSurface::Shade) ||
         b == static_cast<uint8_t>(OverrideSurface::BaseAndShade);
}

// 0x00 None, 0x01 Wisp, 0xFF Any, 0x10..0xFE user-defined.
// Reject 0x02..0x0F (reserved) explicitly.
inline bool isValidOverrideSourceByte(uint8_t b) {
  if (b == static_cast<uint8_t>(OverrideSource::None)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Wisp)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Any)) return true;
  if (b >= 0x10 && b <= 0xFE) return true;
  return false;  // 0x02..0x0F reserved
}

// Write the 6-byte header (magic + version + msgType + seq LE) to `buf`.
// `wireVersion` defaults to PROTOCOL_VERSION_EMIT; OTA-specific builders
// (FW_OFFER/CHUNK/DONE/ACCEPT/REQ/RESULT) pass an explicit version to
// emit at the peer's protocol version — see the doc block above
// PROTOCOL_VERSION_EMIT for the per-peer-version OTA model.
inline void writeHeader(uint8_t* buf, uint8_t msgType, uint16_t seq,
                        uint8_t wireVersion = PROTOCOL_VERSION_EMIT) {
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = wireVersion;
  buf[3] = msgType;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
}

}  // namespace detail

// Build a HELLO frame into `buf`. `name` is utf-8, NOT null-terminated on the wire.
// `nameLen` clamped to HELLO_MAX_NAME. `firmwareVersion` is the sender's packed
// semver (see version.hpp). `otaState` lands in HELLO_TLV_OTA_STATE — pass
// kOtaStateIdle to omit the TLV entirely (more compact for the common case).
// Returns 0 on bad args, total bytes written on success.
inline size_t buildHello(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         const uint8_t shadeRGBW[4], const uint8_t baseRGBW[4],
                         uint32_t firmwareVersion,
                         const char* name, size_t nameLen,
                         uint8_t otaState = kOtaStateIdle,
                         const char* fwChannel = nullptr,
                         const uint8_t* fsDigest = nullptr) {
  if (!buf || !sourceMac || !shadeRGBW || !baseRGBW) return 0;
  if (nameLen > HELLO_MAX_NAME) nameLen = HELLO_MAX_NAME;
  // TLV trailer: tlv_count(1) + (type(1) + len(1) + value(N)) per emitted TLV.
  // OTA_STATE (3 wire bytes) is emitted only when non-Idle; FW_CHANNEL (18
  // wire bytes: type+len+16) is emitted whenever a channel string is passed
  // (so peers can read our {type}-{channel} for the distributor's gate).
  const bool emitOtaState  = (otaState != kOtaStateIdle);
  const bool emitFwChannel = (fwChannel != nullptr && fwChannel[0] != '\0');
  const bool emitFsDigest  = (fsDigest != nullptr);
  const size_t tlvBytes = 1 + (emitOtaState ? 3 : 0) +
                          (emitFwChannel ? (2 + HELLO_FW_CHANNEL_LEN) : 0) +
                          (emitFsDigest ? (2 + HELLO_FS_DIGEST_LEN) : 0);
  const size_t total = HELLO_FIXED_SIZE + 1 + nameLen + tlvBytes;
  if (bufLen < total) return 0;
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = PROTOCOL_VERSION;
  buf[3] = MSG_HELLO;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], shadeRGBW, 4);
  std::memcpy(&buf[16], baseRGBW, 4);
  // Little-endian on the wire; matches every ESP32 we ship and matches the
  // memcpy-into-uint32 pattern parseHello uses on the receive side.
  buf[20] = static_cast<uint8_t>(firmwareVersion & 0xFF);
  buf[21] = static_cast<uint8_t>((firmwareVersion >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>((firmwareVersion >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((firmwareVersion >> 24) & 0xFF);
  buf[24] = static_cast<uint8_t>(nameLen);
  if (nameLen && name) std::memcpy(&buf[25], name, nameLen);
  // TLV trailer starts here.
  size_t off = HELLO_FIXED_SIZE + 1 + nameLen;
  buf[off++] = static_cast<uint8_t>((emitOtaState ? 1 : 0) +
                                    (emitFwChannel ? 1 : 0) +
                                    (emitFsDigest ? 1 : 0));  // tlv_count
  if (emitOtaState) {
    buf[off++] = HELLO_TLV_OTA_STATE;
    buf[off++] = 1;          // len
    buf[off++] = otaState;   // value
  }
  if (emitFwChannel) {
    buf[off++] = HELLO_TLV_FW_CHANNEL;
    buf[off++] = static_cast<uint8_t>(HELLO_FW_CHANNEL_LEN);  // len = 16
    std::memset(&buf[off], 0, HELLO_FW_CHANNEL_LEN);
    for (size_t n = 0; fwChannel[n] != '\0' && n < HELLO_FW_CHANNEL_LEN; ++n) {
      buf[off + n] = static_cast<uint8_t>(fwChannel[n]);
    }
    off += HELLO_FW_CHANNEL_LEN;
  }
  if (emitFsDigest) {
    buf[off++] = HELLO_TLV_FS_STATE;
    buf[off++] = static_cast<uint8_t>(HELLO_FS_DIGEST_LEN);  // len = 8
    std::memcpy(&buf[off], fsDigest, HELLO_FS_DIGEST_LEN);
    off += HELLO_FS_DIGEST_LEN;
  }
  return total;
}

// Build a CONTROL_OP frame. Payload is opaque (JSON in practice). Returns
// total bytes written on success, 0 on bad args / oversize.
inline size_t buildControlOp(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t targetMac[6],
                             const uint8_t sourceMac[6],
                             const uint8_t* payload, size_t payloadLen) {
  if (!buf || !targetMac || !sourceMac) return 0;
  if (payloadLen > CONTROL_MAX_PAYLOAD) return 0;
  const size_t total = CONTROL_FIXED + payloadLen;
  if (bufLen < total) return 0;
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = PROTOCOL_VERSION;
  buf[3] = MSG_CONTROL_OP;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  std::memcpy(&buf[6], targetMac, 6);
  std::memcpy(&buf[12], sourceMac, 6);
  buf[18] = static_cast<uint8_t>(payloadLen & 0xFF);
  buf[19] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
  if (payloadLen && payload) std::memcpy(&buf[CONTROL_FIXED], payload, payloadLen);
  return total;
}

// --- Wisp / override / event builders ---

// Build a MSG_WISP_CLAIM frame. `entries` is `count` packed records, each
// 7 bytes: lampMac(6) + signed int8 rssi(1). `count` must be ≤
// kMaxWispClaimEntries. Returns total bytes written on success, 0 on
// bad args / insufficient buffer.
inline size_t buildWispClaim(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t sourceMac[6],
                             const uint8_t* entries,
                             size_t count) {
  if (!buf || !sourceMac) return 0;
  if (count > kMaxWispClaimEntries) return 0;
  if (count > 0 && !entries) return 0;
  const size_t total = WISP_CLAIM_FIXED_PREFIX + count * WISP_CLAIM_ENTRY_SIZE;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_WISP_CLAIM, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = static_cast<uint8_t>(count);
  if (count) {
    std::memcpy(&buf[WISP_CLAIM_FIXED_PREFIX], entries,
                count * WISP_CLAIM_ENTRY_SIZE);
  }
  return total;
}

// Build a MSG_WISP_PALETTE frame. `rgb` is `count * 3` bytes packed R,G,B;
// caller is responsible for capping count at kMaxWispPaletteColors. Returns
// total bytes written on success, 0 on bad args / insufficient buffer.
inline size_t buildWispPalette(uint8_t* buf, size_t bufLen, uint16_t seq,
                               const uint8_t sourceMac[6],
                               const uint8_t* rgb,
                               size_t count) {
  if (!buf || !sourceMac) return 0;
  if (count > kMaxWispPaletteColors) return 0;
  if (count > 0 && !rgb) return 0;
  const size_t total =
      WISP_PALETTE_FIXED_PREFIX + count * WISP_PALETTE_ENTRY_SIZE;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_WISP_PALETTE, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = static_cast<uint8_t>(count);
  if (count) {
    std::memcpy(&buf[WISP_PALETTE_FIXED_PREFIX], rgb,
                count * WISP_PALETTE_ENTRY_SIZE);
  }
  return total;
}

// Build a MSG_WISP_HELLO frame. `paletteIdPrefix` and `carriedFwChannel`
// are fixed-width 8-byte slots, zero-padded if shorter. Strings longer than
// 8 bytes are truncated. Returns WISP_HELLO_FIXED_SIZE on success, 0 on
// bad args.
inline size_t buildWispHello(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t sourceMac[6],
                             uint32_t wispVersion,
                             uint8_t flags,
                             const char* paletteIdPrefix, size_t paletteIdPrefixLen,
                             const char* carriedFwChannel, size_t carriedFwChannelLen,
                             uint32_t carriedFwVersion) {
  if (!buf || !sourceMac) return 0;
  if (bufLen < WISP_HELLO_FIXED_SIZE) return 0;
  detail::writeHeader(buf, MSG_WISP_HELLO, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = static_cast<uint8_t>(wispVersion & 0xFF);
  buf[13] = static_cast<uint8_t>((wispVersion >> 8) & 0xFF);
  buf[14] = static_cast<uint8_t>((wispVersion >> 16) & 0xFF);
  buf[15] = static_cast<uint8_t>((wispVersion >> 24) & 0xFF);
  buf[16] = flags;
  // Zero-pad the two fixed-width string slots before copying so short
  // values land cleanly.
  std::memset(&buf[17], 0, WISP_HELLO_PALETTE_ID_PREFIX_LEN);
  if (paletteIdPrefix && paletteIdPrefixLen) {
    const size_t n = paletteIdPrefixLen > WISP_HELLO_PALETTE_ID_PREFIX_LEN
                         ? WISP_HELLO_PALETTE_ID_PREFIX_LEN
                         : paletteIdPrefixLen;
    std::memcpy(&buf[17], paletteIdPrefix, n);
  }
  std::memset(&buf[17 + WISP_HELLO_PALETTE_ID_PREFIX_LEN], 0,
              WISP_HELLO_FW_CHANNEL_LEN);
  if (carriedFwChannel && carriedFwChannelLen) {
    const size_t n = carriedFwChannelLen > WISP_HELLO_FW_CHANNEL_LEN
                         ? WISP_HELLO_FW_CHANNEL_LEN
                         : carriedFwChannelLen;
    std::memcpy(&buf[17 + WISP_HELLO_PALETTE_ID_PREFIX_LEN],
                carriedFwChannel, n);
  }
  const size_t fwOff = 17 + WISP_HELLO_PALETTE_ID_PREFIX_LEN +
                       WISP_HELLO_FW_CHANNEL_LEN;
  buf[fwOff]     = static_cast<uint8_t>(carriedFwVersion & 0xFF);
  buf[fwOff + 1] = static_cast<uint8_t>((carriedFwVersion >> 8) & 0xFF);
  buf[fwOff + 2] = static_cast<uint8_t>((carriedFwVersion >> 16) & 0xFF);
  buf[fwOff + 3] = static_cast<uint8_t>((carriedFwVersion >> 24) & 0xFF);
  // v0x05 TLV trailer. Wisp doesn't emit any TLVs yet; future fields
  // can be added by extending this builder and parseWispHello in lockstep.
  if (bufLen < WISP_HELLO_FIXED_SIZE + 1) return 0;
  buf[WISP_HELLO_FIXED_SIZE] = 0;  // tlv_count
  return WISP_HELLO_FIXED_SIZE + 1;
}

// Build a MSG_OVERRIDE_COLORS frame. `numColors` must be 1..kMaxOverrideColorsPerFrame.
// `colorsRGBW` is numColors * 4 bytes. Returns total bytes written on success.
inline size_t buildOverrideColors(uint8_t* buf, size_t bufLen, uint16_t seq,
                                  const uint8_t sourceMac[6],
                                  const uint8_t targetMac[6],
                                  OverrideSurface surface,
                                  OverrideSource sourceKind,
                                  uint16_t fadeDurationMs,
                                  const uint8_t* colorsRGBW,
                                  uint8_t numColors) {
  if (!buf || !sourceMac || !targetMac || !colorsRGBW) return 0;
  if (numColors == 0 || numColors > kMaxOverrideColorsPerFrame) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  const size_t total = OVERRIDE_COLORS_FIXED_SIZE + numColors * 4u;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_OVERRIDE_COLORS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  buf[22] = numColors;
  std::memcpy(&buf[OVERRIDE_COLORS_FIXED_SIZE], colorsRGBW, numColors * 4u);
  return total;
}

// Build a MSG_RESTORE_COLORS frame. Returns RESTORE_FIXED_SIZE on success.
inline size_t buildRestoreColors(uint8_t* buf, size_t bufLen, uint16_t seq,
                                 const uint8_t sourceMac[6],
                                 const uint8_t targetMac[6],
                                 OverrideSurface surface,
                                 OverrideSource sourceKind,
                                 uint16_t fadeDurationMs) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < RESTORE_FIXED_SIZE) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_RESTORE_COLORS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  return RESTORE_FIXED_SIZE;
}

// Build a MSG_OVERRIDE_BRIGHTNESS frame. `brightness` clamped to
// [kBrightnessOverrideMin, 100] — values outside are rejected (returns 0),
// not silently clamped, so callers can spot bugs.
inline size_t buildOverrideBrightness(uint8_t* buf, size_t bufLen, uint16_t seq,
                                      const uint8_t sourceMac[6],
                                      const uint8_t targetMac[6],
                                      OverrideSurface surface,
                                      OverrideSource sourceKind,
                                      uint16_t fadeDurationMs,
                                      uint8_t brightness) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < OVERRIDE_BRIGHTNESS_FIXED_SIZE) return 0;
  if (brightness < kBrightnessOverrideMin || brightness > 100) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_OVERRIDE_BRIGHTNESS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  buf[22] = brightness;
  return OVERRIDE_BRIGHTNESS_FIXED_SIZE;
}

// Build a MSG_RESTORE_BRIGHTNESS frame. Same layout as RESTORE_COLORS.
inline size_t buildRestoreBrightness(uint8_t* buf, size_t bufLen, uint16_t seq,
                                     const uint8_t sourceMac[6],
                                     const uint8_t targetMac[6],
                                     OverrideSurface surface,
                                     OverrideSource sourceKind,
                                     uint16_t fadeDurationMs) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < RESTORE_FIXED_SIZE) return 0;
  if (!detail::isValidOverrideSurfaceByte(static_cast<uint8_t>(surface))) return 0;
  if (!detail::isValidOverrideSourceByte(static_cast<uint8_t>(sourceKind))) return 0;
  detail::writeHeader(buf, MSG_RESTORE_BRIGHTNESS, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(surface);
  buf[19] = static_cast<uint8_t>(sourceKind);
  buf[20] = static_cast<uint8_t>(fadeDurationMs & 0xFF);
  buf[21] = static_cast<uint8_t>((fadeDurationMs >> 8) & 0xFF);
  return RESTORE_FIXED_SIZE;
}

// Build a MSG_EVENT frame. `eventKind` is the raw byte so callers can emit
// user-defined kinds in 0x10..0xFF without re-extending the enum. Stagger
// entries are `numStaggerEntries * (mac(6) + delayMs(2 LE))`; capped at
// kMaxStaggerEntries. Payload is opaque (JSON in practice); capped at
// maxEventPayloadFor(numStaggerEntries) so the full frame stays within
// ESP-NOW's 250-byte limit. Dynamic in the actual stagger count, not the
// worst case, so small meshes can carry larger invocations.
inline size_t buildEvent(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         uint8_t eventKind,
                         const uint8_t* staggerMacs,    // numStaggerEntries * 6
                         const uint16_t* staggerDelays, // numStaggerEntries
                         uint8_t numStaggerEntries,
                         const uint8_t* payload, uint16_t payloadLen) {
  if (!buf || !sourceMac) return 0;
  if (numStaggerEntries > kMaxStaggerEntries) return 0;
  if (numStaggerEntries > 0 && (!staggerMacs || !staggerDelays)) return 0;
  if (payloadLen > maxEventPayloadFor(numStaggerEntries)) return 0;
  const size_t total = EVENT_FIXED_SIZE +
                       static_cast<size_t>(numStaggerEntries) * EVENT_STAGGER_ENTRY +
                       payloadLen;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_EVENT, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = eventKind;
  buf[13] = numStaggerEntries;
  size_t off = 14;
  for (uint8_t i = 0; i < numStaggerEntries; ++i) {
    std::memcpy(&buf[off], &staggerMacs[i * 6], 6);
    off += 6;
    buf[off]     = static_cast<uint8_t>(staggerDelays[i] & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((staggerDelays[i] >> 8) & 0xFF);
    off += 2;
  }
  buf[off]     = static_cast<uint8_t>(payloadLen & 0xFF);
  buf[off + 1] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
  off += 2;
  if (payloadLen && payload) std::memcpy(&buf[off], payload, payloadLen);
  return total;
}

// Validate magic + version. Returns the msg type byte verbatim or 0 if
// invalid. C.3 retired FLAG_LOCAL_ONLY; the high bit is reserved for
// future use, so we no longer mask it — any future reuse of that bit
// will surface immediately as an unrecognised msgType.
inline uint8_t inspect(const uint8_t* data, size_t len) {
  if (!data || len < HEADER_SIZE) return 0;
  if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return 0;
  // Accept any version in [RX_MIN, RX_MAX]. See the doc block above
  // PROTOCOL_VERSION_EMIT for the legacy receive model.
  if (data[2] < PROTOCOL_VERSION_RX_MIN || data[2] > PROTOCOL_VERSION_RX_MAX) {
    return 0;
  }
  return data[3];
}

inline bool parseControlOp(const uint8_t* data, size_t len, ParsedControlOp& out) {
  if (inspect(data, len) != MSG_CONTROL_OP || len < CONTROL_FIXED) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.targetMac, &data[6], 6);
  std::memcpy(out.sourceMac, &data[12], 6);
  out.payloadLen = static_cast<uint16_t>(data[18]) | (static_cast<uint16_t>(data[19]) << 8);
  if (out.payloadLen > CONTROL_MAX_PAYLOAD) return false;
  if (len < CONTROL_FIXED + out.payloadLen) return false;
  out.payload = &data[CONTROL_FIXED];
  return true;
}

inline bool parseHello(const uint8_t* data, size_t len, ParsedHello& out) {
  if (inspect(data, len) != MSG_HELLO || len < HELLO_FIXED_SIZE + 1) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.shade, &data[12], 4);
  std::memcpy(out.base, &data[16], 4);
  out.firmwareVersion =
       static_cast<uint32_t>(data[20])
     | (static_cast<uint32_t>(data[21]) << 8)
     | (static_cast<uint32_t>(data[22]) << 16)
     | (static_cast<uint32_t>(data[23]) << 24);
  const uint8_t rawLen = data[24];
  const uint8_t nameLen = rawLen > HELLO_MAX_NAME ? HELLO_MAX_NAME : rawLen;
  if (len < static_cast<size_t>(HELLO_FIXED_SIZE + 1 + nameLen)) return false;
  out.nameLen = nameLen;
  if (nameLen) std::memcpy(out.name, &data[25], nameLen);
  out.name[nameLen] = '\0';
  // TLV trailer. v0x05 mandates at least a tlv_count byte, but be
  // tolerant of frames that omit it (some future buggy sender that
  // builds the prefix correctly but forgets the trailer) — those just
  // get default TLV-derived fields.
  out.otaState = kOtaStateIdle;
  out.fwChannel[0] = '\0';
  size_t off = HELLO_FIXED_SIZE + 1 + nameLen;
  if (len <= off) return true;
  const uint8_t tlvCount = data[off++];
  for (uint8_t i = 0; i < tlvCount; ++i) {
    // Need at least type+len bytes to advance.
    if (len < off + 2) return false;
    const uint8_t tlvType = data[off++];
    const uint8_t tlvLen  = data[off++];
    if (len < off + tlvLen) return false;
    // Known TLV types update structured fields; unknowns are
    // intentionally skipped via the length byte (this is the
    // forward-compat hinge — adding a new TLV type doesn't break
    // existing parsers).
    if (tlvType == HELLO_TLV_OTA_STATE && tlvLen == 1) {
      out.otaState = data[off];
    } else if (tlvType == HELLO_TLV_FW_CHANNEL &&
               tlvLen == HELLO_FW_CHANNEL_LEN) {
      std::memcpy(out.fwChannel, &data[off], HELLO_FW_CHANNEL_LEN);
      out.fwChannel[HELLO_FW_CHANNEL_LEN] = '\0';
    } else if (tlvType == HELLO_TLV_FS_STATE &&
               tlvLen == HELLO_FS_DIGEST_LEN) {
      std::memcpy(out.fsDigest, &data[off], HELLO_FS_DIGEST_LEN);
      out.hasFsDigest = true;
    }
    off += tlvLen;
  }
  return true;
}

// --- Wisp / override / event parsers ---

inline bool parseWispClaim(const uint8_t* data, size_t len, ParsedWispClaim& out) {
  if (inspect(data, len) != MSG_WISP_CLAIM) return false;
  if (len < WISP_CLAIM_FIXED_PREFIX) return false;
  const uint8_t count = data[12];
  if (count > kMaxWispClaimEntries) return false;
  const size_t expected = WISP_CLAIM_FIXED_PREFIX +
                          static_cast<size_t>(count) * WISP_CLAIM_ENTRY_SIZE;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.count = count;
  out.entries = count ? &data[WISP_CLAIM_FIXED_PREFIX] : nullptr;
  return true;
}

inline bool parseWispPalette(const uint8_t* data, size_t len,
                             ParsedWispPalette& out) {
  if (inspect(data, len) != MSG_WISP_PALETTE) return false;
  if (len < WISP_PALETTE_FIXED_PREFIX) return false;
  const uint8_t count = data[12];
  if (count > kMaxWispPaletteColors) return false;
  const size_t expected = WISP_PALETTE_FIXED_PREFIX +
                          static_cast<size_t>(count) * WISP_PALETTE_ENTRY_SIZE;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.count = count;
  out.rgb = count ? &data[WISP_PALETTE_FIXED_PREFIX] : nullptr;
  return true;
}

inline bool parseWispHello(const uint8_t* data, size_t len, ParsedWispHello& out) {
  if (inspect(data, len) != MSG_WISP_HELLO || len < WISP_HELLO_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.wispVersion =
       static_cast<uint32_t>(data[12])
     | (static_cast<uint32_t>(data[13]) << 8)
     | (static_cast<uint32_t>(data[14]) << 16)
     | (static_cast<uint32_t>(data[15]) << 24);
  out.flags = data[16];
  std::memcpy(out.paletteIdPrefix, &data[17], WISP_HELLO_PALETTE_ID_PREFIX_LEN);
  out.paletteIdPrefix[WISP_HELLO_PALETTE_ID_PREFIX_LEN] = '\0';
  std::memcpy(out.carriedFwChannel,
              &data[17 + WISP_HELLO_PALETTE_ID_PREFIX_LEN],
              WISP_HELLO_FW_CHANNEL_LEN);
  out.carriedFwChannel[WISP_HELLO_FW_CHANNEL_LEN] = '\0';
  const size_t fwOff = 17 + WISP_HELLO_PALETTE_ID_PREFIX_LEN +
                       WISP_HELLO_FW_CHANNEL_LEN;
  out.carriedFwVersion =
       static_cast<uint32_t>(data[fwOff])
     | (static_cast<uint32_t>(data[fwOff + 1]) << 8)
     | (static_cast<uint32_t>(data[fwOff + 2]) << 16)
     | (static_cast<uint32_t>(data[fwOff + 3]) << 24);
  // TLV trailer (v0x05+). Tolerant of frames that omit it for the
  // same reason parseHello is — keep the older fixed-size still
  // parseable until every wisp ships v0x05. No known TLV types yet
  // for WISP_HELLO; the loop walks + skips by length so future
  // additions don't need to change parseWispHello.
  size_t off = WISP_HELLO_FIXED_SIZE;
  if (len <= off) return true;
  const uint8_t tlvCount = data[off++];
  for (uint8_t i = 0; i < tlvCount; ++i) {
    if (len < off + 2) return false;
    const uint8_t tlvLen = data[off + 1];
    if (len < off + 2 + tlvLen) return false;
    // No known WISP_HELLO TLV types route into ParsedWispHello yet —
    // walk past unknowns by length.
    off += 2 + tlvLen;
  }
  return true;
}

inline bool parseOverrideColors(const uint8_t* data, size_t len,
                                ParsedOverrideColors& out) {
  if (inspect(data, len) != MSG_OVERRIDE_COLORS) return false;
  if (len < OVERRIDE_COLORS_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  const uint8_t numColors = data[22];
  if (numColors < 1 || numColors > kMaxOverrideColorsPerFrame) return false;
  const size_t expected = OVERRIDE_COLORS_FIXED_SIZE + numColors * 4u;
  if (len != expected) return false;  // exact-match — silent drop on length mismatch
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  out.numColors = numColors;
  std::memcpy(out.colors, &data[OVERRIDE_COLORS_FIXED_SIZE], numColors * 4u);
  return true;
}

inline bool parseRestoreColors(const uint8_t* data, size_t len,
                               ParsedRestoreColors& out) {
  if (inspect(data, len) != MSG_RESTORE_COLORS) return false;
  if (len < RESTORE_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  return true;
}

inline bool parseOverrideBrightness(const uint8_t* data, size_t len,
                                    ParsedOverrideBrightness& out) {
  if (inspect(data, len) != MSG_OVERRIDE_BRIGHTNESS) return false;
  if (len < OVERRIDE_BRIGHTNESS_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  const uint8_t brightness = data[22];
  if (brightness < kBrightnessOverrideMin || brightness > 100) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  out.brightness = brightness;
  return true;
}

inline bool parseRestoreBrightness(const uint8_t* data, size_t len,
                                   ParsedRestoreBrightness& out) {
  if (inspect(data, len) != MSG_RESTORE_BRIGHTNESS) return false;
  if (len < RESTORE_FIXED_SIZE) return false;
  if (!detail::isValidOverrideSurfaceByte(data[18])) return false;
  if (!detail::isValidOverrideSourceByte(data[19])) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  return true;
}

inline bool parseEvent(const uint8_t* data, size_t len, ParsedEvent& out) {
  if (inspect(data, len) != MSG_EVENT) return false;
  if (len < EVENT_FIXED_SIZE) return false;
  // v0x03 lock-in: reject if the reserved high bit on numStaggerEntries is
  // set. EXPLICIT check (not relying on the > kMaxStaggerEntries clamp
  // below to catch it incidentally) so a future refactor that masks low
  // bits first can't silently start accepting the bit. See
  // kStaggerCountReservedHighBit declaration for the rationale.
  // why: forward-compat reservation per validated plan §"Layer 3".
  if (data[13] & kStaggerCountReservedHighBit) return false;
  const uint8_t numStaggerEntries = data[13];
  if (numStaggerEntries > kMaxStaggerEntries) return false;
  const size_t staggerBytes =
      static_cast<size_t>(numStaggerEntries) * EVENT_STAGGER_ENTRY;
  // Need room for the stagger block + the 2-byte payloadLen.
  if (len < EVENT_FIXED_SIZE + staggerBytes) return false;
  const size_t payloadLenOff = HEADER_SIZE + 6 + 1 + 1 + staggerBytes;  // 14 + staggerBytes
  const uint16_t payloadLen =
       static_cast<uint16_t>(data[payloadLenOff])
     | (static_cast<uint16_t>(data[payloadLenOff + 1]) << 8);
  if (payloadLen > maxEventPayloadFor(numStaggerEntries)) return false;
  const size_t expected = EVENT_FIXED_SIZE + staggerBytes + payloadLen;
  if (len != expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.eventKindRaw = data[12];
  out.eventKind = static_cast<EventKind>(out.eventKindRaw);
  out.numStaggerEntries = numStaggerEntries;
  size_t off = 14;
  for (uint8_t i = 0; i < numStaggerEntries; ++i) {
    std::memcpy(out.staggerEntries[i].mac, &data[off], 6);
    off += 6;
    out.staggerEntries[i].delayMs =
         static_cast<uint16_t>(data[off])
       | (static_cast<uint16_t>(data[off + 1]) << 8);
    off += 2;
  }
  out.payloadLen = payloadLen;
  out.payload    = payloadLen ? &data[payloadLenOff + 2] : nullptr;
  return true;
}

// =============================================================================
// MSG_FW_* — OTA firmware-distribution family (msgType 0x40..0x45)
// =============================================================================
//
// Wire-format contract per /Users/jerrett/.claude/plans/wisp-ota-reconciliation.md.
// All MSG_FW_* messages carry sourceMac(6) + targetMac(6) right after the
// 6-byte header — same ordering convention as MSG_OVERRIDE_COLORS
// (`buildOverrideColors` at line ~449-451). The MSG_FW_* family is
// SINGLE-HOP unicast (addressedToUs filter on the lamp side; no gossip
// relay), unlike HELLO/CONTROL_OP/WISP_HELLO/EVENT which gossip-relay.
//
// Direction:
//   OFFER, CHUNK, DONE      — wisp → lamp (sent via wisp's unicast MeshLink::send)
//   ACCEPT, REQ, RESULT     — lamp → wisp (sent via lamp's EspNowLink::broadcast,
//                              with wisp filtering via addressedToUs on its MAC)
//
// Channel mismatch is a SILENT DROP on the lamp side — no ACCEPT or RESULT
// is ever emitted with a channel-mismatch reason (per scope decision #2).

constexpr uint8_t  MSG_FW_OFFER  = 0x40;
constexpr uint8_t  MSG_FW_ACCEPT = 0x41;
constexpr uint8_t  MSG_FW_CHUNK  = 0x42;
constexpr uint8_t  MSG_FW_REQ    = 0x43;
constexpr uint8_t  MSG_FW_DONE   = 0x44;
constexpr uint8_t  MSG_FW_RESULT = 0x45;

// Filesystem-image OTA (the SPIFFS web-UI image → the spiffs partition).
// Same frame layouts and single-hop convention as MSG_FW_*; the distinct
// msgType is what routes a frame to the FS receiver/distributor (and to the
// spiffs partition) instead of the firmware path — so the builders/parsers
// below are parameterized by msgType and shared, not duplicated. Additive
// type IDs (< kReservedMsgTypeHighBit): an old lamp that doesn't know them
// silently drops them — no PROTOCOL_VERSION bump.
constexpr uint8_t  MSG_FS_OFFER  = 0x46;
constexpr uint8_t  MSG_FS_ACCEPT = 0x47;
constexpr uint8_t  MSG_FS_CHUNK  = 0x48;
constexpr uint8_t  MSG_FS_REQ    = 0x49;
constexpr uint8_t  MSG_FS_DONE   = 0x4A;
constexpr uint8_t  MSG_FS_RESULT = 0x4B;
static_assert(MSG_FS_RESULT < kReservedMsgTypeHighBit,
              "MSG_FS_* must stay below the reserved high-bit type space");

// Channel string is zero-padded ASCII, fixed-width. Carries `{type}-{channel}`
// (e.g. "standard-stable", "snafu-beta") so the existing silent-drop on
// mismatch enforces per-variant OTA gating without a separate type field.
constexpr size_t   FW_CHANNEL_LEN       = 16;
// The HELLO fw-channel TLV carries the same {type}-{channel} identity, so its
// width must match or the distributor's gate compares mismatched lengths.
static_assert(FW_CHANNEL_LEN == HELLO_FW_CHANNEL_LEN,
              "HELLO_TLV_FW_CHANNEL width must equal the OFFER/footer channel");
// First 8 bytes of sha256(signed region) — image fingerprint, NOT signature
// prefix. The signature itself is 64 bytes inside the LSIG footer.
constexpr size_t   FW_SHA256_PREFIX_LEN = 8;
// Hard-locked chunk payload size in v1. Receiver rejects offers that don't
// carry chunkSize == 200; no negotiation.
constexpr uint16_t FW_CHUNK_SIZE        = 200;

// Fixed-size frame totals. Bytes-on-wire layouts come from the
// reconciliation doc's per-msgType sections; see comments on each builder.
constexpr size_t   FW_OFFER_FIXED_SIZE  = 56;   // hdr(6)+src(6)+tgt(6) + body(38)
// MSG_FW_OFFER body field offsets — named so widening any field becomes a
// single-line shift rather than hunting literals through build/parse.
constexpr size_t   FW_OFFER_OFF_VERSION      = 18;
constexpr size_t   FW_OFFER_OFF_TOTAL_LEN    = 22;
constexpr size_t   FW_OFFER_OFF_CHUNK_SIZE   = 26;
constexpr size_t   FW_OFFER_OFF_CHANNEL      = 28;
constexpr size_t   FW_OFFER_OFF_SHA256       = FW_OFFER_OFF_CHANNEL + FW_CHANNEL_LEN;       // 44
constexpr size_t   FW_OFFER_OFF_FOOTER_LEN   = FW_OFFER_OFF_SHA256 + FW_SHA256_PREFIX_LEN;  // 52
constexpr size_t   FW_OFFER_OFF_TOTAL_CHUNKS = FW_OFFER_OFF_FOOTER_LEN + 2;                 // 54
static_assert(FW_OFFER_OFF_TOTAL_CHUNKS + 2 == FW_OFFER_FIXED_SIZE,
              "FW OFFER field layout must end at FW_OFFER_FIXED_SIZE");
constexpr size_t   FW_ACCEPT_FIXED_SIZE = 28;   // hdr(6)+src(6)+tgt(6) + body(10)
constexpr size_t   FW_CHUNK_FIXED_SIZE  = 26;   // hdr(6)+src(6)+tgt(6) + body(8) (payload trails)
constexpr size_t   FW_CHUNK_MAX_SIZE    = FW_CHUNK_FIXED_SIZE + FW_CHUNK_SIZE;  // 226
constexpr size_t   FW_REQ_FIXED_SIZE    = 24;   // hdr(6)+src(6)+tgt(6) + body(6)
constexpr size_t   FW_DONE_FIXED_SIZE   = 38;   // hdr(6)+src(6)+tgt(6) + body(20)
constexpr size_t   FW_RESULT_FIXED_SIZE = 24;   // hdr(6)+src(6)+tgt(6) + body(6)

// Lock-in static asserts. A future refactor that shifts a byte will fail
// to compile here rather than silently sending mismatched frames.
static_assert(FW_OFFER_FIXED_SIZE  == 56, "FW OFFER size lock");
static_assert(FW_ACCEPT_FIXED_SIZE == 28, "FW ACCEPT size lock");
static_assert(FW_CHUNK_FIXED_SIZE  == 26, "FW CHUNK header lock");
static_assert(FW_REQ_FIXED_SIZE    == 24, "FW REQ size lock");
static_assert(FW_DONE_FIXED_SIZE   == 38, "FW DONE size lock");
static_assert(FW_RESULT_FIXED_SIZE == 24, "FW RESULT size lock");
static_assert(FW_CHUNK_MAX_SIZE    <= 250, "ESP-NOW frame cap");
static_assert(FW_OFFER_FIXED_SIZE  <= 250, "ESP-NOW frame cap");

// ACCEPT status byte. 0 = accept-and-stream; 1 = busy (mid-flow already);
// 2 = already-current (offer.version <= mine). Channel mismatch is NEVER
// emitted as an ACCEPT — it's a silent drop on the lamp side.
enum class FwAcceptStatus : uint8_t {
  Accept                = 0,
  DeclineBusy           = 1,
  DeclineAlreadyCurrent = 2,
};

// REQ reason. Diagnostic-only; wisp logs it.
enum class FwReqReason : uint8_t {
  Gap           = 0,  // explicit gap fill from receiver-side bitmap scan
  StallWatchdog = 1,  // 2s without progress; emit one REQ for the lowest gap
};

// RESULT status enum. uint8_t on the wire; values 9..255 reserved for
// forward-compat. Wisp side treats unknown codes as "abort + log + back off".
enum class FwResultStatus : uint8_t {
  Success            = 0,  // verified + boot partition set + rebooting
  SignatureFail      = 1,
  VersionMismatch    = 2,  // DONE.version != OFFER.version (mid-stream swap)
  PartitionWriteFail = 3,
  PartitionReadFail  = 4,
  OtaBeginFail       = 5,
  OtaEndFail         = 6,
  SetBootFail        = 7,
  OfferShaMismatch   = 8,  // sha256Prefix mismatch on verify
  // FS-image OTA reuses this status byte (MSG_FS_RESULT shares the RESULT
  // frame). These take values from the firmware enum's reserved 9..255 range.
  FsMountFail        = 9,   // spiffs unmountable after write → can't recompute digest
  FsDigestMismatch   = 10,  // recomputed manifest digest != fw.lsig signature
  // 11..255 reserved
};

// --- Parsed structs -------------------------------------------------------

struct ParsedFwOffer {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  targetMac[6];
  uint32_t version;
  uint32_t totalLen;
  uint16_t chunkSize;
  char     channel[FW_CHANNEL_LEN + 1];        // null-terminated for logging
  uint8_t  sha256Prefix[FW_SHA256_PREFIX_LEN];
  uint16_t footerLen;
  uint16_t totalChunks;
};

struct ParsedFwAccept {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  uint16_t       offerSeq;
  uint32_t       version;
  FwAcceptStatus status;
  uint32_t       resumeOffset;
};

struct ParsedFwChunk {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  uint16_t       chunkIdx;
  uint32_t       offset;
  uint16_t       len;
  const uint8_t* bytes;  // points into recv buffer; caller must not retain
};

struct ParsedFwReq {
  uint16_t    seq;
  uint8_t     sourceMac[6];
  uint8_t     targetMac[6];
  uint16_t    firstChunkIdx;
  uint16_t    chunkCount;
  FwReqReason reason;
};

struct ParsedFwDone {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  targetMac[6];
  uint32_t version;
  uint32_t totalLen;
  uint8_t  sha256Prefix[FW_SHA256_PREFIX_LEN];
  uint16_t footerLen;
};

struct ParsedFwResult {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  FwResultStatus status;
  uint8_t        detail;
  uint32_t       version;
};

// --- Builders -------------------------------------------------------------
//
// Convention mirrors buildOverrideColors at line ~435:
//   - Returns total bytes written on success, 0 on bad args / oversize buf.
//   - Source MAC written to bytes 6..11; target MAC to bytes 12..17.
//   - All multi-byte integers are little-endian on the wire.

// MSG_FW_OFFER (48 bytes):
//   hdr(6) + src(6) + tgt(6) +
//   version(4 LE) + totalLen(4 LE) + chunkSize(2 LE) + channel(8 zero-pad)
//   + sha256Prefix(8) + footerLen(2 LE) + totalChunks(2 LE)
inline size_t buildFwOffer(uint8_t* buf, size_t bufLen, uint16_t seq,
                           const uint8_t sourceMac[6], const uint8_t targetMac[6],
                           uint32_t version, uint32_t totalLen, uint16_t chunkSize,
                           const char* channel, size_t channelLen,
                           const uint8_t sha256Prefix[FW_SHA256_PREFIX_LEN],
                           uint16_t footerLen, uint16_t totalChunks,
                           uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                           uint8_t msgType = MSG_FW_OFFER) {
  if (!buf || !sourceMac || !targetMac || !sha256Prefix) return 0;
  if (bufLen < FW_OFFER_FIXED_SIZE) return 0;
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[FW_OFFER_OFF_VERSION    ] = static_cast<uint8_t>(version & 0xFF);
  buf[FW_OFFER_OFF_VERSION + 1] = static_cast<uint8_t>((version >> 8) & 0xFF);
  buf[FW_OFFER_OFF_VERSION + 2] = static_cast<uint8_t>((version >> 16) & 0xFF);
  buf[FW_OFFER_OFF_VERSION + 3] = static_cast<uint8_t>((version >> 24) & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_LEN    ] = static_cast<uint8_t>(totalLen & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_LEN + 1] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_LEN + 2] = static_cast<uint8_t>((totalLen >> 16) & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_LEN + 3] = static_cast<uint8_t>((totalLen >> 24) & 0xFF);
  buf[FW_OFFER_OFF_CHUNK_SIZE    ] = static_cast<uint8_t>(chunkSize & 0xFF);
  buf[FW_OFFER_OFF_CHUNK_SIZE + 1] = static_cast<uint8_t>((chunkSize >> 8) & 0xFF);
  // Channel: zero-pad to FW_CHANNEL_LEN bytes. Truncate if caller passed more.
  std::memset(&buf[FW_OFFER_OFF_CHANNEL], 0, FW_CHANNEL_LEN);
  if (channel && channelLen) {
    const size_t n = channelLen > FW_CHANNEL_LEN ? FW_CHANNEL_LEN : channelLen;
    std::memcpy(&buf[FW_OFFER_OFF_CHANNEL], channel, n);
  }
  std::memcpy(&buf[FW_OFFER_OFF_SHA256], sha256Prefix, FW_SHA256_PREFIX_LEN);
  buf[FW_OFFER_OFF_FOOTER_LEN    ] = static_cast<uint8_t>(footerLen & 0xFF);
  buf[FW_OFFER_OFF_FOOTER_LEN + 1] = static_cast<uint8_t>((footerLen >> 8) & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_CHUNKS    ] = static_cast<uint8_t>(totalChunks & 0xFF);
  buf[FW_OFFER_OFF_TOTAL_CHUNKS + 1] = static_cast<uint8_t>((totalChunks >> 8) & 0xFF);
  return FW_OFFER_FIXED_SIZE;
}

// MSG_FW_ACCEPT (28 bytes):
//   hdr(6) + src(6) + tgt(6) + body(10)
// Body: offerSeq(2 LE) + version(4 LE) + status(1) + reserved(3)
//
// IMPLEMENTER NOTE: the reconciliation doc's body layout listed
// `offerSeq(2) + version(4) + status(1) + resumeOffset(4) + reserved(1)`
// = 12 bytes, but the fixed-size constant (28) only allows 10 body bytes.
// The doc's closing note ("If a byte-count cross-check disagrees, the
// layouts above are authoritative and these constants get re-derived")
// is in tension with the explicit FW_ACCEPT_FIXED_SIZE = 28 lock-in.
// Resolution: lock to 28 bytes — drop `resumeOffset` from the wire (it
// was reserved-zero in v1 anyway per the doc's own "Field-merger notes"),
// pack 3 reserved bytes after status. Keep `resumeOffset` in
// ParsedFwAccept as uint32_t for source-compat with callers; it always
// parses to 0 until a future protocol revision reuses those 3 bytes.
inline size_t buildFwAccept(uint8_t* buf, size_t bufLen, uint16_t seq,
                            const uint8_t sourceMac[6], const uint8_t targetMac[6],
                            uint16_t offerSeq, uint32_t version,
                            FwAcceptStatus status, uint32_t resumeOffset,
                            uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                            uint8_t msgType = MSG_FW_ACCEPT) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < FW_ACCEPT_FIXED_SIZE) return 0;
  (void)resumeOffset;  // reserved-zero in v1; pinned by the layout note above
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(offerSeq & 0xFF);
  buf[19] = static_cast<uint8_t>((offerSeq >> 8) & 0xFF);
  buf[20] = static_cast<uint8_t>(version & 0xFF);
  buf[21] = static_cast<uint8_t>((version >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>((version >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((version >> 24) & 0xFF);
  buf[24] = static_cast<uint8_t>(status);
  buf[25] = 0;  // reserved
  buf[26] = 0;  // reserved
  buf[27] = 0;  // reserved
  return FW_ACCEPT_FIXED_SIZE;
}

// MSG_FW_CHUNK (26 + len bytes):
//   hdr(6) + src(6) + tgt(6) + chunkIdx(2 LE) + offset(4 LE) + len(2 LE) + payload(len)
inline size_t buildFwChunk(uint8_t* buf, size_t bufLen, uint16_t seq,
                           const uint8_t sourceMac[6], const uint8_t targetMac[6],
                           uint16_t chunkIdx, uint32_t offset,
                           const uint8_t* bytes, uint16_t len,
                           uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                           uint8_t msgType = MSG_FW_CHUNK) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (len == 0 || len > FW_CHUNK_SIZE) return 0;
  if (len > 0 && !bytes) return 0;
  const size_t total = FW_CHUNK_FIXED_SIZE + static_cast<size_t>(len);
  if (bufLen < total) return 0;
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(chunkIdx & 0xFF);
  buf[19] = static_cast<uint8_t>((chunkIdx >> 8) & 0xFF);
  buf[20] = static_cast<uint8_t>(offset & 0xFF);
  buf[21] = static_cast<uint8_t>((offset >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>((offset >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((offset >> 24) & 0xFF);
  buf[24] = static_cast<uint8_t>(len & 0xFF);
  buf[25] = static_cast<uint8_t>((len >> 8) & 0xFF);
  std::memcpy(&buf[FW_CHUNK_FIXED_SIZE], bytes, len);
  return total;
}

// MSG_FW_REQ (24 bytes):
//   hdr(6) + src(6) + tgt(6) + firstChunkIdx(2 LE) + chunkCount(2 LE) + reason(1) + reserved(1)
inline size_t buildFwReq(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6], const uint8_t targetMac[6],
                         uint16_t firstChunkIdx, uint16_t chunkCount,
                         FwReqReason reason,
                         uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                         uint8_t msgType = MSG_FW_REQ) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < FW_REQ_FIXED_SIZE) return 0;
  // chunkCount cap: 1..32 to prevent re-stream-all abuse per reconciliation doc.
  if (chunkCount == 0 || chunkCount > 32) return 0;
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(firstChunkIdx & 0xFF);
  buf[19] = static_cast<uint8_t>((firstChunkIdx >> 8) & 0xFF);
  buf[20] = static_cast<uint8_t>(chunkCount & 0xFF);
  buf[21] = static_cast<uint8_t>((chunkCount >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>(reason);
  buf[23] = 0;  // reserved
  return FW_REQ_FIXED_SIZE;
}

// MSG_FW_DONE (38 bytes):
//   hdr(6) + src(6) + tgt(6) + version(4 LE) + totalLen(4 LE)
//   + sha256Prefix(8) + footerLen(2 LE) + reserved(2)
inline size_t buildFwDone(uint8_t* buf, size_t bufLen, uint16_t seq,
                          const uint8_t sourceMac[6], const uint8_t targetMac[6],
                          uint32_t version, uint32_t totalLen,
                          const uint8_t sha256Prefix[FW_SHA256_PREFIX_LEN],
                          uint16_t footerLen,
                          uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                          uint8_t msgType = MSG_FW_DONE) {
  if (!buf || !sourceMac || !targetMac || !sha256Prefix) return 0;
  if (bufLen < FW_DONE_FIXED_SIZE) return 0;
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(version & 0xFF);
  buf[19] = static_cast<uint8_t>((version >> 8) & 0xFF);
  buf[20] = static_cast<uint8_t>((version >> 16) & 0xFF);
  buf[21] = static_cast<uint8_t>((version >> 24) & 0xFF);
  buf[22] = static_cast<uint8_t>(totalLen & 0xFF);
  buf[23] = static_cast<uint8_t>((totalLen >> 8) & 0xFF);
  buf[24] = static_cast<uint8_t>((totalLen >> 16) & 0xFF);
  buf[25] = static_cast<uint8_t>((totalLen >> 24) & 0xFF);
  std::memcpy(&buf[26], sha256Prefix, FW_SHA256_PREFIX_LEN);
  buf[34] = static_cast<uint8_t>(footerLen & 0xFF);
  buf[35] = static_cast<uint8_t>((footerLen >> 8) & 0xFF);
  buf[36] = 0;  // reserved
  buf[37] = 0;  // reserved
  return FW_DONE_FIXED_SIZE;
}

// MSG_FW_RESULT (24 bytes):
//   hdr(6) + src(6) + tgt(6) + status(1) + detail(1) + version(4 LE)
inline size_t buildFwResult(uint8_t* buf, size_t bufLen, uint16_t seq,
                            const uint8_t sourceMac[6], const uint8_t targetMac[6],
                            FwResultStatus status, uint8_t detail, uint32_t version,
                            uint8_t wireVersion = PROTOCOL_VERSION_EMIT,
                            uint8_t msgType = MSG_FW_RESULT) {
  if (!buf || !sourceMac || !targetMac) return 0;
  if (bufLen < FW_RESULT_FIXED_SIZE) return 0;
  detail::writeHeader(buf, msgType, seq, wireVersion);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  buf[18] = static_cast<uint8_t>(status);
  buf[19] = detail;
  buf[20] = static_cast<uint8_t>(version & 0xFF);
  buf[21] = static_cast<uint8_t>((version >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>((version >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((version >> 24) & 0xFF);
  return FW_RESULT_FIXED_SIZE;
}

// --- Parsers --------------------------------------------------------------

inline bool parseFwOffer(const uint8_t* data, size_t len, ParsedFwOffer& out,
                         uint8_t expectType = MSG_FW_OFFER) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_OFFER_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.version =
       static_cast<uint32_t>(data[FW_OFFER_OFF_VERSION    ])
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_VERSION + 1]) << 8)
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_VERSION + 2]) << 16)
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_VERSION + 3]) << 24);
  out.totalLen =
       static_cast<uint32_t>(data[FW_OFFER_OFF_TOTAL_LEN    ])
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_TOTAL_LEN + 1]) << 8)
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_TOTAL_LEN + 2]) << 16)
     | (static_cast<uint32_t>(data[FW_OFFER_OFF_TOTAL_LEN + 3]) << 24);
  out.chunkSize =
       static_cast<uint16_t>(data[FW_OFFER_OFF_CHUNK_SIZE    ])
     | (static_cast<uint16_t>(data[FW_OFFER_OFF_CHUNK_SIZE + 1]) << 8);
  std::memcpy(out.channel, &data[FW_OFFER_OFF_CHANNEL], FW_CHANNEL_LEN);
  out.channel[FW_CHANNEL_LEN] = '\0';
  std::memcpy(out.sha256Prefix, &data[FW_OFFER_OFF_SHA256], FW_SHA256_PREFIX_LEN);
  out.footerLen =
       static_cast<uint16_t>(data[FW_OFFER_OFF_FOOTER_LEN    ])
     | (static_cast<uint16_t>(data[FW_OFFER_OFF_FOOTER_LEN + 1]) << 8);
  out.totalChunks =
       static_cast<uint16_t>(data[FW_OFFER_OFF_TOTAL_CHUNKS    ])
     | (static_cast<uint16_t>(data[FW_OFFER_OFF_TOTAL_CHUNKS + 1]) << 8);
  return true;
}

inline bool parseFwAccept(const uint8_t* data, size_t len, ParsedFwAccept& out,
                          uint8_t expectType = MSG_FW_ACCEPT) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_ACCEPT_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.offerSeq =
       static_cast<uint16_t>(data[18])
     | (static_cast<uint16_t>(data[19]) << 8);
  out.version =
       static_cast<uint32_t>(data[20])
     | (static_cast<uint32_t>(data[21]) << 8)
     | (static_cast<uint32_t>(data[22]) << 16)
     | (static_cast<uint32_t>(data[23]) << 24);
  out.status = static_cast<FwAcceptStatus>(data[24]);
  // resumeOffset is reserved-zero in v1 (the 3 trailing body bytes are
  // reserved placeholders). Forward-compat: bytes 25..27 may carry a future
  // 24-bit resumeOffset; for now they're zero on emit and ignored on parse.
  out.resumeOffset = 0;
  return true;
}

inline bool parseFwChunk(const uint8_t* data, size_t len, ParsedFwChunk& out,
                         uint8_t expectType = MSG_FW_CHUNK) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_CHUNK_FIXED_SIZE) return false;
  const uint16_t payloadLen =
       static_cast<uint16_t>(data[24])
     | (static_cast<uint16_t>(data[25]) << 8);
  if (payloadLen == 0 || payloadLen > FW_CHUNK_SIZE) return false;
  if (len != FW_CHUNK_FIXED_SIZE + static_cast<size_t>(payloadLen)) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.chunkIdx =
       static_cast<uint16_t>(data[18])
     | (static_cast<uint16_t>(data[19]) << 8);
  out.offset =
       static_cast<uint32_t>(data[20])
     | (static_cast<uint32_t>(data[21]) << 8)
     | (static_cast<uint32_t>(data[22]) << 16)
     | (static_cast<uint32_t>(data[23]) << 24);
  out.len = payloadLen;
  // Invariant: offset == chunkIdx * FW_CHUNK_SIZE on every chunk (catches
  // malformed senders that disagree with themselves). Last chunk may be
  // shorter than FW_CHUNK_SIZE but its offset still aligns to the grid.
  if (out.offset !=
      static_cast<uint32_t>(out.chunkIdx) * static_cast<uint32_t>(FW_CHUNK_SIZE)) {
    return false;
  }
  out.bytes = &data[FW_CHUNK_FIXED_SIZE];
  return true;
}

inline bool parseFwReq(const uint8_t* data, size_t len, ParsedFwReq& out,
                       uint8_t expectType = MSG_FW_REQ) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_REQ_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.firstChunkIdx =
       static_cast<uint16_t>(data[18])
     | (static_cast<uint16_t>(data[19]) << 8);
  out.chunkCount =
       static_cast<uint16_t>(data[20])
     | (static_cast<uint16_t>(data[21]) << 8);
  if (out.chunkCount == 0 || out.chunkCount > 32) return false;
  out.reason = static_cast<FwReqReason>(data[22]);
  return true;
}

inline bool parseFwDone(const uint8_t* data, size_t len, ParsedFwDone& out,
                        uint8_t expectType = MSG_FW_DONE) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_DONE_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.version =
       static_cast<uint32_t>(data[18])
     | (static_cast<uint32_t>(data[19]) << 8)
     | (static_cast<uint32_t>(data[20]) << 16)
     | (static_cast<uint32_t>(data[21]) << 24);
  out.totalLen =
       static_cast<uint32_t>(data[22])
     | (static_cast<uint32_t>(data[23]) << 8)
     | (static_cast<uint32_t>(data[24]) << 16)
     | (static_cast<uint32_t>(data[25]) << 24);
  std::memcpy(out.sha256Prefix, &data[26], FW_SHA256_PREFIX_LEN);
  out.footerLen =
       static_cast<uint16_t>(data[34])
     | (static_cast<uint16_t>(data[35]) << 8);
  return true;
}

inline bool parseFwResult(const uint8_t* data, size_t len, ParsedFwResult& out,
                          uint8_t expectType = MSG_FW_RESULT) {
  if (inspect(data, len) != expectType) return false;
  if (len < FW_RESULT_FIXED_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.status = static_cast<FwResultStatus>(data[18]);
  out.detail = data[19];
  out.version =
       static_cast<uint32_t>(data[20])
     | (static_cast<uint32_t>(data[21]) << 8)
     | (static_cast<uint32_t>(data[22]) << 16)
     | (static_cast<uint32_t>(data[23]) << 24);
  return true;
}

// =============================================================================
// End MSG_FW_* family
// =============================================================================

// Gossip dedup: small fixed-size ring tracking (sourceMac, msgType, seq) tuples
// seen recently. Drops duplicates so re-broadcasts terminate.
//
// Concurrency: record() can be called from BOTH the ESP-NOW recv task
// (Core 0, via ShowReceiver::handleRecv) AND the Arduino loop task
// (Core 1, via ShowReceiver::sendControlOp recording our own sent ops
// so the inbound re-broadcast doesn't loop back). The critical section
// is the compare loop + slot write — kept SHORT: no allocations, no
// network calls, no logging. See audit finding #7 / Stability #3.
class DedupRing {
 public:
  // v0x03 mesh-deploy lock-in: bumped from 32 to 64. At 20-50 lamps each
  // gossiping every unique (sourceMac, seq), the 32-slot ring wrapped
  // fast enough that a late-arriving relayed copy could re-fire a
  // receiver — specifically a problem now that MSG_EVENT itself gains
  // gossip-relay (Commit E). 64 slots give sufficient headroom: at 50
  // lamps emitting one cascade each within a small window, we still hold
  // ~24 unique (mac, seq) entries past the ring's age horizon. Per-msgType
  // dedup (ShowReceiver has separate rings per message type) means EVENT
  // entries never get evicted by HELLO traffic etc.
  // why: scale-fix per validated plan §"Layer 2".
  static constexpr size_t CAPACITY = 64;

  // Returns true if (mac, msgType, seq) is new (and records it); false if seen.
  bool record(const uint8_t mac[6], uint8_t msgType, uint16_t seq) {
    LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
    for (size_t i = 0; i < CAPACITY; i++) {
      const Entry& e = entries_[i];
      if (e.used && e.msgType == msgType && e.seq == seq &&
          std::memcmp(e.mac, mac, 6) == 0) {
        LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
        return false;
      }
    }
    Entry& slot = entries_[head_];
    slot.used = true;
    slot.msgType = msgType;
    slot.seq = seq;
    std::memcpy(slot.mac, mac, 6);
    head_ = (head_ + 1) % CAPACITY;
    LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
    return true;
  }

 private:
  struct Entry {
    bool used = false;
    uint8_t msgType = 0;
    uint16_t seq = 0;
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
  };
  Entry entries_[CAPACITY];
  size_t head_ = 0;
  LAMP_PROTOCOL_PORTMUX_TYPE mux_ = LAMP_PROTOCOL_PORTMUX_INIT;
};

}  // namespace lamp_protocol

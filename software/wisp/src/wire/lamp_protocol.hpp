#pragma once

// Shared wire format for wisp <-> lamp over ESP-NOW broadcast.
//
// The lamp firmware carries additional types (MSG_FW_* OTA distribution,
// etc.) that the wisp doesn't send or parse. The two PlatformIO projects
// target different boards/frameworks, so the shared types are duplicated
// rather than vendored as a library. Whenever you touch a type that
// appears on both sides, update both copies.


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
// Split into EMIT + RX_MAX to support cross-version fleet migrations.
// See docs/dev/networking.md for the upgrade workflow.
constexpr uint8_t PROTOCOL_VERSION_EMIT   = 0x05;
constexpr uint8_t PROTOCOL_VERSION_RX_MIN = 0x04;
constexpr uint8_t PROTOCOL_VERSION_RX_MAX = 0x05;

// Alias for callers that only need the emit version.
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
  // for every lamp this wisp currently claims, at the RSSI the wisp
  // hears that lamp. Gossip-relayed by lamps the same way MSG_WISP_HELLO
  // is, so the shared claim view propagates across the mesh regardless
  // of whether two wisps can directly hear each other. Lamps don't
  // otherwise act on this message — it's purely wisp-coordination.
  MSG_WISP_CLAIM          = 0x25,
  // Wisp's manualPalette broadcast. Carries up to kMaxWispPaletteColors
  // RGB triples packed binary. Lamps cache the latest, gossip-relay once
  // per (mac, seq), and serve it back to apps inside the wispStatus BLE
  // characteristic JSON as a base64 blob. The wisp is the single source
  // of truth for the palette so all apps see the same value regardless of
  // which lamp they're connected to. Cadence: piggybacked on the 30 s
  // emitStatus() tick, plus an on-change emit from WispOpDispatcher.
  MSG_WISP_PALETTE        = 0x26,
  // Per-lamp paint colors broadcast by the wisp. Each entry carries the lamp's
  // current base + shade RGB so the app preview reflects drift, not just the
  // deterministic newcomer prediction.
  MSG_WISP_PAINT          = 0x27,
  MSG_EVENT               = 0x30,
};

// High bit on msgType is not masked by inspect(); any frame that sets it
// surfaces as an unrecognised type.
constexpr uint8_t kReservedMsgTypeHighBit = 0x80;

// Single-source-of-truth caps.
constexpr size_t kMaxOverrideColorsPerFrame = 8;   // ESP-NOW 250-byte cap math
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

constexpr size_t HEADER_SIZE = 6;
// 6 (header: magic+ver+type+seq) + 6 (sourceMac) + 4 (shade RGBW) +
// 4 (base RGBW) + 4 (firmwareVersion LE) = 24 bytes. Both lamp and
// wisp must run the same value here.
constexpr size_t HELLO_FIXED_SIZE = 24;
constexpr size_t HELLO_MAX_NAME = 32;
// HELLO TLV trailer (v0x05+). After the variable name field, the frame
// carries:
//   [tlv_count 1 byte] [type 1][len 1][value len bytes] x tlv_count
// Each TLV's len byte limits its value to 255 bytes. Parsers skip
// unknown types by advancing 2 + len, so future TLV types can land
// without bumping PROTOCOL_VERSION.
constexpr uint8_t HELLO_TLV_OTA_STATE = 0x01;  // value: 1 byte, 0=idle 1=sending 2=receiving
// value: 16 bytes, the lamp's `{type}-{channel}` identity (zero-padded).
// Lamp-side only carries the OTA gate that consumes this; the wisp mirrors
// the parse so the two copies stay byte-compatible.
constexpr uint8_t HELLO_TLV_FW_CHANNEL = 0x02;
constexpr size_t  HELLO_FW_CHANNEL_LEN = 16;  // == FW_CHANNEL_LEN

// Compact OTA-state enum carried in HELLO_TLV_OTA_STATE.
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
// needing another PROTOCOL_VERSION bump.
constexpr size_t WISP_HELLO_MAX_SIZE              = 96;

// MSG_WISP_CLAIM: header(6) + sourceMac(6) + count(1) + entries[count*7].
// Each entry: lampMac(6) + signed int8 rssi(1) = 7 bytes.
// ESP-NOW frame cap 250 bytes; (250 - 13) / 7 = 33 entries max. We cap
// at 32 to align with LampInventory::MAX_LAMPS — a wisp can never have
// more entries to advertise than its inventory holds anyway.
constexpr size_t WISP_CLAIM_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_CLAIM_ENTRY_SIZE   = 6 + 1;                // 7
constexpr size_t kMaxWispClaimEntries    = 32;
constexpr size_t WISP_CLAIM_MAX_SIZE     = WISP_CLAIM_FIXED_PREFIX +
                                            kMaxWispClaimEntries *
                                            WISP_CLAIM_ENTRY_SIZE;  // 237

// MSG_WISP_PALETTE: header(6) + sourceMac(6) + count(1) + rgb[count*3].
// Each entry is 3 bytes (R, G, B) — no W channel; the wisp's manualPalette
// is RGB-only per WispConfig::ManualPaletteColor. Cap kMaxWispPaletteColors
// at 50 to keep the frame well under the 250-byte ESP-NOW limit: 13 + 50*3
// = 163 bytes. Aurora palettes can be larger than 50; the wisp truncates
// when emitting and logs once on truncation so an operator notices the
// shape mismatch.
constexpr size_t WISP_PALETTE_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PALETTE_ENTRY_SIZE   = 3;                    // R, G, B
constexpr size_t kMaxWispPaletteColors     = 50;
constexpr size_t WISP_PALETTE_MAX_SIZE     = WISP_PALETTE_FIXED_PREFIX +
                                              kMaxWispPaletteColors *
                                              WISP_PALETTE_ENTRY_SIZE;  // 163

// MSG_WISP_PAINT: header(6) + sourceMac(6) + count(1) + entries[count * 12].
// Each entry: lampMac(6) + baseRGB(3) + shadeRGB(3) = 12 bytes.
// Cap at 18 entries: 13 + 18*12 = 229 B, within the 250-byte ESP-NOW frame.
constexpr size_t WISP_PAINT_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PAINT_ENTRY_SIZE   = 6 + 3 + 3;            // 12
constexpr size_t WISP_PAINT_MAX_ENTRIES  = 18;
constexpr size_t WISP_PAINT_MAX_SIZE     = WISP_PAINT_FIXED_PREFIX +
                                            WISP_PAINT_MAX_ENTRIES *
                                            WISP_PAINT_ENTRY_SIZE;  // 229
static_assert(WISP_PAINT_MAX_SIZE <= 250, "MSG_WISP_PAINT exceeds ESP-NOW frame cap");

// MSG_OVERRIDE_COLORS fixed prefix:
//   header(6) + sourceMac(6) + targetMac(6) + surface(1) + sourceKind(1)
//   + fadeDurationMs(4) + numColors(1)
// = 25 bytes; colors[numColors * 4] follow. Min numColors=1 → 29 total.
// fadeDurationMs u32 LE; range up to ~1hr (3,600,000 ms) for wisp color-drift fades.
constexpr size_t OVERRIDE_COLORS_FIXED_SIZE = HEADER_SIZE + 6 + 6 + 1 + 1 + 4 + 1;  // 25
constexpr size_t OVERRIDE_COLORS_MAX_SIZE   = OVERRIDE_COLORS_FIXED_SIZE +
                                              kMaxOverrideColorsPerFrame * 4;       // 57

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

// MAX_PACKET_SIZE: receiver buffer sizing. CONTROL_MAX_SIZE (250) is the
// largest frame; HELLO_MAX_SIZE and the override family are smaller.
constexpr size_t MAX_PACKET_SIZE =
    CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE;

struct ParsedHello {
  uint16_t seq;
  uint8_t sourceMac[6];
  uint8_t shade[4];
  uint8_t base[4];
  // Semver packed (major<<16)|(minor<<8)|patch.
  uint32_t firmwareVersion;
  uint8_t nameLen;
  char name[HELLO_MAX_NAME + 1];  // null-terminated copy
  // TLV-derived fields. Defaults apply when the corresponding TLV is
  // absent from the frame.
  uint8_t otaState = kOtaStateIdle;  // HELLO_TLV_OTA_STATE
  // HELLO_TLV_FW_CHANNEL — the peer's `{type}-{channel}`. Empty when absent.
  char fwChannel[HELLO_FW_CHANNEL_LEN + 1] = {0};
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
  // Not null-terminated on wire; +1 for a parser-written '\0'.
  char     paletteIdPrefix[WISP_HELLO_PALETTE_ID_PREFIX_LEN + 1];
  char     carriedFwChannel[WISP_HELLO_FW_CHANNEL_LEN + 1];
  uint32_t carriedFwVersion;
};

struct ParsedWispClaim {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointers into the recv buffer; caller must not retain past this call.
  // Each entry is (lampMac[6], int8 rssi). The arrays are parallel,
  // both of length `count`. We surface them as raw byte spans to keep
  // the parser branchless.
  const uint8_t* entries;  // count * WISP_CLAIM_ENTRY_SIZE bytes
};

struct ParsedWispPalette {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointer into the recv buffer; caller must not retain past this call.
  // `count * 3` bytes of packed R, G, B.
  const uint8_t* rgb;
};

struct ParsedWispPaint {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointer into the recv buffer; caller must not retain past this call.
  // `count * WISP_PAINT_ENTRY_SIZE` bytes of packed lampMac(6)+baseRGB(3)+shadeRGB(3).
  const uint8_t* entries;
};

struct ParsedOverrideColors {
  uint16_t        seq;
  uint8_t         sourceMac[6];
  uint8_t         targetMac[6];
  OverrideSurface surface;
  OverrideSource  sourceKind;
  uint32_t        fadeDurationMs;
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

namespace detail {

inline bool isValidOverrideSurfaceByte(uint8_t b) {
  return b == static_cast<uint8_t>(OverrideSurface::Base) ||
         b == static_cast<uint8_t>(OverrideSurface::Shade) ||
         b == static_cast<uint8_t>(OverrideSurface::BaseAndShade);
}

// 0x00 None, 0x01 Wisp, 0xFF Any, 0x10..0xFE user-defined.
// Reject 0x02..0x0F (reserved) explicitly. 0xFF maps to Any.
inline bool isValidOverrideSourceByte(uint8_t b) {
  if (b == static_cast<uint8_t>(OverrideSource::None)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Wisp)) return true;
  if (b == static_cast<uint8_t>(OverrideSource::Any)) return true;
  if (b >= 0x10 && b <= 0xFE) return true;
  return false;  // 0x02..0x0F reserved
}

// Write the 6-byte header (magic + version + msgType + seq LE) to `buf`.
inline void writeHeader(uint8_t* buf, uint8_t msgType, uint16_t seq) {
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = PROTOCOL_VERSION;
  buf[3] = msgType;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
}

}  // namespace detail

// Build a HELLO frame into `buf`. `name` is utf-8, NOT null-terminated on the wire.
// `nameLen` clamped to HELLO_MAX_NAME. `firmwareVersion` is the sender's packed
// semver (see version.hpp). Returns 0 on bad args, total bytes written on success.
inline size_t buildHello(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         const uint8_t shadeRGBW[4], const uint8_t baseRGBW[4],
                         uint32_t firmwareVersion,
                         const char* name, size_t nameLen,
                         uint8_t otaState = kOtaStateIdle,
                         const char* fwChannel = nullptr) {
  if (!buf || !sourceMac || !shadeRGBW || !baseRGBW) return 0;
  if (nameLen > HELLO_MAX_NAME) nameLen = HELLO_MAX_NAME;
  const bool emitOtaState  = (otaState != kOtaStateIdle);
  const bool emitFwChannel = (fwChannel != nullptr && fwChannel[0] != '\0');
  const size_t tlvBytes = 1 + (emitOtaState ? 3 : 0) +
                          (emitFwChannel ? (2 + HELLO_FW_CHANNEL_LEN) : 0);
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
  buf[20] = static_cast<uint8_t>(firmwareVersion & 0xFF);
  buf[21] = static_cast<uint8_t>((firmwareVersion >> 8) & 0xFF);
  buf[22] = static_cast<uint8_t>((firmwareVersion >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((firmwareVersion >> 24) & 0xFF);
  buf[24] = static_cast<uint8_t>(nameLen);
  if (nameLen && name) std::memcpy(&buf[25], name, nameLen);
  // TLV trailer.
  size_t off = HELLO_FIXED_SIZE + 1 + nameLen;
  buf[off++] = static_cast<uint8_t>((emitOtaState ? 1 : 0) +
                                    (emitFwChannel ? 1 : 0));  // tlv_count
  if (emitOtaState) {
    buf[off++] = HELLO_TLV_OTA_STATE;
    buf[off++] = 1;
    buf[off++] = otaState;
  }
  if (emitFwChannel) {
    buf[off++] = HELLO_TLV_FW_CHANNEL;
    buf[off++] = static_cast<uint8_t>(HELLO_FW_CHANNEL_LEN);
    std::memset(&buf[off], 0, HELLO_FW_CHANNEL_LEN);
    for (size_t n = 0; fwChannel[n] != '\0' && n < HELLO_FW_CHANNEL_LEN; ++n) {
      buf[off + n] = static_cast<uint8_t>(fwChannel[n]);
    }
    off += HELLO_FW_CHANNEL_LEN;
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
  // tlv_count = 0; no TLVs defined for WISP_HELLO.
  if (bufLen < WISP_HELLO_FIXED_SIZE + 1) return 0;
  buf[WISP_HELLO_FIXED_SIZE] = 0;  // tlv_count
  return WISP_HELLO_FIXED_SIZE + 1;
}

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

// Build a MSG_WISP_PAINT frame. `entries` is `count` packed records, each
// WISP_PAINT_ENTRY_SIZE bytes: lampMac(6) + baseRGB(3) + shadeRGB(3). `count`
// must be ≤ WISP_PAINT_MAX_ENTRIES. Returns total bytes written on success, 0
// on bad args / insufficient buffer / count overflow.
inline size_t buildWispPaint(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t sourceMac[6],
                             const uint8_t* entries,
                             uint8_t count) {
  if (!buf || !sourceMac) return 0;
  if (count > WISP_PAINT_MAX_ENTRIES) return 0;
  if (count > 0 && !entries) return 0;
  const size_t total = WISP_PAINT_FIXED_PREFIX + count * WISP_PAINT_ENTRY_SIZE;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_WISP_PAINT, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = count;
  if (count) {
    std::memcpy(&buf[WISP_PAINT_FIXED_PREFIX], entries,
                count * WISP_PAINT_ENTRY_SIZE);
  }
  return total;
}

// Build a MSG_OVERRIDE_COLORS frame. `numColors` must be 1..kMaxOverrideColorsPerFrame.
// `colorsRGBW` is numColors * 4 bytes. Returns total bytes written on success.
inline size_t buildOverrideColors(uint8_t* buf, size_t bufLen, uint16_t seq,
                                  const uint8_t sourceMac[6],
                                  const uint8_t targetMac[6],
                                  OverrideSurface surface,
                                  OverrideSource sourceKind,
                                  uint32_t fadeDurationMs,
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
  buf[22] = static_cast<uint8_t>((fadeDurationMs >> 16) & 0xFF);
  buf[23] = static_cast<uint8_t>((fadeDurationMs >> 24) & 0xFF);
  buf[24] = numColors;
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

// Validate magic + version. Returns the msg type byte verbatim or 0 if
// invalid.
inline uint8_t inspect(const uint8_t* data, size_t len) {
  if (!data || len < HEADER_SIZE) return 0;
  if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return 0;
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
  // TLV trailer walk (v0x05+).
  out.otaState = kOtaStateIdle;
  out.fwChannel[0] = '\0';
  size_t off = HELLO_FIXED_SIZE + 1 + nameLen;
  if (len <= off) return true;
  const uint8_t tlvCount = data[off++];
  for (uint8_t i = 0; i < tlvCount; ++i) {
    if (len < off + 2) return false;
    const uint8_t tlvType = data[off++];
    const uint8_t tlvLen  = data[off++];
    if (len < off + tlvLen) return false;
    if (tlvType == HELLO_TLV_OTA_STATE && tlvLen == 1) {
      out.otaState = data[off];
    } else if (tlvType == HELLO_TLV_FW_CHANNEL &&
               tlvLen == HELLO_FW_CHANNEL_LEN) {
      std::memcpy(out.fwChannel, &data[off], HELLO_FW_CHANNEL_LEN);
      out.fwChannel[HELLO_FW_CHANNEL_LEN] = '\0';
    }
    off += tlvLen;
  }
  return true;
}

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

inline bool parseWispPaint(const uint8_t* data, size_t len, ParsedWispPaint& out) {
  if (inspect(data, len) != MSG_WISP_PAINT) return false;
  if (len < WISP_PAINT_FIXED_PREFIX) return false;
  const uint8_t count = data[12];
  if (count > WISP_PAINT_MAX_ENTRIES) return false;
  const size_t expected = WISP_PAINT_FIXED_PREFIX +
                          static_cast<size_t>(count) * WISP_PAINT_ENTRY_SIZE;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.count = count;
  out.entries = count ? &data[WISP_PAINT_FIXED_PREFIX] : nullptr;
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
  // TLV trailer (v0x05+). No known WISP_HELLO TLV types; walk + skip.
  size_t off = WISP_HELLO_FIXED_SIZE;
  if (len <= off) return true;
  const uint8_t tlvCount = data[off++];
  for (uint8_t i = 0; i < tlvCount; ++i) {
    if (len < off + 2) return false;
    const uint8_t tlvLen = data[off + 1];
    if (len < off + 2 + tlvLen) return false;
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
  const uint8_t numColors = data[24];
  if (numColors < 1 || numColors > kMaxOverrideColorsPerFrame) return false;
  const size_t expected = OVERRIDE_COLORS_FIXED_SIZE + numColors * 4u;
  if (len != expected) return false;  // exact-match — silent drop on length mismatch
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.surface    = static_cast<OverrideSurface>(data[18]);
  out.sourceKind = static_cast<OverrideSource>(data[19]);
  out.fadeDurationMs =
       static_cast<uint32_t>(data[20])
     | (static_cast<uint32_t>(data[21]) << 8)
     | (static_cast<uint32_t>(data[22]) << 16)
     | (static_cast<uint32_t>(data[23]) << 24);
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

// Gossip dedup: small fixed-size ring tracking (sourceMac, msgType, seq) tuples
// seen recently. Drops duplicates so re-broadcasts terminate.
//
// Concurrency: record() is called from the WiFi recv task (via MeshRouter::onPacket).
// The critical section is the compare loop + slot write — kept SHORT: no
// allocations, no network calls, no logging.
class DedupRing {
 public:
  // 64 slots: in a dense crowd each gossiping a unique (sourceMac, seq), a
  // smaller ring wraps before a late-arriving relay lands and re-fires a
  // receiver. Per-msgType dedup (separate rings per message type)
  // prevents HELLO traffic from evicting command or control entries.
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

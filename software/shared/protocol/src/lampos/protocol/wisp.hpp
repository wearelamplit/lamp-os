#pragma once

#include <cstdint>
#include <cstring>

#include <lampos/protocol/header.hpp>

// The wisp coordination family: MSG_WISP_HELLO (0x20), MSG_WISP_CLAIM (0x25),
// MSG_WISP_PALETTE (0x26), MSG_WISP_PAINT (0x27), MSG_WISP_STATE (0x28).
// build/parse: buildWispHello/parseWispHello, buildWispClaim/parseWispClaim,
//              buildWispPalette/parseWispPalette,
//              buildWispState/parseWispState.
//
// MSG_WISP_HELLO (0x20), wisp presence beacon (WISP_HELLO_FIXED_SIZE == 45):
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    4    wispVersion (LE, packed semver)
//   16    1    flags (WISP_HELLO_FLAG_PAINT_MODE|WIFI_CONNECTED|SOURCE_LIVE)
//   17    8    paletteIdPrefix (WISP_HELLO_PALETTE_ID_PREFIX_LEN)
//   25   16    carriedFwChannel ({type}-{channel}, WISP_HELLO_FW_CHANNEL_LEN)
//   41    4    carriedFwVersion (LE)
//   45    …    TLV trailer (v0x05+): [tlv_count][type,len,value]… (wisp emits
//              tlv_count=0 today; parser skips unknowns). Cap WISP_HELLO_MAX_SIZE=96.
//
// MSG_WISP_CLAIM (0x25), wisp-to-wisp lamp-claim broadcast (WISP_CLAIM_MAX_SIZE==713):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ kMaxWispClaimEntries = 100)
//   13   7*n   entries: each WISP_CLAIM_ENTRY_SIZE = lampMac(6) + int8 rssi(1)
//              Fixed prefix WISP_CLAIM_FIXED_PREFIX = 13. The whole claim set
//              rides one v2 frame; no windowing.
//
// MSG_WISP_PALETTE (0x26), wisp manualPalette broadcast (MAX_SIZE == 213):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ kMaxWispPaletteColors = 50)
//   13   3*n   rgb: each WISP_PALETTE_ENTRY_SIZE = R,G,B
//   +    1*n   w plane (length-gated trailer; frames without it parse with
//              w == nullptr). Fixed prefix WISP_PALETTE_FIXED_PREFIX = 13.
//
// MSG_WISP_PAINT (0x27), per-lamp paint colors broadcast (MAX_SIZE == 1213):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ WISP_PAINT_MAX_ENTRIES = 100)
//   13  12*n   entries: each WISP_PAINT_ENTRY_SIZE = lampMac(6)+baseRGB(3)+shadeRGB(3)
//              Fixed prefix WISP_PAINT_FIXED_PREFIX = 13. The whole painted set
//              rides one v2 frame; no windowing.
//
// MSG_WISP_STATE (0x28), declarative per-lamp state broadcast (MAX_SIZE == 1461):
//    0    6    header
//    6    6    sourceMac
//   12    1    brightness (per-frame global)
//   13    4    driftRateMs (LE, per-frame global; matches WispConfig driftIntervalMs)
//   17    1    presenceFlags (per-frame global)
//   18    1    count (≤ WISP_STATE_MAX_ENTRIES = 103)
//   19  12*n   entries: each WISP_STATE_ENTRY_SIZE = lampMac(6)+baseRGB(3)+shadeRGB(3)
//   +    2*n   w plane: baseW,shadeW per entry, in entry order (length-gated
//              trailer; frames without it parse with w == nullptr, W = 0).
//              Fixed prefix WISP_STATE_FIXED_PREFIX = 19. The whole state rides
//              one v2 frame; no windowing, no relay (re-covers on its cadence).
//   +    …    TLV trailer, only after a W plane: [tlv_count][type,len,value]…
//              WISP_STATE_TLV_DRIFT_FADE (0x10): drift fade records, 11 B each;
//              WISP_STATE_TLV_DRIFT_FADE_PARTIAL (0x11, len 0): the records
//              cover only some fading entries. Parsers that stop at the W plane
//              ignore it.

namespace lamp_protocol {

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
// Set while the wisp's streamed palette source (OSC) is fresh.
constexpr uint8_t WISP_HELLO_FLAG_SOURCE_LIVE       = 0x04;
// WISP_HELLO TLV trailer (v0x05+). Same shape as HELLO's:
//   [tlv_count 1] [type 1][len 1][value len] x tlv_count
// Sized at 96 to hold the fixed fields plus a full TLV trailer without
// a PROTOCOL_VERSION bump. The wisp always sends tlv_count=0; the parser
// tolerates + skips unknowns identically to HELLO.
constexpr size_t WISP_HELLO_MAX_SIZE              = 96;
// TLV-type registry for WISP_HELLO. Shares its 1-byte type-space
// namespace with HELLO; types appear in separate frames so the same
// numeric value is unambiguous in context.

// MSG_WISP_CLAIM: header(6) + sourceMac(6) + count(1) + entries[count*7].
// Each entry: lampMac(6) + signed int8 rssi(1) = 7 bytes. One v2 frame carries
// the whole claim set (up to kMaxWispClaimEntries = LampInventory::MAX_LAMPS),
// so every lamp refreshes each tick and no sender-side windowing is needed.
constexpr size_t WISP_CLAIM_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_CLAIM_ENTRY_SIZE   = 6 + 1;                // 7
constexpr size_t kMaxWispClaimEntries    = 100;
// MSG_WISP_PALETTE: header(6) + sourceMac(6) + count(1) + rgb[count*3]
// + w[count]. The W plane rides after the RGB block so parsers that read
// only count*3 stay compatible (parseWispPalette is `len <` tolerant).
// Cap kMaxWispPaletteColors at 50 to keep the frame well under the
// 250-byte ESP-NOW limit: 13 + 50*3 + 50 = 213 bytes. The wisp truncates a
// larger palette when emitting and logs once so an operator notices.
constexpr size_t WISP_PALETTE_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PALETTE_ENTRY_SIZE   = 3;                    // R, G, B
constexpr size_t kMaxWispPaletteColors     = 50;
constexpr size_t WISP_PALETTE_MAX_SIZE     = WISP_PALETTE_FIXED_PREFIX +
                                              kMaxWispPaletteColors *
                                              (WISP_PALETTE_ENTRY_SIZE + 1);  // 213
constexpr size_t WISP_CLAIM_MAX_SIZE     = WISP_CLAIM_FIXED_PREFIX +
                                            kMaxWispClaimEntries *
                                            WISP_CLAIM_ENTRY_SIZE;  // 713
static_assert(WISP_CLAIM_MAX_SIZE <= ESPNOW_V2_FRAME_MAX,
              "MSG_WISP_CLAIM exceeds ESP-NOW v2 frame cap");

// MSG_WISP_PAINT: header(6) + sourceMac(6) + count(1) + entries[count * 12].
// Each entry: lampMac(6) + baseRGB(3) + shadeRGB(3) = 12 bytes. One v2 frame
// carries the whole painted set: 13 + 100*12 = 1213 B, under the v2 cap.
constexpr size_t WISP_PAINT_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PAINT_ENTRY_SIZE   = 6 + 3 + 3;            // 12
constexpr size_t WISP_PAINT_MAX_ENTRIES  = 100;
constexpr size_t WISP_PAINT_MAX_SIZE     = WISP_PAINT_FIXED_PREFIX +
                                            WISP_PAINT_MAX_ENTRIES *
                                            WISP_PAINT_ENTRY_SIZE;  // 1213
static_assert(WISP_PAINT_MAX_SIZE <= ESPNOW_V2_FRAME_MAX,
              "MSG_WISP_PAINT exceeds ESP-NOW v2 frame cap");

// MSG_WISP_STATE: header(6) + sourceMac(6) + brightness(1) + driftRateMs(4 LE)
// + presenceFlags(1) + count(1) + entries[count * 12] + w[count * 2].
// Brightness / drift-rate / presence are per-frame globals, not per-lamp. Each
// entry: lampMac(6) + baseRGB(3) + shadeRGB(3) = 12 bytes. The W plane rides
// after the entries so parsers that read only count*12 stay compatible. The
// entry cap is whatever fits one v2 frame with the W plane.
constexpr size_t WISP_STATE_ENTRY_SIZE   = 6 + 3 + 3;  // 12
constexpr size_t WISP_STATE_W_SIZE       = 2;          // baseW + shadeW
constexpr size_t WISP_STATE_FIXED_PREFIX = HEADER_SIZE + 6 + 1 + 4 + 1 + 1;  // 19
constexpr size_t WISP_STATE_MAX_ENTRIES  =
    (ESPNOW_V2_FRAME_MAX - WISP_STATE_FIXED_PREFIX) /
    (WISP_STATE_ENTRY_SIZE + WISP_STATE_W_SIZE);  // 103
constexpr size_t WISP_STATE_MAX_SIZE     = WISP_STATE_FIXED_PREFIX +
                                            WISP_STATE_MAX_ENTRIES *
                                            (WISP_STATE_ENTRY_SIZE +
                                             WISP_STATE_W_SIZE);  // 1461
static_assert(WISP_STATE_ENTRY_SIZE == 12, "WISP_STATE entry is mac(6)+base(3)+shade(3)");
static_assert(WISP_STATE_MAX_ENTRIES == 103,
              "WISP_STATE cap moved: update networking.md and every sizing of it");
static_assert(WISP_STATE_FIXED_PREFIX == 19,
              "WISP_STATE prefix is header(6)+mac(6)+brightness(1)+drift(4)+presence(1)+count(1)");
static_assert(WISP_STATE_MAX_SIZE <= ESPNOW_V2_FRAME_MAX,
              "MSG_WISP_STATE exceeds ESP-NOW v2 frame cap");

// MSG_WISP_STATE TLV trailer, HELLO-shaped. Its type ids sit outside HELLO's
// 0x01..0x07 so one 1-byte registry stays unambiguous across both.
// A DRIFT_FADE record: entryIndex(1, into this frame's entries) +
// targetBase RGBW(4) + targetShade RGBW(4) + leftDs(2 LE, time left in the
// fade, 100 ms units, rounded up). A TLV holds at most
// WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV records; more repeat the type. A frame
// with records and no PARTIAL mark carries one for every fading entry.
constexpr uint8_t WISP_STATE_TLV_DRIFT_FADE         = 0x10;
constexpr uint8_t WISP_STATE_TLV_DRIFT_FADE_PARTIAL = 0x11;
constexpr size_t  WISP_STATE_TLV_HEADER_SIZE        = 2;  // type + len
constexpr size_t  WISP_STATE_DRIFT_FADE_RECORD_SIZE = 1 + 4 + 4 + 2;  // 11
constexpr size_t  WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV =
    255 / WISP_STATE_DRIFT_FADE_RECORD_SIZE;  // 23
constexpr uint32_t WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS = 100;

constexpr size_t wispStateFrameSize(size_t count) {
  return WISP_STATE_FIXED_PREFIX +
         count * (WISP_STATE_ENTRY_SIZE + WISP_STATE_W_SIZE);
}

// Trailer bytes for `records` DRIFT_FADE records, plus the PARTIAL mark.
constexpr size_t wispStateDriftFadeTrailerSize(size_t records, bool partial) {
  return 1 +
         (records + WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV - 1) /
             WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV * WISP_STATE_TLV_HEADER_SIZE +
         records * WISP_STATE_DRIFT_FADE_RECORD_SIZE +
         (partial ? WISP_STATE_TLV_HEADER_SIZE : 0);
}

// Most records whose trailer fits `room` bytes.
constexpr size_t wispStateDriftFadeFit(size_t room, bool partial) {
  size_t k = 0;
  while (k < WISP_STATE_MAX_ENTRIES &&
         wispStateDriftFadeTrailerSize(k + 1, partial) <= room) {
    ++k;
  }
  return k;
}

// How many of `fading` records a STATE frame of `count` entries carries: all
// of them when they fit one frame, else as many as fit beside the PARTIAL
// mark, with `partial` set.
constexpr size_t wispStateDriftFadeRecordsToSend(size_t count, size_t fading,
                                                 bool& partial) {
  partial = false;
  if (fading == 0 || count > WISP_STATE_MAX_ENTRIES) return 0;
  const size_t room = ESPNOW_V2_FRAME_MAX - wispStateFrameSize(count);
  if (wispStateDriftFadeTrailerSize(fading, false) <= room) return fading;
  partial = true;
  const size_t k = wispStateDriftFadeFit(room, true);
  return k < fading ? k : fading;
}

// Most records any STATE frame carries, over every entry count.
constexpr size_t wispStateDriftFadeMaxRecords() {
  size_t best = 0;
  for (size_t n = 0; n <= WISP_STATE_MAX_ENTRIES; ++n) {
    const size_t fit =
        wispStateDriftFadeFit(ESPNOW_V2_FRAME_MAX - wispStateFrameSize(n), false);
    const size_t k = fit < n ? fit : n;
    if (k > best) best = k;
  }
  return best;
}
constexpr size_t WISP_STATE_DRIFT_FADE_MAX_RECORDS = wispStateDriftFadeMaxRecords();
static_assert(WISP_STATE_DRIFT_FADE_MAX_RECORDS == 57,
              "drift fade fit moved: update networking.md");

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
  // Pointers into the recv buffer; caller must not retain past this call.
  // `rgb` is `count * 3` bytes of packed R, G, B. `w` is `count` bytes,
  // nullptr when the frame carries no W plane.
  const uint8_t* rgb;
  const uint8_t* w;
};

struct ParsedWispState {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  brightness;
  uint32_t driftRateMs;
  uint8_t  presenceFlags;
  uint8_t  count;
  // Pointers into the recv buffer; caller must not retain past this call.
  // `entries` is `count * WISP_STATE_ENTRY_SIZE` bytes of packed
  // lampMac(6)+baseRGB(3)+shadeRGB(3). `w` is `count * WISP_STATE_W_SIZE`
  // bytes of baseW,shadeW in entry order, nullptr when the frame carries no
  // W plane (W reads as 0).
  const uint8_t* entries;
  const uint8_t* w;
  // The TLV trailer past the W plane, nullptr (and 0) when the frame has
  // none or no W plane.
  const uint8_t* trailer;
  size_t trailerLen;
};

// One lamp's drift fade from a STATE trailer.
struct WispDriftFade {
  uint8_t  base[4];   // target RGBW
  uint8_t  shade[4];  // target RGBW
  uint32_t leftMs;    // time left in the fade
};

enum class WispDriftFadeLookup : uint8_t {
  None,     // no record, and the frame does not mark itself partial
  Record,   // the entry's record, in `out`
  Partial,  // no record in a frame that carries only some
};

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
// `w` is `count` bytes appended as the W plane, or nullptr to omit it.
// Caller is responsible for capping count at kMaxWispPaletteColors. Returns
// total bytes written on success, 0 on bad args / insufficient buffer.
inline size_t buildWispPalette(uint8_t* buf, size_t bufLen, uint16_t seq,
                               const uint8_t sourceMac[6],
                               const uint8_t* rgb,
                               const uint8_t* w,
                               size_t count) {
  if (!buf || !sourceMac) return 0;
  if (count > kMaxWispPaletteColors) return 0;
  if (count > 0 && !rgb) return 0;
  const size_t rgbEnd =
      WISP_PALETTE_FIXED_PREFIX + count * WISP_PALETTE_ENTRY_SIZE;
  const size_t total = rgbEnd + (w ? count : 0);
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_WISP_PALETTE, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = static_cast<uint8_t>(count);
  if (count) {
    std::memcpy(&buf[WISP_PALETTE_FIXED_PREFIX], rgb,
                count * WISP_PALETTE_ENTRY_SIZE);
    if (w) std::memcpy(&buf[rgbEnd], w, count);
  }
  return total;
}

// Build a MSG_WISP_STATE frame. `entries` is `count` packed records, each
// WISP_STATE_ENTRY_SIZE bytes: lampMac(6) + baseRGB(3) + shadeRGB(3). `w` is
// `count * WISP_STATE_W_SIZE` bytes (baseW, shadeW per entry) appended as the
// W plane, or nullptr to omit it. `brightness`, `driftRateMs`, and
// `presenceFlags` are per-frame globals. `count` must be ≤
// WISP_STATE_MAX_ENTRIES. Returns total bytes written on success, 0 on bad
// args / insufficient buffer / count overflow.
inline size_t buildWispState(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t sourceMac[6],
                             uint8_t brightness,
                             uint32_t driftRateMs,
                             uint8_t presenceFlags,
                             const uint8_t* entries,
                             const uint8_t* w,
                             uint8_t count) {
  if (!buf || !sourceMac) return 0;
  if (count > WISP_STATE_MAX_ENTRIES) return 0;
  if (count > 0 && !entries) return 0;
  const size_t entriesEnd =
      WISP_STATE_FIXED_PREFIX + count * WISP_STATE_ENTRY_SIZE;
  const size_t total = entriesEnd + (w ? count * WISP_STATE_W_SIZE : 0);
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_WISP_STATE, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = brightness;
  buf[13] = static_cast<uint8_t>(driftRateMs & 0xFF);
  buf[14] = static_cast<uint8_t>((driftRateMs >> 8) & 0xFF);
  buf[15] = static_cast<uint8_t>((driftRateMs >> 16) & 0xFF);
  buf[16] = static_cast<uint8_t>((driftRateMs >> 24) & 0xFF);
  buf[17] = presenceFlags;
  buf[18] = count;
  if (count) {
    std::memcpy(&buf[WISP_STATE_FIXED_PREFIX], entries,
                count * WISP_STATE_ENTRY_SIZE);
    if (w) std::memcpy(&buf[entriesEnd], w, count * WISP_STATE_W_SIZE);
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
  out.w = (count && len >= expected + count) ? &data[expected] : nullptr;
  return true;
}

inline bool parseWispState(const uint8_t* data, size_t len, ParsedWispState& out) {
  if (inspect(data, len) != MSG_WISP_STATE) return false;
  if (len < WISP_STATE_FIXED_PREFIX) return false;
  const uint8_t count = data[18];
  if (count > WISP_STATE_MAX_ENTRIES) return false;
  const size_t expected = WISP_STATE_FIXED_PREFIX +
                          static_cast<size_t>(count) * WISP_STATE_ENTRY_SIZE;
  if (len < expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.brightness = data[12];
  out.driftRateMs =
       static_cast<uint32_t>(data[13])
     | (static_cast<uint32_t>(data[14]) << 8)
     | (static_cast<uint32_t>(data[15]) << 16)
     | (static_cast<uint32_t>(data[16]) << 24);
  out.presenceFlags = data[17];
  out.count = count;
  out.entries = count ? &data[WISP_STATE_FIXED_PREFIX] : nullptr;
  out.w = (count && len >= expected + count * WISP_STATE_W_SIZE)
              ? &data[expected]
              : nullptr;
  const size_t wEnd = expected + count * WISP_STATE_W_SIZE;
  out.trailer = (out.w && len > wEnd) ? &data[wEnd] : nullptr;
  out.trailerLen = out.trailer ? len - wEnd : 0;
  return true;
}

// Append the drift fade trailer to a STATE frame of `frameLen` bytes that ends
// in its W plane: `recordCount` packed records, and the PARTIAL mark when
// `partial`. Returns the new length; `frameLen` unchanged when there is
// nothing to add, the frame has no W plane, or the trailer would not fit
// `bufLen` or one v2 frame.
inline size_t appendWispStateDriftFade(uint8_t* buf, size_t bufLen,
                                       size_t frameLen, const uint8_t* records,
                                       size_t recordCount, bool partial) {
  if (!buf || frameLen < WISP_STATE_FIXED_PREFIX) return frameLen;
  if (recordCount == 0 && !partial) return frameLen;
  if (recordCount > 0 && !records) return frameLen;
  const uint8_t count = buf[18];
  if (count == 0 || frameLen != wispStateFrameSize(count)) return frameLen;
  const size_t total =
      frameLen + wispStateDriftFadeTrailerSize(recordCount, partial);
  if (total > bufLen || total > ESPNOW_V2_FRAME_MAX) return frameLen;
  uint8_t* p = &buf[frameLen];
  uint8_t* tlvCount = p++;
  *tlvCount = 0;
  for (size_t done = 0; done < recordCount;) {
    size_t n = recordCount - done;
    if (n > WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV) {
      n = WISP_STATE_DRIFT_FADE_RECORDS_PER_TLV;
    }
    *p++ = WISP_STATE_TLV_DRIFT_FADE;
    *p++ = static_cast<uint8_t>(n * WISP_STATE_DRIFT_FADE_RECORD_SIZE);
    std::memcpy(p, records + done * WISP_STATE_DRIFT_FADE_RECORD_SIZE,
                n * WISP_STATE_DRIFT_FADE_RECORD_SIZE);
    p += n * WISP_STATE_DRIFT_FADE_RECORD_SIZE;
    done += n;
    ++*tlvCount;
  }
  if (partial) {
    *p++ = WISP_STATE_TLV_DRIFT_FADE_PARTIAL;
    *p++ = 0;
    ++*tlvCount;
  }
  return total;
}

enum class WispDriftFadeTrailer : uint8_t {
  Malformed,  // a TLV overruns the trailer, or a record TLV's length is ragged
  Complete,   // a record for every fading entry
  Partial,    // the PARTIAL mark: records for only some fading entries
};

// Walk a STATE trailer's drift fade records in order, calling
// `onRecord(const uint8_t* record)` with each 11-byte record. Records before a
// malformed TLV are still visited; the result says whether to trust them.
template <typename OnRecord>
inline WispDriftFadeTrailer walkWispDriftFade(const uint8_t* trailer,
                                              size_t trailerLen,
                                              OnRecord onRecord) {
  if (!trailer || trailerLen < 1) return WispDriftFadeTrailer::Complete;
  const uint8_t tlvCount = trailer[0];
  size_t off = 1;
  bool partial = false;
  for (uint8_t i = 0; i < tlvCount; ++i) {
    if (off + WISP_STATE_TLV_HEADER_SIZE > trailerLen) {
      return WispDriftFadeTrailer::Malformed;
    }
    const uint8_t type = trailer[off];
    const uint8_t len = trailer[off + 1];
    off += WISP_STATE_TLV_HEADER_SIZE;
    if (off + len > trailerLen) return WispDriftFadeTrailer::Malformed;
    const uint8_t* v = &trailer[off];
    off += len;
    if (type == WISP_STATE_TLV_DRIFT_FADE_PARTIAL) {
      partial = true;
    } else if (type == WISP_STATE_TLV_DRIFT_FADE) {
      if (len % WISP_STATE_DRIFT_FADE_RECORD_SIZE != 0) {
        return WispDriftFadeTrailer::Malformed;
      }
      for (size_t r = 0; r < len; r += WISP_STATE_DRIFT_FADE_RECORD_SIZE) {
        onRecord(&v[r]);
      }
    }
  }
  return partial ? WispDriftFadeTrailer::Partial : WispDriftFadeTrailer::Complete;
}

// Find entry `entryIndex`'s drift fade in a STATE trailer. A malformed trailer
// reads None; the STATE itself still applies. `out` is written whenever the
// record is found, a None from a later malformed TLV included, and
// `outWritten`, when given, reports whether it was.
inline WispDriftFadeLookup lookupWispDriftFade(const uint8_t* trailer,
                                               size_t trailerLen,
                                               uint8_t entryIndex,
                                               WispDriftFade& out,
                                               bool* outWritten = nullptr) {
  if (outWritten) *outWritten = false;
  bool found = false;
  const WispDriftFadeTrailer form =
      walkWispDriftFade(trailer, trailerLen, [&](const uint8_t* r) {
        if (found || r[0] != entryIndex) return;
        std::memcpy(out.base, &r[1], 4);
        std::memcpy(out.shade, &r[5], 4);
        const uint32_t ds = static_cast<uint32_t>(r[9]) |
                            (static_cast<uint32_t>(r[10]) << 8);
        out.leftMs = ds * WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS;
        found = true;
        if (outWritten) *outWritten = true;
      });
  if (form == WispDriftFadeTrailer::Malformed) return WispDriftFadeLookup::None;
  if (found) return WispDriftFadeLookup::Record;
  return form == WispDriftFadeTrailer::Partial ? WispDriftFadeLookup::Partial
                                               : WispDriftFadeLookup::None;
}

// Locate this receiver's own entry inside a parsed MSG_WISP_STATE block.
// `entries` is `count` packed WISP_STATE_ENTRY_SIZE records; `mac` is the
// receiver's 6-byte mesh MAC. Returns a pointer to the matching 12-byte
// entry (lampMac(6)+baseRGB(3)+shadeRGB(3)), or nullptr when absent.
inline const uint8_t* findWispStateEntry(const uint8_t* entries, uint8_t count,
                                         const uint8_t mac[6]) {
  if (!entries || !mac) return nullptr;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* e = entries + static_cast<size_t>(i) * WISP_STATE_ENTRY_SIZE;
    if (std::memcmp(e, mac, 6) == 0) return e;
  }
  return nullptr;
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
  // TLV trailer (v0x05+). Tolerant of frames that omit it, the same as
  // parseHello, so a fixed-size frame without the trailer still parses. No
  // known WISP_HELLO TLV types; the loop walks and skips by length so future
  // additions don't need to change parseWispHello.
  size_t off = WISP_HELLO_FIXED_SIZE;
  if (len <= off) return true;
  const uint8_t tlvCount = data[off++];
  for (uint8_t i = 0; i < tlvCount; ++i) {
    if (len < off + 2) return false;
    const uint8_t tlvLen = data[off + 1];
    if (len < off + 2 + tlvLen) return false;
    // No WISP_HELLO TLV types route into ParsedWispHello; walk past
    // unknowns by length.
    off += 2 + tlvLen;
  }
  return true;
}

}  // namespace lamp_protocol

#pragma once

#include <cstdint>
#include <cstring>

#include <lampos/protocol/header.hpp>

// The wisp coordination family: MSG_WISP_HELLO (0x20), MSG_WISP_CLAIM (0x25),
// MSG_WISP_PALETTE (0x26), MSG_WISP_PAINT (0x27).
// build/parse: buildWispHello/parseWispHello, buildWispClaim/parseWispClaim,
//              buildWispPalette/parseWispPalette, buildWispPaint/parseWispPaint.
//
// MSG_WISP_HELLO (0x20), wisp presence beacon (WISP_HELLO_FIXED_SIZE == 45):
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    4    wispVersion (LE, packed semver)
//   16    1    flags (WISP_HELLO_FLAG_PAINT_MODE|WIFI_CONNECTED|AURORA_CONNECTED)
//   17    8    paletteIdPrefix (WISP_HELLO_PALETTE_ID_PREFIX_LEN)
//   25   16    carriedFwChannel ({type}-{channel}, WISP_HELLO_FW_CHANNEL_LEN)
//   41    4    carriedFwVersion (LE)
//   45    …    TLV trailer (v0x05+): [tlv_count][type,len,value]… (wisp emits
//              tlv_count=0 today; parser skips unknowns). Cap WISP_HELLO_MAX_SIZE=96.
//
// MSG_WISP_CLAIM (0x25), wisp-to-wisp lamp-claim broadcast (WISP_CLAIM_MAX_SIZE==237):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ kMaxWispClaimEntries = 32)
//   13   7*n   entries: each WISP_CLAIM_ENTRY_SIZE = lampMac(6) + int8 rssi(1)
//              Fixed prefix WISP_CLAIM_FIXED_PREFIX = 13.
//
// MSG_WISP_PALETTE (0x26), wisp manualPalette broadcast (MAX_SIZE == 213):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ kMaxWispPaletteColors = 50)
//   13   3*n   rgb: each WISP_PALETTE_ENTRY_SIZE = R,G,B
//   +    1*n   w plane (length-gated trailer; frames without it parse with
//              w == nullptr). Fixed prefix WISP_PALETTE_FIXED_PREFIX = 13.
//
// MSG_WISP_PAINT (0x27), per-lamp paint colors broadcast (MAX_SIZE == 229):
//    0    6    header
//    6    6    sourceMac
//   12    1    count (≤ WISP_PAINT_MAX_ENTRIES = 18)
//   13  12*n   entries: each WISP_PAINT_ENTRY_SIZE = lampMac(6)+baseRGB(3)+shadeRGB(3)
//              Fixed prefix WISP_PAINT_FIXED_PREFIX = 13.

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
constexpr uint8_t WISP_HELLO_FLAG_AURORA_CONNECTED  = 0x04;
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
// Each entry: lampMac(6) + signed int8 rssi(1) = 7 bytes.
// ESP-NOW frame cap 250 bytes; (250 - 13) / 7 = 33 entries max, capped at
// 32. A claim set larger than one frame rotates across frames sender-side
// (contested entries every frame); receivers accumulate.
constexpr size_t WISP_CLAIM_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_CLAIM_ENTRY_SIZE   = 6 + 1;                // 7
constexpr size_t kMaxWispClaimEntries    = 32;
// MSG_WISP_PALETTE: header(6) + sourceMac(6) + count(1) + rgb[count*3]
// + w[count]. The W plane rides after the RGB block so parsers that read
// only count*3 stay compatible (parseWispPalette is `len <` tolerant).
// Cap kMaxWispPaletteColors at 50 to keep the frame well under the
// 250-byte ESP-NOW limit: 13 + 50*3 + 50 = 213 bytes. Aurora palettes can
// be larger than 50; the wisp truncates when emitting and logs once on
// truncation so an operator notices the shape mismatch.
constexpr size_t WISP_PALETTE_FIXED_PREFIX = HEADER_SIZE + 6 + 1;  // 13
constexpr size_t WISP_PALETTE_ENTRY_SIZE   = 3;                    // R, G, B
constexpr size_t kMaxWispPaletteColors     = 50;
constexpr size_t WISP_PALETTE_MAX_SIZE     = WISP_PALETTE_FIXED_PREFIX +
                                              kMaxWispPaletteColors *
                                              (WISP_PALETTE_ENTRY_SIZE + 1);  // 213
constexpr size_t WISP_CLAIM_MAX_SIZE     = WISP_CLAIM_FIXED_PREFIX +
                                            kMaxWispClaimEntries *
                                            WISP_CLAIM_ENTRY_SIZE;  // 237

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

struct ParsedWispPaint {
  uint16_t seq;
  uint8_t  sourceMac[6];
  uint8_t  count;
  // Pointer into the recv buffer; caller must not retain past this call.
  // `count * WISP_PAINT_ENTRY_SIZE` bytes of packed lampMac(6)+baseRGB(3)+shadeRGB(3).
  const uint8_t* entries;
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

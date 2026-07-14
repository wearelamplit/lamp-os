#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"
#include "presence.hpp"

namespace lamp_protocol {

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
//
// build/parse entry points (parameterized by msgType, so the identical
// machinery serves both the FW_* and FS_* type IDs):
//   buildFwOffer/parseFwOffer   buildFwAccept/parseFwAccept
//   buildFwChunk/parseFwChunk   buildFwReq/parseFwReq
//   buildFwDone/parseFwDone     buildFwResult/parseFwResult
//
// FAMILY BYTE-MAPS. All share hdr(6)+src(6)+tgt(6) = bytes 0..17; the tables
// below give the body. Where a field has a named offset constant, the map
// references the NAME (not a literal) so it can't silently drift.
//
// MSG_FW_OFFER  (FW_OFFER_FIXED_SIZE == 56):
//   FW_OFFER_OFF_VERSION      4  version (LE)
//   FW_OFFER_OFF_TOTAL_LEN    4  totalLen (LE)
//   FW_OFFER_OFF_CHUNK_SIZE   2  chunkSize (LE; locked to FW_CHUNK_SIZE=200)
//   FW_OFFER_OFF_CHANNEL     16  channel {type}-{channel} (FW_CHANNEL_LEN, 0-pad)
//   FW_OFFER_OFF_SHA256       8  sha256Prefix (FW_SHA256_PREFIX_LEN)
//   FW_OFFER_OFF_FOOTER_LEN   2  footerLen (LE)
//   FW_OFFER_OFF_TOTAL_CHUNKS 2  totalChunks (LE)
//
// MSG_FW_ACCEPT (FW_ACCEPT_FIXED_SIZE == 28):
//   18  2  offerSeq (LE)   20  4  version (LE)   24  1  status (FwAcceptStatus)
//   25  3  reserved (resumeOffset parses to 0 in v1)
//
// MSG_FW_CHUNK  (FW_CHUNK_FIXED_SIZE == 26 + payload, MAX 226):
//   18  2  chunkIdx (LE)   20  4  offset (LE)   24  2  len (LE)
//   26  len  payload (offset must equal chunkIdx * FW_CHUNK_SIZE)
//
// MSG_FW_REQ    (FW_REQ_FIXED_SIZE == 24):
//   18  2  firstChunkIdx (LE)  20  2  chunkCount (LE, 1..32)  22  1  reason
//   23  1  reserved
//
// MSG_FW_DONE   (FW_DONE_FIXED_SIZE == 38):
//   18  4  version (LE)  22  4  totalLen (LE)  26  8  sha256Prefix
//   34  2  footerLen (LE)  36  2  reserved
//
// MSG_FW_RESULT (FW_RESULT_FIXED_SIZE == 24):
//   18  1  status (FwResultStatus)  19  1  detail  20  4  version (LE)
//
// MSG_FS_* (0x46..0x4B) reuse every layout above via the msgType parameter.

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

// RESULT status enum. uint8_t on the wire; values 9..255 reserved for forward-compat.
// The wisp treats unknown codes as "abort + log + back off".
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

// MSG_FW_OFFER (FW_OFFER_FIXED_SIZE == 56 bytes; see the family byte-map at
// the top of this header for the offset table — channel is 16 bytes, not the
// 8 an older draft of this comment claimed):
//   hdr(6) + src(6) + tgt(6) +
//   version(4 LE) + totalLen(4 LE) + chunkSize(2 LE) + channel(16 zero-pad)
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
  // resumeOffset is reserved-zero in v1; bytes 25..27 are zero on emit
  // and ignored on parse (forward-compat slot for a 24-bit resume field).
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

}  // namespace lamp_protocol

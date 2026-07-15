#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"

// =============================================================================
// presence.hpp — MSG_HELLO (0x01), the lamp presence beacon.
// build: buildHello, parse: parseHello.
// =============================================================================
//
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    4    shade RGBW
//   16    4    base RGBW
//   20    4    firmwareVersion (LE, packed semver)
//   24    1    nameLen
//   25    N    name (utf-8, nameLen bytes, NOT null-terminated on wire)
//  25+N   …    TLV trailer (v0x05+): [tlv_count 1][type 1][len 1][value len]…
//              Known types: HELLO_TLV_OTA_STATE (0x01, 1B),
//              HELLO_TLV_FW_CHANNEL (0x02, HELLO_FW_CHANNEL_LEN=16B),
//              HELLO_TLV_FS_STATE (0x03, HELLO_FS_DIGEST_LEN=8B).
//              Unknown types are skipped by their len byte (forward-compat).
//
// Fixed prefix through nameLen is HELLO_FIXED_SIZE (24) + 1; the whole frame
// is capped at HELLO_MAX_SIZE (128).

namespace lamp_protocol {

// HELLO fixed prefix: header(6) + sourceMac(6) + shade(4) + base(4) + firmwareVersion(4).
// Name length byte + name bytes follow this prefix.
// 6 + 6 + 4 + 4 + 4 = 24 bytes. Both lamp and wisp must match — protocol is verbatim-mirrored.
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
// older lamps that predate FS OTA → distributor treats absent as "don't offer
// FS" (such a peer can't receive it anyway).
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

struct ParsedHello {
  uint16_t seq;
  uint8_t sourceMac[6];
  uint8_t shade[4];
  uint8_t base[4];
  // Semver packed (major<<16)|(minor<<8)|patch. Zero when the TLV is absent.
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

}  // namespace lamp_protocol

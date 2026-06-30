#pragma once

// OTA wire-format subset: the MSG_FW_* family (0x40..0x45) and MSG_HELLO (0x01).
// Byte layouts must stay byte-for-byte identical to the sender's, so a sender and
// this receiver interoperate without re-framing. Protocol byte at buf[2] is 0x05.

#include <cstdint>
#include <cstring>

namespace catch_ota {

constexpr uint8_t MAGIC_0 = 'L';
constexpr uint8_t MAGIC_1 = 'M';

// Hardcoded emit + receive version. catch_ota speaks only v0x05.
constexpr uint8_t PROTOCOL_VERSION = 0x05;

constexpr uint8_t MSG_HELLO    = 0x01;
constexpr uint8_t MSG_FW_OFFER = 0x40;
constexpr uint8_t MSG_FW_ACCEPT = 0x41;
constexpr uint8_t MSG_FW_CHUNK  = 0x42;
constexpr uint8_t MSG_FW_REQ    = 0x43;
constexpr uint8_t MSG_FW_DONE   = 0x44;
constexpr uint8_t MSG_FW_RESULT = 0x45;

// HELLO wire constants (fixed prefix identical to dev).
constexpr size_t HEADER_SIZE      = 6;
constexpr size_t HELLO_FIXED_SIZE = 24;  // header(6)+mac(6)+shade(4)+base(4)+fwVer(4)
constexpr size_t HELLO_MAX_NAME   = 32;
constexpr size_t HELLO_MAX_SIZE   = 128;

constexpr uint8_t HELLO_TLV_OTA_STATE  = 0x01;
constexpr uint8_t HELLO_TLV_FW_CHANNEL = 0x02;
constexpr size_t  HELLO_FW_CHANNEL_LEN = 16;

constexpr uint8_t kOtaStateIdle      = 0;
constexpr uint8_t kOtaStateSending   = 1;
constexpr uint8_t kOtaStateReceiving = 2;

// FW-family constants (identical to dev).
constexpr size_t   FW_CHANNEL_LEN       = 16;
constexpr size_t   FW_SHA256_PREFIX_LEN = 8;
constexpr uint16_t FW_CHUNK_SIZE        = 200;

constexpr size_t   FW_OFFER_FIXED_SIZE  = 56;
constexpr size_t   FW_ACCEPT_FIXED_SIZE = 28;
constexpr size_t   FW_CHUNK_FIXED_SIZE  = 26;
constexpr size_t   FW_CHUNK_MAX_SIZE    = FW_CHUNK_FIXED_SIZE + FW_CHUNK_SIZE;  // 226
constexpr size_t   FW_REQ_FIXED_SIZE    = 24;
constexpr size_t   FW_DONE_FIXED_SIZE   = 38;
constexpr size_t   FW_RESULT_FIXED_SIZE = 24;

static_assert(FW_OFFER_FIXED_SIZE  == 56, "FW OFFER size lock");
static_assert(FW_ACCEPT_FIXED_SIZE == 28, "FW ACCEPT size lock");
static_assert(FW_CHUNK_FIXED_SIZE  == 26, "FW CHUNK header lock");
static_assert(FW_REQ_FIXED_SIZE    == 24, "FW REQ size lock");
static_assert(FW_DONE_FIXED_SIZE   == 38, "FW DONE size lock");
static_assert(FW_RESULT_FIXED_SIZE == 24, "FW RESULT size lock");
static_assert(FW_CHUNK_MAX_SIZE    <= 250, "ESP-NOW frame cap");
static_assert(FW_OFFER_FIXED_SIZE  <= 250, "ESP-NOW frame cap");

// OFFER body field offsets (named to prevent silent shifts on refactor).
constexpr size_t FW_OFFER_OFF_VERSION      = 18;
constexpr size_t FW_OFFER_OFF_TOTAL_LEN    = 22;
constexpr size_t FW_OFFER_OFF_CHUNK_SIZE   = 26;
constexpr size_t FW_OFFER_OFF_CHANNEL      = 28;
constexpr size_t FW_OFFER_OFF_SHA256       = FW_OFFER_OFF_CHANNEL + FW_CHANNEL_LEN;       // 44
constexpr size_t FW_OFFER_OFF_FOOTER_LEN   = FW_OFFER_OFF_SHA256 + FW_SHA256_PREFIX_LEN;  // 52
constexpr size_t FW_OFFER_OFF_TOTAL_CHUNKS = FW_OFFER_OFF_FOOTER_LEN + 2;                 // 54
static_assert(FW_OFFER_OFF_TOTAL_CHUNKS + 2 == FW_OFFER_FIXED_SIZE,
              "FW OFFER field layout must end at FW_OFFER_FIXED_SIZE");

// ACCEPT status byte.
enum class FwAcceptStatus : uint8_t {
    Accept      = 0,
    DeclineBusy = 1,
};

// REQ reason.
enum class FwReqReason : uint8_t {
    Gap           = 0,
    StallWatchdog = 1,
};

// RESULT status.
enum class FwResultStatus : uint8_t {
    Success            = 0,
    SignatureFail      = 1,
    VersionMismatch    = 2,
    PartitionWriteFail = 3,
    OtaBeginFail       = 5,
    OtaEndFail         = 6,
    SetBootFail        = 7,
    OfferShaMismatch   = 8,
};

// Parsed structs

struct ParsedHello {
    uint16_t seq;
    uint8_t  sourceMac[6];
    uint8_t  shade[4];
    uint8_t  base[4];
    uint32_t firmwareVersion;
    uint8_t  nameLen;
    char     name[HELLO_MAX_NAME + 1];
    uint8_t  otaState = kOtaStateIdle;
    char     fwChannel[HELLO_FW_CHANNEL_LEN + 1] = {0};
};

struct ParsedFwOffer {
    uint16_t seq;
    uint8_t  sourceMac[6];
    uint8_t  targetMac[6];
    uint32_t version;
    uint32_t totalLen;
    uint16_t chunkSize;
    char     channel[FW_CHANNEL_LEN + 1];
    uint8_t  sha256Prefix[FW_SHA256_PREFIX_LEN];
    uint16_t footerLen;
    uint16_t totalChunks;
};

struct ParsedFwChunk {
    uint16_t       seq;
    uint8_t        sourceMac[6];
    uint8_t        targetMac[6];
    uint16_t       chunkIdx;
    uint32_t       offset;
    uint16_t       len;
    const uint8_t* bytes;
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

// Little-endian field accessors. Every multi-byte wire field is LE.
inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}
inline void writeU16LE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}
inline void writeU32LE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v & 0xFF);
    p[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

namespace detail {
void writeHeader(uint8_t* buf, uint8_t msgType, uint16_t seq);
}  // namespace detail

// inspect(), buildHello() and parseHello() stay inline here: the HELLO unit test
// compiles hello_emitter.cpp without linking ota_protocol.cpp, so it resolves
// them through this header. The FW builders/parsers below live in the .cpp.

// inspect(): validate magic + version byte, return msgType or 0 on failure.
inline uint8_t inspect(const uint8_t* data, size_t len) {
    if (!data || len < HEADER_SIZE) return 0;
    if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return 0;
    if (data[2] != PROTOCOL_VERSION) return 0;
    return data[3];
}

// Builders

// buildHello: emits a v0x05 HELLO with tlv_count=0 when otaState=kOtaStateIdle
// and fwChannel=nullptr (the catch_ota use case).
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
    writeU16LE(&buf[4], seq);
    std::memcpy(&buf[6], sourceMac, 6);
    std::memcpy(&buf[12], shadeRGBW, 4);
    std::memcpy(&buf[16], baseRGBW, 4);
    writeU32LE(&buf[20], firmwareVersion);
    buf[24] = static_cast<uint8_t>(nameLen);
    if (nameLen && name) std::memcpy(&buf[25], name, nameLen);
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

// MSG_FW_OFFER (56 bytes):
//   hdr(6)+src(6)+tgt(6)+version(4)+totalLen(4)+chunkSize(2)+channel(16)+sha256(8)+footerLen(2)+totalChunks(2)
size_t buildFwOffer(uint8_t* buf, size_t bufLen, uint16_t seq,
                    const uint8_t sourceMac[6], const uint8_t targetMac[6],
                    uint32_t version, uint32_t totalLen, uint16_t chunkSize,
                    const char* channel, size_t channelLen,
                    const uint8_t sha256Prefix[FW_SHA256_PREFIX_LEN],
                    uint16_t footerLen, uint16_t totalChunks);

// MSG_FW_ACCEPT (28 bytes):
//   hdr(6)+src(6)+tgt(6)+offerSeq(2)+version(4)+status(1)+reserved(3)
size_t buildFwAccept(uint8_t* buf, size_t bufLen, uint16_t seq,
                     const uint8_t sourceMac[6], const uint8_t targetMac[6],
                     uint16_t offerSeq, uint32_t version,
                     FwAcceptStatus status, uint32_t resumeOffset);

// MSG_FW_REQ (24 bytes):
//   hdr(6)+src(6)+tgt(6)+firstChunkIdx(2)+chunkCount(2)+reason(1)+reserved(1)
size_t buildFwReq(uint8_t* buf, size_t bufLen, uint16_t seq,
                  const uint8_t sourceMac[6], const uint8_t targetMac[6],
                  uint16_t firstChunkIdx, uint16_t chunkCount,
                  FwReqReason reason);

// MSG_FW_RESULT (24 bytes):
//   hdr(6)+src(6)+tgt(6)+status(1)+detail(1)+version(4)
size_t buildFwResult(uint8_t* buf, size_t bufLen, uint16_t seq,
                     const uint8_t sourceMac[6], const uint8_t targetMac[6],
                     FwResultStatus status, uint8_t detail, uint32_t version);

// Parsers

inline bool parseHello(const uint8_t* data, size_t len, ParsedHello& out) {
    if (inspect(data, len) != MSG_HELLO) return false;
    if (len < HELLO_FIXED_SIZE + 1) return false;
    out.seq = readU16LE(&data[4]);
    std::memcpy(out.sourceMac, &data[6], 6);
    std::memcpy(out.shade, &data[12], 4);
    std::memcpy(out.base, &data[16], 4);
    out.firmwareVersion = readU32LE(&data[20]);
    const uint8_t rawLen  = data[24];
    const uint8_t nameLen = rawLen > HELLO_MAX_NAME
                                ? static_cast<uint8_t>(HELLO_MAX_NAME)
                                : rawLen;
    if (len < static_cast<size_t>(HELLO_FIXED_SIZE + 1 + nameLen)) return false;
    out.nameLen = nameLen;
    if (nameLen) std::memcpy(out.name, &data[25], nameLen);
    out.name[nameLen] = '\0';
    out.otaState   = kOtaStateIdle;
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

bool parseFwOffer(const uint8_t* data, size_t len, ParsedFwOffer& out);

bool parseFwChunk(const uint8_t* data, size_t len, ParsedFwChunk& out);

bool parseFwDone(const uint8_t* data, size_t len, ParsedFwDone& out);

}  // namespace catch_ota

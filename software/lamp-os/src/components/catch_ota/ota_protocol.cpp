#include "ota_protocol.hpp"

namespace catch_ota {

namespace detail {
void writeHeader(uint8_t* buf, uint8_t msgType, uint16_t seq) {
    buf[0] = MAGIC_0;
    buf[1] = MAGIC_1;
    buf[2] = PROTOCOL_VERSION;
    buf[3] = msgType;
    writeU16LE(&buf[4], seq);
}
}  // namespace detail

size_t buildFwOffer(uint8_t* buf, size_t bufLen, uint16_t seq,
                    const uint8_t sourceMac[6], const uint8_t targetMac[6],
                    uint32_t version, uint32_t totalLen, uint16_t chunkSize,
                    const char* channel, size_t channelLen,
                    const uint8_t sha256Prefix[FW_SHA256_PREFIX_LEN],
                    uint16_t footerLen, uint16_t totalChunks) {
    if (!buf || !sourceMac || !targetMac || !sha256Prefix) return 0;
    if (bufLen < FW_OFFER_FIXED_SIZE) return 0;
    detail::writeHeader(buf, MSG_FW_OFFER, seq);
    std::memcpy(&buf[6], sourceMac, 6);
    std::memcpy(&buf[12], targetMac, 6);
    writeU32LE(&buf[FW_OFFER_OFF_VERSION], version);
    writeU32LE(&buf[FW_OFFER_OFF_TOTAL_LEN], totalLen);
    writeU16LE(&buf[FW_OFFER_OFF_CHUNK_SIZE], chunkSize);
    std::memset(&buf[FW_OFFER_OFF_CHANNEL], 0, FW_CHANNEL_LEN);
    if (channel && channelLen) {
        const size_t n = channelLen > FW_CHANNEL_LEN ? FW_CHANNEL_LEN : channelLen;
        std::memcpy(&buf[FW_OFFER_OFF_CHANNEL], channel, n);
    }
    std::memcpy(&buf[FW_OFFER_OFF_SHA256], sha256Prefix, FW_SHA256_PREFIX_LEN);
    writeU16LE(&buf[FW_OFFER_OFF_FOOTER_LEN], footerLen);
    writeU16LE(&buf[FW_OFFER_OFF_TOTAL_CHUNKS], totalChunks);
    return FW_OFFER_FIXED_SIZE;
}

size_t buildFwAccept(uint8_t* buf, size_t bufLen, uint16_t seq,
                     const uint8_t sourceMac[6], const uint8_t targetMac[6],
                     uint16_t offerSeq, uint32_t version,
                     FwAcceptStatus status, uint32_t /*resumeOffset*/) {
    if (!buf || !sourceMac || !targetMac) return 0;
    if (bufLen < FW_ACCEPT_FIXED_SIZE) return 0;
    detail::writeHeader(buf, MSG_FW_ACCEPT, seq);
    std::memcpy(&buf[6], sourceMac, 6);
    std::memcpy(&buf[12], targetMac, 6);
    writeU16LE(&buf[18], offerSeq);
    writeU32LE(&buf[20], version);
    buf[24] = static_cast<uint8_t>(status);
    buf[25] = 0;
    buf[26] = 0;
    buf[27] = 0;
    return FW_ACCEPT_FIXED_SIZE;
}

size_t buildFwReq(uint8_t* buf, size_t bufLen, uint16_t seq,
                  const uint8_t sourceMac[6], const uint8_t targetMac[6],
                  uint16_t firstChunkIdx, uint16_t chunkCount,
                  FwReqReason reason) {
    if (!buf || !sourceMac || !targetMac) return 0;
    if (bufLen < FW_REQ_FIXED_SIZE) return 0;
    if (chunkCount == 0 || chunkCount > 32) return 0;
    detail::writeHeader(buf, MSG_FW_REQ, seq);
    std::memcpy(&buf[6], sourceMac, 6);
    std::memcpy(&buf[12], targetMac, 6);
    writeU16LE(&buf[18], firstChunkIdx);
    writeU16LE(&buf[20], chunkCount);
    buf[22] = static_cast<uint8_t>(reason);
    buf[23] = 0;
    return FW_REQ_FIXED_SIZE;
}

size_t buildFwResult(uint8_t* buf, size_t bufLen, uint16_t seq,
                     const uint8_t sourceMac[6], const uint8_t targetMac[6],
                     FwResultStatus status, uint8_t detail, uint32_t version) {
    if (!buf || !sourceMac || !targetMac) return 0;
    if (bufLen < FW_RESULT_FIXED_SIZE) return 0;
    detail::writeHeader(buf, MSG_FW_RESULT, seq);
    std::memcpy(&buf[6], sourceMac, 6);
    std::memcpy(&buf[12], targetMac, 6);
    buf[18] = static_cast<uint8_t>(status);
    buf[19] = detail;
    writeU32LE(&buf[20], version);
    return FW_RESULT_FIXED_SIZE;
}

bool parseFwOffer(const uint8_t* data, size_t len, ParsedFwOffer& out) {
    if (inspect(data, len) != MSG_FW_OFFER) return false;
    if (len < FW_OFFER_FIXED_SIZE) return false;
    out.seq      = readU16LE(&data[4]);
    std::memcpy(out.sourceMac, &data[6], 6);
    std::memcpy(out.targetMac, &data[12], 6);
    out.version   = readU32LE(&data[FW_OFFER_OFF_VERSION]);
    out.totalLen  = readU32LE(&data[FW_OFFER_OFF_TOTAL_LEN]);
    out.chunkSize = readU16LE(&data[FW_OFFER_OFF_CHUNK_SIZE]);
    std::memcpy(out.channel, &data[FW_OFFER_OFF_CHANNEL], FW_CHANNEL_LEN);
    out.channel[FW_CHANNEL_LEN] = '\0';
    std::memcpy(out.sha256Prefix, &data[FW_OFFER_OFF_SHA256], FW_SHA256_PREFIX_LEN);
    out.footerLen   = readU16LE(&data[FW_OFFER_OFF_FOOTER_LEN]);
    out.totalChunks = readU16LE(&data[FW_OFFER_OFF_TOTAL_CHUNKS]);
    return true;
}

bool parseFwChunk(const uint8_t* data, size_t len, ParsedFwChunk& out) {
    if (inspect(data, len) != MSG_FW_CHUNK) return false;
    if (len < FW_CHUNK_FIXED_SIZE) return false;
    const uint16_t payloadLen = readU16LE(&data[24]);
    if (payloadLen == 0 || payloadLen > FW_CHUNK_SIZE) return false;
    if (len != FW_CHUNK_FIXED_SIZE + static_cast<size_t>(payloadLen)) return false;
    out.seq = readU16LE(&data[4]);
    std::memcpy(out.sourceMac, &data[6], 6);
    std::memcpy(out.targetMac, &data[12], 6);
    out.chunkIdx = readU16LE(&data[18]);
    out.offset   = readU32LE(&data[20]);
    out.len = payloadLen;
    if (out.offset !=
        static_cast<uint32_t>(out.chunkIdx) * static_cast<uint32_t>(FW_CHUNK_SIZE)) {
        return false;
    }
    out.bytes = &data[FW_CHUNK_FIXED_SIZE];
    return true;
}

bool parseFwDone(const uint8_t* data, size_t len, ParsedFwDone& out) {
    if (inspect(data, len) != MSG_FW_DONE) return false;
    if (len < FW_DONE_FIXED_SIZE) return false;
    out.seq      = readU16LE(&data[4]);
    std::memcpy(out.sourceMac, &data[6], 6);
    std::memcpy(out.targetMac, &data[12], 6);
    out.version  = readU32LE(&data[18]);
    out.totalLen = readU32LE(&data[22]);
    std::memcpy(out.sha256Prefix, &data[26], FW_SHA256_PREFIX_LEN);
    out.footerLen = readU16LE(&data[34]);
    return true;
}

}  // namespace catch_ota

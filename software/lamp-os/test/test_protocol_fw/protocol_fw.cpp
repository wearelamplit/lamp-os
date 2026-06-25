// Native-host round-trip tests for the Phase F MSG_FW_* additions:
// MSG_FW_OFFER, MSG_FW_ACCEPT, MSG_FW_CHUNK, MSG_FW_REQ, MSG_FW_DONE,
// MSG_FW_RESULT (0x40..0x45).
//
// Same pattern as test_protocol_v2 — pin the wire format so a refactor
// of the header can't silently shift byte offsets or drop a field. We
// include the production header directly; it's self-contained and
// provides a no-op portMUX shim when neither ARDUINO nor ESP_PLATFORM
// is defined.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>

#include "components/network/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

// Lock-in pins so any future protocol shrink/refactor that drifts these
// values fails to compile rather than silently going on the wire with
// wrong sizes.
static_assert(lp::FW_OFFER_FIXED_SIZE  == 56, "FW OFFER size pin");
static_assert(lp::FW_ACCEPT_FIXED_SIZE == 28, "FW ACCEPT size pin");
static_assert(lp::FW_CHUNK_FIXED_SIZE  == 26, "FW CHUNK header pin");
static_assert(lp::FW_REQ_FIXED_SIZE    == 24, "FW REQ size pin");
static_assert(lp::FW_DONE_FIXED_SIZE   == 38, "FW DONE size pin");
static_assert(lp::FW_RESULT_FIXED_SIZE == 24, "FW RESULT size pin");
static_assert(lp::FW_CHUNK_MAX_SIZE   <= 250, "FW CHUNK max within ESP-NOW");
static_assert(lp::FW_OFFER_FIXED_SIZE <= 250, "FW OFFER max within ESP-NOW");
static_assert(lp::FW_CHUNK_SIZE       == 200, "FW chunk payload v1 lock");
static_assert(lp::FW_CHANNEL_LEN       ==  16, "FW channel slot lock (typed)");
static_assert(lp::FW_SHA256_PREFIX_LEN ==   8, "FW sha256 prefix lock");

static const uint8_t kWispMac[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kLampMac[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

// --- MSG_FW_OFFER ---

void test_fw_offer_roundtrip() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  const char channel[] = "standard-stable";   // 15 bytes; builder zero-pads to 16
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {
      0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE};
  const size_t n = lp::buildFwOffer(
      buf, sizeof(buf), /*seq=*/0x1234, kWispMac, kLampMac,
      /*version=*/0x00010203,
      /*totalLen=*/0x000F4240,    // 1,000,000 bytes
      /*chunkSize=*/lp::FW_CHUNK_SIZE,
      channel, 15,
      sha,
      /*footerLen=*/96,
      /*totalChunks=*/5000);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_OFFER_FIXED_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_OFFER, lp::inspect(buf, n));

  lp::ParsedFwOffer out;
  TEST_ASSERT_TRUE(lp::parseFwOffer(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x1234, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kWispMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLampMac, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT32(0x00010203u, out.version);
  TEST_ASSERT_EQUAL_UINT32(0x000F4240u, out.totalLen);
  TEST_ASSERT_EQUAL_UINT16(lp::FW_CHUNK_SIZE, out.chunkSize);
  TEST_ASSERT_EQUAL_STRING("standard-stable", out.channel);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sha, out.sha256Prefix, lp::FW_SHA256_PREFIX_LEN);
  TEST_ASSERT_EQUAL_UINT16(96, out.footerLen);
  TEST_ASSERT_EQUAL_UINT16(5000, out.totalChunks);
}

void test_fw_offer_full_channel_string() {
  // Exactly 16 bytes of channel — no terminator on the wire, but the parsed
  // struct gets a null-terminator appended at index 16.
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  const char channel[16] = {'a','b','c','d','e','f','g','h',
                            'i','j','k','l','m','n','o','p'};
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const size_t n = lp::buildFwOffer(buf, sizeof(buf), 1, kWispMac, kLampMac,
                                    0, 0, lp::FW_CHUNK_SIZE,
                                    channel, 16, sha, 96, 0);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_OFFER_FIXED_SIZE, n);
  lp::ParsedFwOffer out;
  TEST_ASSERT_TRUE(lp::parseFwOffer(buf, n, out));
  TEST_ASSERT_EQUAL_STRING("abcdefghijklmnop", out.channel);
  TEST_ASSERT_EQUAL_CHAR('\0', out.channel[16]);
}

void test_fw_offer_typed_channels_distinguishable() {
  // Two OFFERs with different {type}-{channel} strings must produce
  // distinct wire bytes in the channel slot — this is what makes the
  // existing channelMatchesOurs() silent-drop enforce type gating.
  uint8_t bufStd[lp::FW_OFFER_FIXED_SIZE];
  uint8_t bufSnf[lp::FW_OFFER_FIXED_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const char chStd[] = "standard-stable";
  const char chSnf[] = "snafu-stable";
  lp::buildFwOffer(bufStd, sizeof(bufStd), 1, kWispMac, kLampMac, 0, 0,
                   lp::FW_CHUNK_SIZE, chStd, 15, sha, 96, 0);
  lp::buildFwOffer(bufSnf, sizeof(bufSnf), 1, kWispMac, kLampMac, 0, 0,
                   lp::FW_CHUNK_SIZE, chSnf, 12, sha, 96, 0);
  lp::ParsedFwOffer outStd, outSnf;
  TEST_ASSERT_TRUE(lp::parseFwOffer(bufStd, sizeof(bufStd), outStd));
  TEST_ASSERT_TRUE(lp::parseFwOffer(bufSnf, sizeof(bufSnf), outSnf));
  TEST_ASSERT_EQUAL_STRING("standard-stable", outStd.channel);
  TEST_ASSERT_EQUAL_STRING("snafu-stable", outSnf.channel);
  // The wire bytes differ at the channel slot — gate enforces silent drop.
  TEST_ASSERT_TRUE(std::memcmp(outStd.channel, outSnf.channel,
                               lp::FW_CHANNEL_LEN) != 0);
}

void test_fw_offer_too_short_rejected() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  TEST_ASSERT_EQUAL_UINT32(lp::FW_OFFER_FIXED_SIZE,
      lp::buildFwOffer(buf, sizeof(buf), 1, kWispMac, kLampMac,
                       0, 0, lp::FW_CHUNK_SIZE, "x", 1, sha, 96, 0));
  lp::ParsedFwOffer out;
  TEST_ASSERT_FALSE(lp::parseFwOffer(buf, lp::FW_OFFER_FIXED_SIZE - 1, out));
}

// --- MSG_FW_ACCEPT ---

void test_fw_accept_roundtrip() {
  uint8_t buf[lp::FW_ACCEPT_FIXED_SIZE];
  const size_t n = lp::buildFwAccept(
      buf, sizeof(buf), /*seq=*/42, kLampMac, kWispMac,
      /*offerSeq=*/0x1234, /*version=*/0x01020304,
      lp::FwAcceptStatus::Accept, /*resumeOffset=*/0);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_ACCEPT_FIXED_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_ACCEPT, lp::inspect(buf, n));

  lp::ParsedFwAccept out;
  TEST_ASSERT_TRUE(lp::parseFwAccept(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(42, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLampMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kWispMac, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT16(0x1234, out.offerSeq);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, out.version);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(out.status));
  // resumeOffset is reserved-zero in v1 — always parses to 0 even if the
  // builder was passed a non-zero value (forward-compat).
  TEST_ASSERT_EQUAL_UINT32(0u, out.resumeOffset);
}

void test_fw_accept_status_codes() {
  uint8_t buf[lp::FW_ACCEPT_FIXED_SIZE];
  for (auto code : {lp::FwAcceptStatus::Accept,
                    lp::FwAcceptStatus::DeclineBusy,
                    lp::FwAcceptStatus::DeclineAlreadyCurrent}) {
    lp::buildFwAccept(buf, sizeof(buf), 1, kLampMac, kWispMac,
                      0, 0, code, 0);
    lp::ParsedFwAccept out;
    TEST_ASSERT_TRUE(lp::parseFwAccept(buf, lp::FW_ACCEPT_FIXED_SIZE, out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(code),
                            static_cast<uint8_t>(out.status));
  }
}

void test_fw_accept_too_short_rejected() {
  uint8_t buf[lp::FW_ACCEPT_FIXED_SIZE];
  lp::buildFwAccept(buf, sizeof(buf), 1, kLampMac, kWispMac, 0, 0,
                    lp::FwAcceptStatus::Accept, 0);
  lp::ParsedFwAccept out;
  TEST_ASSERT_FALSE(lp::parseFwAccept(buf, lp::FW_ACCEPT_FIXED_SIZE - 1, out));
}

// --- MSG_FW_CHUNK ---

void test_fw_chunk_roundtrip_min_payload() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const uint8_t payload[1] = {0xAB};
  const size_t n = lp::buildFwChunk(
      buf, sizeof(buf), /*seq=*/1, kWispMac, kLampMac,
      /*chunkIdx=*/0, /*offset=*/0, payload, /*len=*/1);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_CHUNK_FIXED_SIZE + 1, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_CHUNK, lp::inspect(buf, n));

  lp::ParsedFwChunk out;
  TEST_ASSERT_TRUE(lp::parseFwChunk(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0, out.chunkIdx);
  TEST_ASSERT_EQUAL_UINT32(0u, out.offset);
  TEST_ASSERT_EQUAL_UINT16(1, out.len);
  TEST_ASSERT_EQUAL_UINT8(0xAB, out.bytes[0]);
}

void test_fw_chunk_roundtrip_full_payload() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  uint8_t payload[lp::FW_CHUNK_SIZE];
  for (size_t i = 0; i < lp::FW_CHUNK_SIZE; ++i) {
    payload[i] = static_cast<uint8_t>(i ^ 0xA5);
  }
  // chunkIdx=37 → offset must equal 37 * 200 = 7400.
  const size_t n = lp::buildFwChunk(
      buf, sizeof(buf), /*seq=*/7, kWispMac, kLampMac,
      /*chunkIdx=*/37, /*offset=*/37u * lp::FW_CHUNK_SIZE,
      payload, /*len=*/lp::FW_CHUNK_SIZE);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_CHUNK_MAX_SIZE, n);
  TEST_ASSERT_EQUAL_UINT32(226u, n);

  lp::ParsedFwChunk out;
  TEST_ASSERT_TRUE(lp::parseFwChunk(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(37, out.chunkIdx);
  TEST_ASSERT_EQUAL_UINT32(7400u, out.offset);
  TEST_ASSERT_EQUAL_UINT16(lp::FW_CHUNK_SIZE, out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.bytes, lp::FW_CHUNK_SIZE);
}

void test_fw_chunk_zero_len_rejected_by_builder() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const uint8_t payload[1] = {0};
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwChunk(buf, sizeof(buf), 1, kWispMac, kLampMac,
                       0, 0, payload, 0));
}

void test_fw_chunk_oversize_rejected_by_builder() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE + 16];
  uint8_t payload[lp::FW_CHUNK_SIZE + 1] = {0};
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwChunk(buf, sizeof(buf), 1, kWispMac, kLampMac,
                       0, 0, payload, lp::FW_CHUNK_SIZE + 1));
}

void test_fw_chunk_offset_chunkidx_mismatch_rejected_by_parser() {
  // Build a valid chunk, then corrupt the offset field so the
  // (offset == chunkIdx * chunkSize) invariant fails. The parser must
  // reject — catches malformed senders that disagree with themselves.
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const uint8_t payload[lp::FW_CHUNK_SIZE] = {0};
  const size_t n = lp::buildFwChunk(buf, sizeof(buf), 1, kWispMac, kLampMac,
                                    /*chunkIdx=*/5,
                                    /*offset=*/5u * lp::FW_CHUNK_SIZE,
                                    payload, lp::FW_CHUNK_SIZE);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, n);
  // Sanity: as-built parses cleanly.
  lp::ParsedFwChunk ok;
  TEST_ASSERT_TRUE(lp::parseFwChunk(buf, n, ok));
  // Corrupt the LSB of offset (bytes 20..23).
  buf[20] = 0x01;
  lp::ParsedFwChunk bad;
  TEST_ASSERT_FALSE(lp::parseFwChunk(buf, n, bad));
}

void test_fw_chunk_length_mismatch_rejected_by_parser() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const uint8_t payload[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  const size_t n = lp::buildFwChunk(buf, sizeof(buf), 1, kWispMac, kLampMac,
                                    0, 0, payload, 10);
  lp::ParsedFwChunk out;
  // Truncated frame — exact length mismatch.
  TEST_ASSERT_FALSE(lp::parseFwChunk(buf, n - 1, out));
  // Buffer claims one more byte than the len field says — also rejected.
  TEST_ASSERT_FALSE(lp::parseFwChunk(buf, n + 1, out));
}

// --- MSG_FW_REQ ---

void test_fw_req_roundtrip() {
  uint8_t buf[lp::FW_REQ_FIXED_SIZE];
  const size_t n = lp::buildFwReq(
      buf, sizeof(buf), /*seq=*/9, kLampMac, kWispMac,
      /*firstChunkIdx=*/123, /*chunkCount=*/4,
      lp::FwReqReason::StallWatchdog);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_REQ_FIXED_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_REQ, lp::inspect(buf, n));

  lp::ParsedFwReq out;
  TEST_ASSERT_TRUE(lp::parseFwReq(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(9, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLampMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kWispMac, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT16(123, out.firstChunkIdx);
  TEST_ASSERT_EQUAL_UINT16(4, out.chunkCount);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lp::FwReqReason::StallWatchdog),
      static_cast<uint8_t>(out.reason));
}

void test_fw_req_chunk_count_zero_rejected_by_builder() {
  uint8_t buf[lp::FW_REQ_FIXED_SIZE];
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwReq(buf, sizeof(buf), 1, kLampMac, kWispMac,
                     0, 0, lp::FwReqReason::Gap));
}

void test_fw_req_chunk_count_over_cap_rejected_by_builder() {
  uint8_t buf[lp::FW_REQ_FIXED_SIZE];
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwReq(buf, sizeof(buf), 1, kLampMac, kWispMac,
                     0, 33, lp::FwReqReason::Gap));
}

void test_fw_req_too_short_rejected() {
  uint8_t buf[lp::FW_REQ_FIXED_SIZE];
  lp::buildFwReq(buf, sizeof(buf), 1, kLampMac, kWispMac, 0, 1,
                 lp::FwReqReason::Gap);
  lp::ParsedFwReq out;
  TEST_ASSERT_FALSE(lp::parseFwReq(buf, lp::FW_REQ_FIXED_SIZE - 1, out));
}

// --- MSG_FW_DONE ---

void test_fw_done_roundtrip() {
  uint8_t buf[lp::FW_DONE_FIXED_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {
      0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  const size_t n = lp::buildFwDone(
      buf, sizeof(buf), /*seq=*/0xBEEF, kWispMac, kLampMac,
      /*version=*/0x00010203, /*totalLen=*/0x12345678,
      sha, /*footerLen=*/96);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_DONE_FIXED_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_DONE, lp::inspect(buf, n));

  lp::ParsedFwDone out;
  TEST_ASSERT_TRUE(lp::parseFwDone(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kWispMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLampMac, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT32(0x00010203u, out.version);
  TEST_ASSERT_EQUAL_UINT32(0x12345678u, out.totalLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(sha, out.sha256Prefix, lp::FW_SHA256_PREFIX_LEN);
  TEST_ASSERT_EQUAL_UINT16(96, out.footerLen);
}

void test_fw_done_too_short_rejected() {
  uint8_t buf[lp::FW_DONE_FIXED_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  lp::buildFwDone(buf, sizeof(buf), 1, kWispMac, kLampMac, 0, 0, sha, 96);
  lp::ParsedFwDone out;
  TEST_ASSERT_FALSE(lp::parseFwDone(buf, lp::FW_DONE_FIXED_SIZE - 1, out));
}

// --- MSG_FW_RESULT ---

void test_fw_result_roundtrip() {
  uint8_t buf[lp::FW_RESULT_FIXED_SIZE];
  const size_t n = lp::buildFwResult(
      buf, sizeof(buf), /*seq=*/0xC0DE, kLampMac, kWispMac,
      lp::FwResultStatus::PartitionWriteFail,
      /*detail=*/0x42, /*version=*/0x00010203);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_RESULT_FIXED_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_RESULT, lp::inspect(buf, n));

  lp::ParsedFwResult out;
  TEST_ASSERT_TRUE(lp::parseFwResult(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0xC0DE, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kLampMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kWispMac, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::FwResultStatus::PartitionWriteFail),
                          static_cast<uint8_t>(out.status));
  TEST_ASSERT_EQUAL_UINT8(0x42, out.detail);
  TEST_ASSERT_EQUAL_UINT32(0x00010203u, out.version);
}

void test_fw_result_status_codes_roundtrip() {
  uint8_t buf[lp::FW_RESULT_FIXED_SIZE];
  for (auto code : {lp::FwResultStatus::Success,
                    lp::FwResultStatus::SignatureFail,
                    lp::FwResultStatus::VersionMismatch,
                    lp::FwResultStatus::PartitionWriteFail,
                    lp::FwResultStatus::PartitionReadFail,
                    lp::FwResultStatus::OtaBeginFail,
                    lp::FwResultStatus::OtaEndFail,
                    lp::FwResultStatus::SetBootFail,
                    lp::FwResultStatus::OfferShaMismatch}) {
    lp::buildFwResult(buf, sizeof(buf), 1, kLampMac, kWispMac, code, 0, 0);
    lp::ParsedFwResult out;
    TEST_ASSERT_TRUE(lp::parseFwResult(buf, lp::FW_RESULT_FIXED_SIZE, out));
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(code),
                            static_cast<uint8_t>(out.status));
  }
}

void test_fw_result_too_short_rejected() {
  uint8_t buf[lp::FW_RESULT_FIXED_SIZE];
  lp::buildFwResult(buf, sizeof(buf), 1, kLampMac, kWispMac,
                    lp::FwResultStatus::Success, 0, 0);
  lp::ParsedFwResult out;
  TEST_ASSERT_FALSE(lp::parseFwResult(buf, lp::FW_RESULT_FIXED_SIZE - 1, out));
}

// --- inspect() returns the right msgType for each ---

void test_fw_inspect_msg_types() {
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const uint8_t payload[1] = {0};

  size_t n = lp::buildFwOffer(buf, sizeof(buf), 1, kWispMac, kLampMac,
                              0, 0, lp::FW_CHUNK_SIZE, "x", 1, sha, 96, 0);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_OFFER, lp::inspect(buf, n));

  n = lp::buildFwAccept(buf, sizeof(buf), 1, kLampMac, kWispMac, 0, 0,
                        lp::FwAcceptStatus::Accept, 0);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_ACCEPT, lp::inspect(buf, n));

  n = lp::buildFwChunk(buf, sizeof(buf), 1, kWispMac, kLampMac, 0, 0,
                       payload, 1);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_CHUNK, lp::inspect(buf, n));

  n = lp::buildFwReq(buf, sizeof(buf), 1, kLampMac, kWispMac, 0, 1,
                     lp::FwReqReason::Gap);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_REQ, lp::inspect(buf, n));

  n = lp::buildFwDone(buf, sizeof(buf), 1, kWispMac, kLampMac, 0, 0, sha, 96);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_DONE, lp::inspect(buf, n));

  n = lp::buildFwResult(buf, sizeof(buf), 1, kLampMac, kWispMac,
                        lp::FwResultStatus::Success, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_RESULT, lp::inspect(buf, n));
}

// --- Builder rejects too-small buffer ---

void test_fw_builders_reject_short_buffer() {
  uint8_t small[1];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const uint8_t payload[1] = {0};

  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwOffer(small, sizeof(small), 1, kWispMac, kLampMac,
                       0, 0, lp::FW_CHUNK_SIZE, "x", 1, sha, 96, 0));
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwAccept(small, sizeof(small), 1, kLampMac, kWispMac,
                        0, 0, lp::FwAcceptStatus::Accept, 0));
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwChunk(small, sizeof(small), 1, kWispMac, kLampMac,
                       0, 0, payload, 1));
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwReq(small, sizeof(small), 1, kLampMac, kWispMac,
                     0, 1, lp::FwReqReason::Gap));
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwDone(small, sizeof(small), 1, kWispMac, kLampMac,
                      0, 0, sha, 96));
  TEST_ASSERT_EQUAL_UINT32(0u,
      lp::buildFwResult(small, sizeof(small), 1, kLampMac, kWispMac,
                        lp::FwResultStatus::Success, 0, 0));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_fw_offer_roundtrip);
  RUN_TEST(test_fw_offer_full_channel_string);
  RUN_TEST(test_fw_offer_typed_channels_distinguishable);
  RUN_TEST(test_fw_offer_too_short_rejected);

  RUN_TEST(test_fw_accept_roundtrip);
  RUN_TEST(test_fw_accept_status_codes);
  RUN_TEST(test_fw_accept_too_short_rejected);

  RUN_TEST(test_fw_chunk_roundtrip_min_payload);
  RUN_TEST(test_fw_chunk_roundtrip_full_payload);
  RUN_TEST(test_fw_chunk_zero_len_rejected_by_builder);
  RUN_TEST(test_fw_chunk_oversize_rejected_by_builder);
  RUN_TEST(test_fw_chunk_offset_chunkidx_mismatch_rejected_by_parser);
  RUN_TEST(test_fw_chunk_length_mismatch_rejected_by_parser);

  RUN_TEST(test_fw_req_roundtrip);
  RUN_TEST(test_fw_req_chunk_count_zero_rejected_by_builder);
  RUN_TEST(test_fw_req_chunk_count_over_cap_rejected_by_builder);
  RUN_TEST(test_fw_req_too_short_rejected);

  RUN_TEST(test_fw_done_roundtrip);
  RUN_TEST(test_fw_done_too_short_rejected);

  RUN_TEST(test_fw_result_roundtrip);
  RUN_TEST(test_fw_result_status_codes_roundtrip);
  RUN_TEST(test_fw_result_too_short_rejected);

  RUN_TEST(test_fw_inspect_msg_types);
  RUN_TEST(test_fw_builders_reject_short_buffer);

  return UNITY_END();
}

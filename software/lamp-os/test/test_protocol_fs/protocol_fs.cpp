// FS-image OTA frames reuse the FW builders/parsers with the MSG_FS_* msgType.
// These tests pin: (1) FS frames round-trip when built + parsed with the FS
// type, (2) type isolation — an FS frame won't parse as FW and vice versa (the
// msgType gate is what routes a frame to the spiffs partition vs the app
// partition), (3) inspect() surfaces the FS type for dispatch.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

static const uint8_t kSrc[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const uint8_t kDst[6] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

static void test_fs_offer_roundtrip_and_dispatch() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {1, 2, 3, 4, 5, 6, 7, 8};
  const char* ch = "standard-stable";
  const size_t n = lp::buildFwOffer(
      buf, sizeof(buf), 42, kSrc, kDst, 0x000100A5, 0x30000, lp::FW_CHUNK_SIZE,
      ch, std::strlen(ch), sha, /*footerLen=*/72, /*totalChunks=*/983,
      lp::PROTOCOL_VERSION_EMIT, lp::MSG_FS_OFFER);
  TEST_ASSERT_EQUAL_UINT32(lp::FW_OFFER_FIXED_SIZE, n);

  // Dispatch sees the FS type.
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FS_OFFER, lp::inspect(buf, n));

  lp::ParsedFwOffer out;
  TEST_ASSERT_TRUE(lp::parseFwOffer(buf, n, out, lp::MSG_FS_OFFER));
  TEST_ASSERT_EQUAL_UINT32(0x000100A5, out.version);
  TEST_ASSERT_EQUAL_UINT32(0x30000, out.totalLen);
  TEST_ASSERT_EQUAL_UINT16(lp::FW_CHUNK_SIZE, out.chunkSize);
  TEST_ASSERT_EQUAL_UINT16(983, out.totalChunks);
  TEST_ASSERT_EQUAL_STRING("standard-stable", out.channel);
}

// An FS OFFER must NOT parse as a firmware OFFER (default expectType), or a
// stale-FS image could be written to the app partition.
static void test_fs_offer_not_parsed_as_fw() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const char* ch = "snafu-beta";
  const size_t n = lp::buildFwOffer(
      buf, sizeof(buf), 1, kSrc, kDst, 1, 100, lp::FW_CHUNK_SIZE, ch,
      std::strlen(ch), sha, 0, 1, lp::PROTOCOL_VERSION_EMIT, lp::MSG_FS_OFFER);
  lp::ParsedFwOffer out;
  TEST_ASSERT_FALSE(lp::parseFwOffer(buf, n, out));                    // default = FW
  TEST_ASSERT_FALSE(lp::parseFwOffer(buf, n, out, lp::MSG_FW_OFFER));  // explicit FW
}

// And a firmware OFFER must not parse as FS.
static void test_fw_offer_not_parsed_as_fs() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const char* ch = "standard-stable";
  const size_t n = lp::buildFwOffer(buf, sizeof(buf), 1, kSrc, kDst, 1, 100,
                                    lp::FW_CHUNK_SIZE, ch, std::strlen(ch), sha,
                                    0, 1);  // default msgType = MSG_FW_OFFER
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FW_OFFER, lp::inspect(buf, n));
  lp::ParsedFwOffer out;
  TEST_ASSERT_FALSE(lp::parseFwOffer(buf, n, out, lp::MSG_FS_OFFER));
}

// The chunk carrier (the bulk of an FS transfer) round-trips under MSG_FS_CHUNK
// and is type-isolated from FW chunks.
static void test_fs_chunk_roundtrip_and_isolation() {
  uint8_t payload[200];
  for (int i = 0; i < 200; ++i) payload[i] = static_cast<uint8_t>(i);
  uint8_t buf[lp::FW_CHUNK_MAX_SIZE];
  const size_t n = lp::buildFwChunk(buf, sizeof(buf), 7, kSrc, kDst,
                                    /*chunkIdx=*/5, /*offset=*/1000, payload, 200,
                                    lp::PROTOCOL_VERSION_EMIT, lp::MSG_FS_CHUNK);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FS_CHUNK, lp::inspect(buf, n));

  lp::ParsedFwChunk out;
  TEST_ASSERT_TRUE(lp::parseFwChunk(buf, n, out, lp::MSG_FS_CHUNK));
  TEST_ASSERT_EQUAL_UINT16(5, out.chunkIdx);
  TEST_ASSERT_EQUAL_UINT32(1000, out.offset);
  TEST_ASSERT_EQUAL_UINT16(200, out.len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.bytes, 200);

  TEST_ASSERT_FALSE(lp::parseFwChunk(buf, n, out));  // default FW type rejects
}

// FS RESULT carries the FS-specific status codes over the shared RESULT frame.
static void test_fs_result_roundtrip() {
  uint8_t buf[lp::FW_RESULT_FIXED_SIZE];
  const size_t n = lp::buildFwResult(
      buf, sizeof(buf), 3, kSrc, kDst, lp::FwResultStatus::FsDigestMismatch, 0,
      0x000100A5, lp::PROTOCOL_VERSION_EMIT, lp::MSG_FS_RESULT);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_FS_RESULT, lp::inspect(buf, n));
  lp::ParsedFwResult out;
  TEST_ASSERT_TRUE(lp::parseFwResult(buf, n, out, lp::MSG_FS_RESULT));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::FwResultStatus::FsDigestMismatch),
                          static_cast<uint8_t>(out.status));
  TEST_ASSERT_EQUAL_UINT32(0x000100A5, out.version);
}

// HELLO_TLV_FS_STATE round-trips the FS digest prefix and coexists with the
// existing OTA-state + fw-channel TLVs. Absent TLV → hasFsDigest=false.
static void test_hello_fs_digest_tlv() {
  const uint8_t shade[4] = {1, 2, 3, 4};
  const uint8_t base[4] = {5, 6, 7, 8};
  const uint8_t digest[lp::HELLO_FS_DIGEST_LEN] = {0xAA, 0xBB, 0xCC, 0xDD,
                                                   0xEE, 0xFF, 0x11, 0x22};
  uint8_t buf[lp::HELLO_MAX_SIZE];

  // All three TLVs emitted together (non-idle ota, channel, fs digest).
  size_t n = lp::buildHello(buf, sizeof(buf), 1, kSrc, shade, base, 0x000100A5,
                            "gramp", 5, lp::kOtaStateSending, "standard-stable",
                            digest);
  TEST_ASSERT_TRUE(n > 0);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_TRUE(out.hasFsDigest);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, out.fsDigest, lp::HELLO_FS_DIGEST_LEN);
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateSending, out.otaState);
  TEST_ASSERT_EQUAL_STRING("standard-stable", out.fwChannel);

  // No fs digest passed → absent → hasFsDigest false.
  n = lp::buildHello(buf, sizeof(buf), 1, kSrc, shade, base, 0x000100A5, "g", 1,
                     lp::kOtaStateIdle, nullptr, nullptr);
  lp::ParsedHello out2;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out2));
  TEST_ASSERT_FALSE(out2.hasFsDigest);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_hello_fs_digest_tlv);
  RUN_TEST(test_fs_offer_roundtrip_and_dispatch);
  RUN_TEST(test_fs_offer_not_parsed_as_fw);
  RUN_TEST(test_fw_offer_not_parsed_as_fs);
  RUN_TEST(test_fs_chunk_roundtrip_and_isolation);
  RUN_TEST(test_fs_result_roundtrip);
  return UNITY_END();
}

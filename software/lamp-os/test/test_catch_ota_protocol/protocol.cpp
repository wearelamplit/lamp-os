// Round-trip + invariant tests for catch_ota's OTA wire format port.
// Pin the byte layout so a refactor can't silently shift field offsets.
// Include the .cpp directly — no test_build_src, -I src is set.

#include "../../src/components/catch_ota/ota_protocol.cpp"

#include <unity.h>

#include <cstdint>
#include <cstring>

void setUp(void) {}
void tearDown(void) {}

namespace co = catch_ota;

// --------------------------------------------------------------------------
// test_offer_roundtrip
// Build a FW_OFFER frame with known values, parse it back, verify every
// field equals what was written. This pins the entire OFFER byte layout.
// --------------------------------------------------------------------------
void test_offer_roundtrip() {
    const uint8_t src[6] = {0xAA, 0xBB, 0xCC, 0x11, 0x22, 0x33};
    const uint8_t tgt[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint32_t version     = 0x00010203u;  // packed (1<<16)|(2<<8)|3
    const uint32_t totalLen    = 0x000C8000u;  // 819200 bytes
    const uint16_t chunkSize   = co::FW_CHUNK_SIZE;
    const char     channel[]   = "standard-stable";
    const uint8_t  sha[co::FW_SHA256_PREFIX_LEN] = {0xDE,0xAD,0xBE,0xEF,0x01,0x02,0x03,0x04};
    const uint16_t footerLen   = 512;
    const uint16_t totalChunks = 4096;

    uint8_t buf[co::FW_OFFER_FIXED_SIZE];
    size_t n = co::buildFwOffer(buf, sizeof(buf), 0x0042u, src, tgt,
                                version, totalLen, chunkSize,
                                channel, strlen(channel),
                                sha, footerLen, totalChunks);
    TEST_ASSERT_EQUAL_UINT(co::FW_OFFER_FIXED_SIZE, n);

    co::ParsedFwOffer out{};
    bool ok = co::parseFwOffer(buf, n, out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_UINT16(0x0042u, out.seq);
    TEST_ASSERT_EQUAL_MEMORY(src, out.sourceMac, 6);
    TEST_ASSERT_EQUAL_MEMORY(tgt, out.targetMac, 6);
    TEST_ASSERT_EQUAL_UINT32(version, out.version);
    TEST_ASSERT_EQUAL_UINT32(totalLen, out.totalLen);
    TEST_ASSERT_EQUAL_UINT16(chunkSize, out.chunkSize);
    TEST_ASSERT_EQUAL_STRING("standard-stable", out.channel);
    TEST_ASSERT_EQUAL_MEMORY(sha, out.sha256Prefix, co::FW_SHA256_PREFIX_LEN);
    TEST_ASSERT_EQUAL_UINT16(footerLen, out.footerLen);
    TEST_ASSERT_EQUAL_UINT16(totalChunks, out.totalChunks);
}

// --------------------------------------------------------------------------
// test_hello_proto_magic_and_no_tlv
// buildHello with zeroed shade+base, otaState=idle, fwChannel=nullptr
// must produce: magic 'L','M' at [0..1], version 0x05 at [2],
// firmwareVersion==1 packed LE at [20..23], and tlv_count==0.
// --------------------------------------------------------------------------
void test_hello_proto_magic_and_no_tlv() {
    const uint8_t mac[6]   = {0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE};
    const uint8_t shade[4] = {0, 0, 0, 0};
    const uint8_t base[4]  = {0, 0, 0, 0};
    // firmwareVersion=1 → packed (0<<16)|(0<<8)|1 = 1
    const uint32_t fwVer   = 1u;
    const char     name[]  = "test";

    uint8_t buf[co::HELLO_MAX_SIZE];
    size_t n = co::buildHello(buf, sizeof(buf), 0x0001u, mac, shade, base,
                              fwVer, name, strlen(name));
    TEST_ASSERT_TRUE(n > 0);

    // Magic
    TEST_ASSERT_EQUAL_UINT8('L', buf[0]);
    TEST_ASSERT_EQUAL_UINT8('M', buf[1]);

    // Protocol version byte hardcoded 0x05
    TEST_ASSERT_EQUAL_UINT8(0x05, buf[2]);

    // firmwareVersion LE at [20..23]
    uint32_t parsedVer =
        static_cast<uint32_t>(buf[20])
        | (static_cast<uint32_t>(buf[21]) << 8)
        | (static_cast<uint32_t>(buf[22]) << 16)
        | (static_cast<uint32_t>(buf[23]) << 24);
    TEST_ASSERT_EQUAL_UINT32(fwVer, parsedVer);

    // tlv_count == 0: the byte immediately after the name field
    // offset = HELLO_FIXED_SIZE(24) + 1(nameLen byte) + nameLen(4) = 29
    size_t tlvCountOff = co::HELLO_FIXED_SIZE + 1 + strlen(name);
    TEST_ASSERT_EQUAL_UINT8(0, buf[tlvCountOff]);
}

// --------------------------------------------------------------------------
// test_short_offer_rejected
// A truncated OFFER frame (one byte short) must be rejected by parseFwOffer.
// --------------------------------------------------------------------------
void test_short_offer_rejected() {
    const uint8_t src[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
    const uint8_t tgt[6] = {0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C};
    const uint8_t sha[co::FW_SHA256_PREFIX_LEN] = {};

    uint8_t buf[co::FW_OFFER_FIXED_SIZE];
    size_t n = co::buildFwOffer(buf, sizeof(buf), 1u, src, tgt,
                                1u, 1000u, co::FW_CHUNK_SIZE,
                                "stable", 6, sha, 0, 5);
    TEST_ASSERT_EQUAL_UINT(co::FW_OFFER_FIXED_SIZE, n);

    co::ParsedFwOffer out{};
    // One byte short — must reject
    TEST_ASSERT_FALSE(co::parseFwOffer(buf, n - 1, out));
    // Zero length — must reject
    TEST_ASSERT_FALSE(co::parseFwOffer(buf, 0, out));
    // nullptr — must reject
    TEST_ASSERT_FALSE(co::parseFwOffer(nullptr, n, out));
}

// --------------------------------------------------------------------------
// test_req_chunkcount_range
// buildFwReq: chunkCount 1..32 accepted, 0 or 33 rejected.
// --------------------------------------------------------------------------
void test_req_chunkcount_range() {
    const uint8_t src[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    const uint8_t tgt[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    uint8_t buf[co::FW_REQ_FIXED_SIZE];

    // chunkCount=1 must succeed
    TEST_ASSERT_EQUAL_UINT(co::FW_REQ_FIXED_SIZE,
        co::buildFwReq(buf, sizeof(buf), 1u, src, tgt,
                       0u, 1u, co::FwReqReason::Gap));

    // chunkCount=32 must succeed
    TEST_ASSERT_EQUAL_UINT(co::FW_REQ_FIXED_SIZE,
        co::buildFwReq(buf, sizeof(buf), 2u, src, tgt,
                       0u, 32u, co::FwReqReason::Gap));

    // chunkCount=0 must fail
    TEST_ASSERT_EQUAL_UINT(0u,
        co::buildFwReq(buf, sizeof(buf), 3u, src, tgt,
                       0u, 0u, co::FwReqReason::Gap));

    // chunkCount=33 must fail
    TEST_ASSERT_EQUAL_UINT(0u,
        co::buildFwReq(buf, sizeof(buf), 4u, src, tgt,
                       0u, 33u, co::FwReqReason::Gap));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_offer_roundtrip);
    RUN_TEST(test_hello_proto_magic_and_no_tlv);
    RUN_TEST(test_short_offer_rejected);
    RUN_TEST(test_req_chunkcount_range);
    return UNITY_END();
}

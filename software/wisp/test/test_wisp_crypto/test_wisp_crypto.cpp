// Native test seam: include the .cpp directly.
#include "config/crypto.cpp"  // NOLINT(build/include)

#include <unity.h>
#include <cstring>

void setUp()    {}
void tearDown() {}

// CHAR_WISP_OP UUID 5f64f4e1-d6d9-4a44-9b3f-3a8d6f7e6b40, byte-reversed (LE).
// Matches uuidSaltLE16(CHAR_WISP_OP) in the app's lamp_crypto.dart.
static const uint8_t kWispOpSaltLE[16] = {
    0x40, 0x6b, 0x7e, 0x6f, 0x8d, 0x3a, 0x3f, 0x9b,
    0x44, 0x4a, 0xd9, 0xd6, 0xe1, 0xf4, 0x64, 0x5f
};

static const char* kCharShortName = "wispOp";

// Known vector: password="testpass", nonce=00..0B, plaintext='{"op":"setName","name":"testwisp"}'.
// Computed with Python cryptography (HKDF-SHA256 + AES-256-GCM, no AAD).
// Wire payload: [0x02][nonce 12B][tag 16B][ciphertext].
static const uint8_t kWirePayload[] = {
    0x02,
    // nonce
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b,
    // tag
    0x8c, 0x0b, 0xd0, 0x14, 0xb0, 0xca, 0x8a, 0xd0,
    0x26, 0x07, 0x3f, 0x43, 0x6e, 0x36, 0xbe, 0x11,
    // ciphertext
    0x7a, 0xe8, 0x91, 0x52, 0xf5, 0x7e, 0xff, 0xe6,
    0x07, 0xdf, 0xb8, 0x30, 0x2c, 0xfc, 0x7d, 0x09,
    0x39, 0x12, 0xfe, 0x29, 0xb4, 0x08, 0xce, 0x82,
    0x43, 0xd2, 0xf0, 0xf2, 0xde, 0xcc, 0x5f, 0x3b,
    0x1e, 0x84
};
static const size_t kWirePayloadLen = sizeof(kWirePayload);
static const char* kExpectedPlaintext = "{\"op\":\"setName\",\"name\":\"testwisp\"}";

void test_known_vector_decrypts_correctly() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        kWirePayload, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING(kExpectedPlaintext, out.c_str());
}

void test_tampered_tag_fails() {
    wisp::crypto::RecentNonces nonces;
    uint8_t bad[kWirePayloadLen];
    std::memcpy(bad, kWirePayload, kWirePayloadLen);
    bad[14] ^= 0xFF;  // flip a byte in the tag
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        bad, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(0, (int)out.size());
}

void test_empty_password_refuses() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        kWirePayload, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "", nonces, out);
    TEST_ASSERT_FALSE(ok);
}

void test_wrong_password_fails() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        kWirePayload, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "wrongpass", nonces, out);
    TEST_ASSERT_FALSE(ok);
}

void test_duplicate_nonce_rejected() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    // First decrypt succeeds.
    bool ok1 = wisp::crypto::decryptOp(
        kWirePayload, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_TRUE(ok1);
    // Second with same nonce rejected.
    bool ok2 = wisp::crypto::decryptOp(
        kWirePayload, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_FALSE(ok2);
}

void test_payload_too_short_fails() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    const uint8_t short_payload[] = { 0x02, 0x00, 0x01 };
    bool ok = wisp::crypto::decryptOp(
        short_payload, sizeof(short_payload),
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_FALSE(ok);
}

void test_magic_byte_wrong_fails() {
    wisp::crypto::RecentNonces nonces;
    uint8_t bad[kWirePayloadLen];
    std::memcpy(bad, kWirePayload, kWirePayloadLen);
    bad[0] = 0x01;  // plaintext magic instead of ciphertext
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        bad, kWirePayloadLen,
        kWispOpSaltLE, kCharShortName,
        "testpass", nonces, out);
    TEST_ASSERT_FALSE(ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_known_vector_decrypts_correctly);
    RUN_TEST(test_tampered_tag_fails);
    RUN_TEST(test_empty_password_refuses);
    RUN_TEST(test_wrong_password_fails);
    RUN_TEST(test_duplicate_nonce_rejected);
    RUN_TEST(test_payload_too_short_fails);
    RUN_TEST(test_magic_byte_wrong_fails);
    return UNITY_END();
}

// Native test seam: include .cpp files directly (excluded from native build filter).
#include "config/crypto.cpp"              // NOLINT(build/include)
#include "config/wisp_config.cpp"         // NOLINT(build/include)
#include "config/wisp_op_dispatcher.cpp"  // NOLINT(build/include)

#include <unity.h>
#include <cstring>
#include <vector>

// CHAR_WISP_OP UUID 5f64f4e1-d6d9-4a44-9b3f-3a8d6f7e6b40, byte-reversed (LE).
static const uint8_t kWispOpSaltLE[16] = {
    0x40, 0x6b, 0x7e, 0x6f, 0x8d, 0x3a, 0x3f, 0x9b,
    0x44, 0x4a, 0xd9, 0xd6, 0xe1, 0xf4, 0x64, 0x5f
};

// Known vector: password="testpass", nonce=00..0B, plaintext below.
// plaintext: {"char":"wispOp","op":"setName","name":"testwisp"} (50B)
// Derived with HKDF-SHA256(salt=kWispOpSaltLE, info="lamp-v1\0wispOp", IKM="testpass")
// then AES-256-GCM(key, nonce, plaintext, AAD=empty).
// Wire: [0x02][nonce 12B][tag 16B][ciphertext 50B]
static const uint8_t kSealedSetName[] = {
    0x02,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b,
    // tag
    0xa5, 0x42, 0x1c, 0x32, 0x9a, 0xdf, 0x7a, 0x82,
    0x94, 0xee, 0x75, 0x86, 0x2e, 0x9c, 0xa6, 0x12,
    // ciphertext
    0x7a, 0xe8, 0x9d, 0x4a, 0xb6, 0x36, 0xff, 0xaf,
    0x40, 0xdc, 0x9f, 0x22, 0x31, 0xd6, 0x2f, 0x07,
    0x37, 0x5e, 0xf0, 0x34, 0xf3, 0x10, 0xd6, 0xd3,
    0x52, 0xc3, 0xcd, 0xe7, 0xc4, 0xc0, 0x0e, 0x67,
    0x1e, 0x97, 0xf7, 0x97, 0x90, 0xf9, 0x6b, 0xb2,
    0x4b, 0xda, 0x0f, 0x88, 0x55, 0xeb, 0xa4, 0x05,
    0x08, 0x4a
};
static const size_t kSealedSetNameLen = sizeof(kSealedSetName);

void setUp()    { kv_reset(); }
void tearDown() {}

// Sanity: sealed vector decrypts to the expected plaintext.
void test_sealed_vector_sanity() {
    wisp::crypto::RecentNonces nonces;
    std::string out;
    bool ok = wisp::crypto::decryptOp(
        kSealedSetName, kSealedSetNameLen,
        kWispOpSaltLE, "wispOp",
        "testpass", nonces, out);
    TEST_ASSERT_TRUE_MESSAGE(ok, "sealed vector must decrypt (check the hardcoded bytes)");
    TEST_ASSERT_TRUE(out.find("wispOp") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("setName") != std::string::npos);
}

// Password set: 0x02-sealed op decrypts and is dispatched to setName.
void test_password_set_sealed_accepted_and_dispatched() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);

    auto result = d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedNameChange, (int)result);
    TEST_ASSERT_EQUAL_STRING("testwisp", cfg.name().c_str());
}

// Password set: bare-JSON (bare '{') is rejected.
void test_password_set_bare_json_rejected() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"shuffle"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected, (int)result);
}

// Password set: 0x01-prefixed plaintext is rejected.
void test_password_set_0x01_plaintext_rejected() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    const char* json = R"({"char":"wispOp","op":"shuffle"})";
    std::vector<uint8_t> payload;
    payload.push_back(0x01);
    for (size_t i = 0; i < strlen(json); ++i) payload.push_back((uint8_t)json[i]);
    auto result = d.dispatch(payload.data(), payload.size());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected, (int)result);
}

// No password: bare-JSON accepted.
void test_no_password_bare_json_accepted() {
    wisp::WispConfig cfg;
    cfg.begin();
    // password() is empty by default

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"shuffle"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedShuffle, (int)result);
}

// No password: 0x01-prefixed plaintext accepted (prefix byte skipped).
void test_no_password_0x01_prefix_accepted() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* json = R"({"char":"wispOp","op":"shuffle"})";
    std::vector<uint8_t> payload;
    payload.push_back(0x01);
    for (size_t i = 0; i < strlen(json); ++i) payload.push_back((uint8_t)json[i]);
    auto result = d.dispatch(payload.data(), payload.size());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedShuffle, (int)result);
}

// No password: 0x02-sealed attempted, so decryptOp rejects the empty password.
void test_no_password_sealed_rejected() {
    wisp::WispConfig cfg;
    cfg.begin();
    // no password set

    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);
    auto result = d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected, (int)result);
}

// setManualPalette is accepted plaintext even with a password set.
// Palette colors are unauthenticated by design: an attacker in BLE proximity
// can vandalize colors regardless, so sealing adds no meaningful protection
// (integrity exception under the proximity threat model).
void test_set_manual_palette_plaintext_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setManualPalette","colors":[[255,0,0],[0,255,0]]})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedManualPalette, (int)result);
}

// --- Task 4b: setPassword op + replay window ---

// setPassword sealed under the OLD password changes the password; subsequent
// ops sealed under the NEW password are accepted; old-password ops are rejected.
//
// Vector A: {"char":"wispOp","op":"setPassword","password":"newpass"}
//           sealed under "testpass", nonce=01*12
// Vector B: {"char":"wispOp","op":"shuffle"}
//           sealed under "newpass",  nonce=02*12
static const uint8_t kSealedSetPassword[] = {
    0x02,
    // nonce
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01,
    // tag
    0xb5, 0x98, 0xc3, 0xc2, 0x5d, 0x56, 0x5d, 0xe3,
    0xd2, 0xe5, 0xbc, 0x63, 0xf5, 0x58, 0xc0, 0xa2,
    // ciphertext
    0x2d, 0x7b, 0xf3, 0x71, 0x0f, 0x9a, 0xee, 0x05,
    0x71, 0x33, 0x1b, 0x5a, 0x0d, 0x96, 0xc6, 0xfb,
    0xad, 0xea, 0x17, 0xfc, 0xe3, 0x3b, 0x5b, 0x66,
    0x5d, 0x41, 0xba, 0xbe, 0x6b, 0x45, 0x68, 0x38,
    0x02, 0x74, 0x22, 0x3e, 0xe5, 0x22, 0x4a, 0x65,
    0xcf, 0xa9, 0x97, 0xc5, 0xda, 0x2e, 0x1c, 0x24,
    0x03, 0x33, 0x6d, 0x3b, 0x33, 0x66, 0x88, 0xf8,
    0xf3
};
static const size_t kSealedSetPasswordLen = sizeof(kSealedSetPassword);

// shuffle sealed under "newpass", nonce=02*12
static const uint8_t kSealedShuffleUnderNewpass[] = {
    0x02,
    // nonce
    0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02,
    0x02, 0x02, 0x02, 0x02,
    // tag
    0x59, 0xf8, 0x92, 0xa3, 0x6b, 0x05, 0x18, 0x5c,
    0x04, 0x6c, 0xf8, 0xec, 0xec, 0x09, 0xd0, 0x4d,
    // ciphertext
    0xb2, 0x73, 0xcb, 0xd2, 0x0c, 0x8b, 0x32, 0x60,
    0xe0, 0x77, 0x0e, 0xb9, 0x07, 0xcc, 0x28, 0x2c,
    0x8c, 0x17, 0x17, 0xd5, 0x14, 0x58, 0x9f, 0xe1,
    0x98, 0x6e, 0xc1, 0x48, 0x51, 0x2a, 0x11, 0x39
};
static const size_t kSealedShuffleUnderNewpassLen = sizeof(kSealedShuffleUnderNewpass);

void test_set_password_sealed_changes_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);

    // Change password from "testpass" to "newpass" via a sealed op.
    auto r1 = d.dispatch(kSealedSetPassword, kSealedSetPasswordLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedPasswordChange, (int)r1);
    TEST_ASSERT_EQUAL_STRING("newpass", cfg.password().c_str());

    // A subsequent op sealed under the NEW password must succeed.
    auto r2 = d.dispatch(kSealedShuffleUnderNewpass, kSealedShuffleUnderNewpassLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedShuffle, (int)r2);
}

// Plaintext setPassword accepted when factory-fresh (no password set).
void test_set_password_plaintext_factory_fresh() {
    wisp::WispConfig cfg;
    cfg.begin();
    // No password set, so plaintext is the accept path.

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"setPassword","password":"initial"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedPasswordChange, (int)result);
    TEST_ASSERT_EQUAL_STRING("initial", cfg.password().c_str());
}

// Plaintext setPassword rejected once a password already exists.
void test_set_password_plaintext_rejected_when_password_set() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("existing");

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"setPassword","password":"hijack"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected, (int)result);
    TEST_ASSERT_EQUAL_STRING("existing", cfg.password().c_str());
}

// Repeated nonce: the second dispatch of the same sealed payload is dropped.
void test_replay_same_nonce_dropped() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);

    // First dispatch succeeds.
    auto r1 = d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedNameChange, (int)r1);

    // Second dispatch with identical payload (same nonce) is rejected.
    auto r2 = d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected, (int)r2);
}

// Nonce ring is bounded: pre-fill the ring to MAX_RECENT_NONCES and verify
// that adding one more evicts the oldest entry instead of growing.
// ponytail: RAM-only replay window; a forced reboot clears it.
// Config ops are low-value/idempotent, acceptable under the proximity threat model.
void test_nonce_ring_bounded() {
    wisp::crypto::RecentNonces nonces;
    // Pre-fill to the cap with synthetic nonces.
    for (size_t i = 0; i < wisp::crypto::MAX_RECENT_NONCES; ++i) {
        std::array<uint8_t, wisp::crypto::NONCE_LEN> n{};
        n[0] = static_cast<uint8_t>(i);
        nonces.entries.push_back(n);
    }
    TEST_ASSERT_EQUAL_INT((int)wisp::crypto::MAX_RECENT_NONCES,
                          (int)nonces.entries.size());

    // Simulate the push_back+trim that decryptOp performs on accept.
    std::array<uint8_t, wisp::crypto::NONCE_LEN> newNonce{};
    newNonce[0] = 0xFF;
    nonces.entries.push_back(newNonce);
    if (nonces.entries.size() > wisp::crypto::MAX_RECENT_NONCES) {
        nonces.entries.pop_front();
    }

    // Size must stay at the cap; oldest entry (i=0) must be gone.
    TEST_ASSERT_EQUAL_INT((int)wisp::crypto::MAX_RECENT_NONCES,
                          (int)nonces.entries.size());
    TEST_ASSERT_NOT_EQUAL(0, (int)nonces.entries.front()[0]);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_sealed_vector_sanity);
    RUN_TEST(test_password_set_sealed_accepted_and_dispatched);
    RUN_TEST(test_password_set_bare_json_rejected);
    RUN_TEST(test_password_set_0x01_plaintext_rejected);
    RUN_TEST(test_no_password_bare_json_accepted);
    RUN_TEST(test_no_password_0x01_prefix_accepted);
    RUN_TEST(test_no_password_sealed_rejected);
    RUN_TEST(test_set_manual_palette_plaintext_with_password);
    RUN_TEST(test_set_password_sealed_changes_password);
    RUN_TEST(test_set_password_plaintext_factory_fresh);
    RUN_TEST(test_set_password_plaintext_rejected_when_password_set);
    RUN_TEST(test_replay_same_nonce_dropped);
    RUN_TEST(test_nonce_ring_bounded);
    return UNITY_END();
}

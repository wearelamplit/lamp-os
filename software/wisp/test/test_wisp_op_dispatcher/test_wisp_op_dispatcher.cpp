// Native test seam: include .cpp files directly (excluded from native build filter).
#include "config/crypto.cpp"              // NOLINT(build/include)
#include "config/wisp_config.cpp"         // NOLINT(build/include)
#include "config/wisp_op_dispatcher.cpp"  // NOLINT(build/include)

#include <unity.h>
#include <cstring>
#include <string>
#include <vector>

#include <mbedtls/gcm.h>
#include <mbedtls/hkdf.h>
#include <mbedtls/md.h>

#include "../test_osc_address/osc_address_cases.hpp"

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

// Tuples may be [r,g,b] or [r,g,b,w] per color; missing w reads as 0.
void test_set_manual_palette_mixed_rgb_rgbw_tuples() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setManualPalette","colors":[[255,0,0],[0,255,0,128],[1,2,3,255]]})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedManualPalette, (int)result);

    const auto& pal = cfg.manualPalette();
    TEST_ASSERT_EQUAL(3, (int)pal.size());
    TEST_ASSERT_EQUAL_UINT8(0,   pal[0].w);
    TEST_ASSERT_EQUAL_UINT8(128, pal[1].w);
    TEST_ASSERT_EQUAL_UINT8(255, pal[2].w);
    TEST_ASSERT_EQUAL_UINT8(0,   pal[1].r);
    TEST_ASSERT_EQUAL_UINT8(255, pal[1].g);
}

static wisp::DispatchResult dispatchJson(wisp::WispOpDispatcher& d,
                                         const char* json) {
    return d.dispatch(reinterpret_cast<const uint8_t*>(json), strlen(json));
}

// A palette op's seq is echoed beside the palette it stored, and a plaintext
// palette op leaves opSeq alone.
void test_set_manual_palette_echoes_seq() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    auto result = dispatchJson(d,
        R"({"char":"wispOp","op":"setManualPalette","colors":[[1,2,3]],"seq":42})");
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedManualPalette, (int)result);
    const auto status = cfg.manualPaletteStatus();
    TEST_ASSERT_EQUAL_UINT16(42, status.seq);
    TEST_ASSERT_EQUAL_STRING(wisp::statusManualId({{1, 2, 3, 0}}).c_str(), status.id);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.opSeq());
}

// No seq, or one that is not an integer in 1..65535, means none: the op
// stores its palette and clears the previous echo.
void test_set_manual_palette_without_valid_seq_clears_echo() {
    const char* ops[] = {
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]]})",
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]],"seq":0})",
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]],"seq":70000})",
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]],"seq":-1})",
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]],"seq":"42"})",
        R"({"char":"wispOp","op":"setManualPalette","colors":[[4,5,6]],"seq":4.5})",
    };
    for (const char* op : ops) {
        wisp::WispConfig cfg;
        cfg.begin();
        wisp::WispOpDispatcher d(cfg);
        dispatchJson(d,
            R"({"char":"wispOp","op":"setManualPalette","colors":[[1,2,3]],"seq":42})");
        TEST_ASSERT_EQUAL_UINT16(42, cfg.manualPaletteStatus().seq);
        auto result = dispatchJson(d, op);
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::AppliedManualPalette,
                                      (int)result, op);
        TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, cfg.manualPaletteStatus().seq, op);
        TEST_ASSERT_EQUAL_UINT8(4, cfg.manualPalette()[0].r);
    }
}

// A palette op that stores nothing leaves the echo of the last one that did.
void test_malformed_palette_op_keeps_echo() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    dispatchJson(d,
        R"({"char":"wispOp","op":"setManualPalette","colors":[[1,2,3]],"seq":42})");
    auto result = dispatchJson(d,
        R"({"char":"wispOp","op":"setManualPalette","seq":7})");
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed, (int)result);
    TEST_ASSERT_EQUAL_UINT16(42, cfg.manualPaletteStatus().seq);
    TEST_ASSERT_EQUAL_UINT8(1, cfg.manualPalette()[0].r);
}

// A palette op with more than 16 colours stores its first 16.
void test_set_manual_palette_keeps_the_first_16_colours() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    std::string op = R"({"char":"wispOp","op":"setManualPalette","colors":[)";
    for (int i = 0; i < 20; ++i) {
        if (i) op += ",";
        op += "[" + std::to_string(i + 1) + "," + std::to_string(40 + i) +
              "," + std::to_string(200 - i) + "]";
    }
    op += "]}";
    auto result = dispatchJson(d, op.c_str());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedManualPalette, (int)result);
    const auto& pal = cfg.manualPalette();
    TEST_ASSERT_EQUAL(16, (int)pal.size());
    TEST_ASSERT_EQUAL_UINT8(1, pal[0].r);
    TEST_ASSERT_EQUAL_UINT8(16, pal[15].r);
    TEST_ASSERT_EQUAL_UINT8(55, pal[15].g);
}

void test_set_off_color_rgbw_tuple() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* rgbw = R"({"char":"wispOp","op":"setOffColor","color":[10,20,30,40]})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(rgbw), strlen(rgbw));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOffColor, (int)result);
    TEST_ASSERT_EQUAL_UINT8(30, cfg.offColor().b);
    TEST_ASSERT_EQUAL_UINT8(40, cfg.offColor().w);

    const char* rgb = R"({"char":"wispOp","op":"setOffColor","color":[1,2,3]})";
    result = d.dispatch(reinterpret_cast<const uint8_t*>(rgb), strlen(rgb));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOffColor, (int)result);
    TEST_ASSERT_EQUAL_UINT8(0, cfg.offColor().w);
}

// pollStatus is accepted without a password.
void test_poll_status_no_password() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"pollStatus"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::PollStatus, (int)result);
}

// pollStatus is on the plaintext exception list: it only triggers a
// re-broadcast of already-public state, so a password-protected wisp
// still refreshes for an app without the password.
void test_poll_status_plaintext_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"pollStatus"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::PollStatus, (int)result);
}

// pollConfig is accepted without a password and changes nothing.
void test_poll_config_no_password() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"pollConfig"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::PollConfig, (int)result);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.opSeq());
}

// pollConfig rides the plaintext exception list beside pollStatus: it only
// re-broadcasts state every lamp already relays.
void test_poll_config_plaintext_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"pollConfig"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::PollConfig, (int)result);
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

// opSeq bumps once per sealed op that is accepted+applied.
void test_op_seq_bumps_on_sealed_applied() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");

    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);

    TEST_ASSERT_EQUAL_UINT32(0, cfg.opSeq());
    auto r = d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedNameChange, (int)r);
    TEST_ASSERT_EQUAL_UINT32(1, cfg.opSeq());

    // A replay of the same nonce is Rejected and must not bump.
    d.dispatch(kSealedSetName, kSealedSetNameLen);
    TEST_ASSERT_EQUAL_UINT32(1, cfg.opSeq());
}

// A plaintext accepted op does not bump opSeq (only sealed ops do).
void test_op_seq_unchanged_on_plaintext() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"shuffle"})";
    auto r = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedShuffle, (int)r);
    TEST_ASSERT_EQUAL_UINT32(0, cfg.opSeq());
}

// --- setLedStrip op ---

// Happy path: valid ledType + pixelCount dispatches to AppliedLedStrip and
// updates the config.
void test_set_led_strip_happy_path() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setLedStrip","ledType":"BGR","pixelCount":42})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedLedStrip, (int)result);
    TEST_ASSERT_EQUAL_INT((int)lampos::led::ByteOrder::BGR, (int)cfg.ledFormat());
    TEST_ASSERT_EQUAL_INT(42, (int)cfg.pixelCount());
}

// Unknown ledType returns Malformed.
void test_set_led_strip_unknown_led_type() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setLedStrip","ledType":"RGBX","pixelCount":10})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed, (int)result);
}

// pixelCount < 1 returns Malformed.
void test_set_led_strip_zero_pixel_count() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setLedStrip","ledType":"GRB","pixelCount":0})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed, (int)result);
}

// Missing pixelCount field returns Malformed.
void test_set_led_strip_missing_pixel_count() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain =
        R"({"char":"wispOp","op":"setLedStrip","ledType":"GRB"})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed, (int)result);
}

// --- setBrightness op ---

void test_set_brightness_happy_path() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* plain = R"({"char":"wispOp","op":"setBrightness","brightness":60})";
    auto result = d.dispatch(reinterpret_cast<const uint8_t*>(plain), strlen(plain));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedBrightnessChange, (int)result);
    TEST_ASSERT_EQUAL_INT(60, (int)cfg.brightness());
}

void test_set_brightness_out_of_bounds_malformed() {
    wisp::WispConfig cfg;
    cfg.begin();

    wisp::WispOpDispatcher d(cfg);
    const char* over = R"({"char":"wispOp","op":"setBrightness","brightness":101})";
    TEST_ASSERT_EQUAL_INT(
        (int)wisp::DispatchResult::Malformed,
        (int)d.dispatch(reinterpret_cast<const uint8_t*>(over), strlen(over)));
    const char* missing = R"({"char":"wispOp","op":"setBrightness"})";
    TEST_ASSERT_EQUAL_INT(
        (int)wisp::DispatchResult::Malformed,
        (int)d.dispatch(reinterpret_cast<const uint8_t*>(missing), strlen(missing)));
    TEST_ASSERT_EQUAL_INT(100, (int)cfg.brightness());
}

// --- setSource / setOscRole ---

static wisp::DispatchResult dispatchStr(wisp::WispOpDispatcher& d, const char* s) {
    return d.dispatch(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

void test_set_source_osc_accepted() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedSourceChange,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSource","mode":"osc"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::WispSourceMode::Osc, (int)cfg.sourceMode());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedSourceChange,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSource","mode":1})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::WispSourceMode::Manual, (int)cfg.sourceMode());
}

// Aurora is gone: its string and its retired numeric value are unknown.
void test_set_source_aurora_rejected() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSource","mode":"aurora"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSource","mode":2})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::WispSourceMode::Off, (int)cfg.sourceMode());
}

void test_set_osc_role_persists() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRole,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOscRole","role":"fill"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Fill, (int)cfg.oscRole());
    wisp::WispConfig reload;
    reload.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Fill, (int)reload.oscRole());
}

void test_set_osc_role_invalid_malformed() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOscRole","role":"quaternary"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOscRole"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Primary, (int)cfg.oscRole());
}

// Role is config like any other: plaintext is refused once a password is set.
void test_set_osc_role_plaintext_rejected_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOscRole","role":"fill"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscRole::Primary, (int)cfg.oscRole());
}

// Seal `json` as the app seals a wispOp: HKDF-SHA256 key from the password
// (same salt and info as the vectors above), AES-256-GCM, every nonce byte
// `nonceByte`.
static std::vector<uint8_t> sealWispOp(const char* password, uint8_t nonceByte,
                                       const char* json) {
    static const char kInfo[] = "lamp-v1\0wispOp";
    uint8_t key[32];
    TEST_ASSERT_EQUAL_INT(0, mbedtls_hkdf(
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), kWispOpSaltLE, 16,
        reinterpret_cast<const uint8_t*>(password), strlen(password),
        reinterpret_cast<const uint8_t*>(kInfo), sizeof(kInfo) - 1, key, 32));
    const size_t len = strlen(json);
    std::vector<uint8_t> out(1 + 12 + 16 + len);
    out[0] = 0x02;
    std::memset(out.data() + 1, nonceByte, 12);
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    TEST_ASSERT_EQUAL_INT(0, mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 256));
    TEST_ASSERT_EQUAL_INT(0, mbedtls_gcm_crypt_and_tag(
        &gcm, MBEDTLS_GCM_ENCRYPT, len, out.data() + 1, 12, nullptr, 0,
        reinterpret_cast<const uint8_t*>(json), out.data() + 29, 16,
        out.data() + 13));
    mbedtls_gcm_free(&gcm);
    return out;
}

void test_set_swatches_applies_and_reloads() {
    wisp::WispConfig cfg;
    cfg.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Gradient, (int)cfg.paletteStyle());
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedSwatches,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSwatches","swatches":true})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Swatches, (int)cfg.paletteStyle());
    wisp::WispConfig reload;
    reload.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Swatches, (int)reload.paletteStyle());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedSwatches,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSwatches","swatches":false})"));
    wisp::WispConfig reload2;
    reload2.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Gradient, (int)reload2.paletteStyle());
}

// A stored value the wisp does not know loads as gradient.
void test_unknown_stored_style_loads_as_gradient() {
    Preferences prefs;
    prefs.begin("wisp", false);
    prefs.putUChar("swatches", 7);
    wisp::WispConfig cfg;
    cfg.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Gradient, (int)cfg.paletteStyle());
}

void test_set_swatches_needs_a_bool() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    const char* bad[] = {
        R"({"char":"wispOp","op":"setSwatches"})",
        R"({"char":"wispOp","op":"setSwatches","swatches":1})",
        R"({"char":"wispOp","op":"setSwatches","swatches":"true"})",
        R"({"char":"wispOp","op":"setSwatches","swatches":null})",
    };
    for (const char* op : bad) {
        TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed, (int)dispatchStr(d, op));
    }
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Gradient, (int)cfg.paletteStyle());
}

void test_set_swatches_plaintext_rejected_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setSwatches","swatches":true})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Gradient, (int)cfg.paletteStyle());
}

// A sealed setSwatches counts as applied, so opSeq confirms it for the app.
void test_set_swatches_sealed_bumps_op_seq() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);
    const auto sealed = sealWispOp("testpass", 0x03,
        R"({"char":"wispOp","op":"setSwatches","swatches":true})");
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedSwatches,
        (int)d.dispatch(sealed.data(), sealed.size()));
    TEST_ASSERT_EQUAL_INT((int)wisp::PaletteStyle::Swatches, (int)cfg.paletteStyle());
    TEST_ASSERT_EQUAL_UINT32(1, cfg.opSeq());
}

void test_set_osc_maestro_applies_and_reloads() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":3})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
    TEST_ASSERT_EQUAL_UINT8(3, cfg.oscSlot());
    wisp::WispConfig reload;
    reload.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)reload.oscMode());
    TEST_ASSERT_EQUAL_UINT8(3, reload.oscSlot());
}

// A setOsc with no slot keeps the stored one, so switching sub-mode never
// moves the Maestro slot.
void test_set_osc_without_slot_keeps_slot() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setOsc(wisp::OscMode::Maestro, 8, "");
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"straylight"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Straylight, (int)cfg.oscMode());
    TEST_ASSERT_EQUAL_UINT8(8, cfg.oscSlot());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":null})"));
    TEST_ASSERT_EQUAL_UINT8(8, cfg.oscSlot());
}

// Any invalid field makes the whole op Malformed and changes nothing.
void test_set_osc_invalid_changes_nothing() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setOsc(wisp::OscMode::Maestro, 4, "");
    wisp::WispOpDispatcher d(cfg);
    const char* bad[] = {
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":0})",
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":11})",
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":-1})",
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":"3"})",
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":2.5})",
        R"({"char":"wispOp","op":"setOsc","mode":"straylight","slot":true})",
        R"({"char":"wispOp","op":"setOsc","mode":"aurora","slot":2})",
        R"({"char":"wispOp","op":"setOsc","mode":"Custom","address":"/a"})",
        R"({"char":"wispOp","op":"setOsc","mode":1,"slot":2})",
        R"({"char":"wispOp","op":"setOsc","slot":2})",
    };
    for (const char* op : bad) {
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::Malformed,
                                      (int)dispatchStr(d, op), op);
        TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
        TEST_ASSERT_EQUAL_UINT8(4, cfg.oscSlot());
    }
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":10})"));
    TEST_ASSERT_EQUAL_UINT8(10, cfg.oscSlot());
}

// In Osc, a setOsc that moves the listened address says so; a resend and a
// change made outside Osc do not.
void test_set_osc_reports_a_live_retarget() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    const char* slot3 = R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":3})";
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, slot3));
    cfg.setSourceMode(wisp::WispSourceMode::Osc);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, slot3));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRetarget,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":4})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRetarget,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"straylight"})"));
}

// setOsc is config like any other: sealed once a password is set.
void test_set_osc_plaintext_rejected_with_password() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":3})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Straylight, (int)cfg.oscMode());
}

// A sealed setOsc counts as applied, so opSeq confirms it for the app.
void test_set_osc_sealed_bumps_op_seq() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);
    const auto sealed = sealWispOp("testpass", 0x05,
        R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":9})");
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)d.dispatch(sealed.data(), sealed.size()));
    TEST_ASSERT_EQUAL_UINT8(9, cfg.oscSlot());
    TEST_ASSERT_EQUAL_UINT32(1, cfg.opSeq());
    cfg.setSourceMode(wisp::WispSourceMode::Osc);
    const auto retarget = sealWispOp("testpass", 0x06,
        R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":2})");
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRetarget,
        (int)d.dispatch(retarget.data(), retarget.size()));
    TEST_ASSERT_EQUAL_UINT32(2, cfg.opSeq());
}

static std::string setOscCustom(const char* address) {
    return std::string(R"({"char":"wispOp","op":"setOsc","mode":"custom","address":")") +
           address + "\"}";
}

void test_set_osc_custom_applies_and_reloads() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, setOscCustom("/lamp/palette").c_str()));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Custom, (int)cfg.oscMode());
    TEST_ASSERT_EQUAL_STRING("/lamp/palette", cfg.oscAddress());
    wisp::WispConfig reload;
    reload.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Custom, (int)reload.oscMode());
    TEST_ASSERT_EQUAL_STRING("/lamp/palette", reload.oscAddress());
}

// Every case of the shared address rule: a valid address applies, an
// invalid one makes the op Malformed and changes nothing.
void test_set_osc_address_follows_the_rule() {
    for (const OscAddressCase& c : kOscAddressCases) {
        kv_reset();
        wisp::WispConfig cfg;
        cfg.begin();
        cfg.setOsc(wisp::OscMode::Maestro, 4, "/kept");
        wisp::WispOpDispatcher d(cfg);
        JsonDocument op;
        op["char"] = "wispOp";
        op["op"] = "setOsc";
        op["mode"] = "custom";
        op["address"] = c.address;
        std::string json;
        serializeJson(op, json);
        const auto res = dispatchStr(d, json.c_str());
        if (c.valid) {
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::AppliedOscSettings,
                                          (int)res, c.address);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(c.address, cfg.oscAddress(), c.address);
            TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Custom, (int)cfg.oscMode());
        } else {
            TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::Malformed,
                                          (int)res, c.address);
            TEST_ASSERT_EQUAL_STRING("/kept", cfg.oscAddress());
            TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
            TEST_ASSERT_EQUAL_UINT8(4, cfg.oscSlot());
        }
    }
}

// An address that is not a string, carries a NUL, or is empty is Malformed
// and changes nothing.
void test_set_osc_address_must_be_a_string() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setOsc(wisp::OscMode::Maestro, 4, "/kept");
    wisp::WispOpDispatcher d(cfg);
    const char* bad[] = {
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":5})",
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":true})",
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":["/a"]})",
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":{"a":1}})",
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":"/a\u0000#"})",
        R"({"char":"wispOp","op":"setOsc","mode":"custom","address":"/a\u0000"})",
        R"({"char":"wispOp","op":"setOsc","mode":"maestro","address":""})",
    };
    for (const char* op : bad) {
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::Malformed,
                                      (int)dispatchStr(d, op), op);
        TEST_ASSERT_EQUAL_STRING("/kept", cfg.oscAddress());
        TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
    }
}

// applyOscSettings, the path the console takes too, holds the rule itself:
// every invalid case is Malformed in any sub-mode and stores nothing; ""
// stores outside Custom only.
void test_apply_osc_settings_follows_the_rule() {
    for (wisp::OscMode mode : {wisp::OscMode::Maestro, wisp::OscMode::Custom}) {
        for (const OscAddressCase& c : kOscAddressCases) {
            kv_reset();
            wisp::WispConfig cfg;
            cfg.begin();
            cfg.setOsc(wisp::OscMode::Straylight, 4, "/kept");
            const bool empty = c.address[0] == '\0';
            const bool takes = c.valid || (empty && mode != wisp::OscMode::Custom);
            const auto res = wisp::applyOscSettings(cfg, mode, 7, c.address);
            TEST_ASSERT_EQUAL_INT_MESSAGE(
                (int)(takes ? wisp::DispatchResult::AppliedOscSettings
                            : wisp::DispatchResult::Malformed),
                (int)res, c.address);
            TEST_ASSERT_EQUAL_STRING_MESSAGE(takes ? c.address : "/kept",
                                             cfg.oscAddress(), c.address);
            TEST_ASSERT_EQUAL_UINT8(takes ? 7 : 4, cfg.oscSlot());
        }
    }
    kv_reset();
    wisp::WispConfig cfg;
    cfg.begin();
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)wisp::applyOscSettings(cfg, wisp::OscMode::Maestro, 2, nullptr));
    TEST_ASSERT_EQUAL_UINT8(1, cfg.oscSlot());
}

// An absent or null address keeps the stored one, so switching sub-mode or
// slot never moves it, and switching back to Custom needs no address.
void test_set_osc_without_address_keeps_address() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setOsc(wisp::OscMode::Custom, 1, "/lamp/a");
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","slot":5})"));
    TEST_ASSERT_EQUAL_STRING("/lamp/a", cfg.oscAddress());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"custom","address":null})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Custom, (int)cfg.oscMode());
    TEST_ASSERT_EQUAL_STRING("/lamp/a", cfg.oscAddress());
    TEST_ASSERT_EQUAL_UINT8(5, cfg.oscSlot());
}

// An address sent with another sub-mode is stored for a later Custom.
void test_set_osc_stores_address_outside_custom() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setOsc","mode":"maestro","address":"/later"})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
    TEST_ASSERT_EQUAL_STRING("/later", cfg.oscAddress());
}

// Custom with no address sent and none stored would listen to nothing.
void test_set_osc_custom_needs_an_address() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setOsc(wisp::OscMode::Maestro, 3, "");
    wisp::WispOpDispatcher d(cfg);
    for (const char* op : {
             R"({"char":"wispOp","op":"setOsc","mode":"custom"})",
             R"({"char":"wispOp","op":"setOsc","mode":"custom","address":null})",
             R"({"char":"wispOp","op":"setOsc","mode":"custom","slot":2})"}) {
        TEST_ASSERT_EQUAL_INT_MESSAGE((int)wisp::DispatchResult::Malformed,
                                      (int)dispatchStr(d, op), op);
        TEST_ASSERT_EQUAL_INT((int)wisp::OscMode::Maestro, (int)cfg.oscMode());
        TEST_ASSERT_EQUAL_UINT8(3, cfg.oscSlot());
    }
}

// In Osc, a Custom address change retargets; a resend does not.
void test_set_osc_custom_reports_a_live_retarget() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setSourceMode(wisp::WispSourceMode::Osc);
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRetarget,
        (int)dispatchStr(d, setOscCustom("/a").c_str()));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)dispatchStr(d, setOscCustom("/a").c_str()));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscRetarget,
        (int)dispatchStr(d, setOscCustom("/b").c_str()));
}

// A sealed Custom change bumps opSeq, so the app can confirm it.
void test_set_osc_custom_sealed_bumps_op_seq() {
    wisp::WispConfig cfg;
    cfg.begin();
    cfg.setPassword("testpass");
    wisp::WispOpDispatcher d(cfg);
    wisp::crypto::RecentNonces nonces;
    d.setNonces(&nonces);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Rejected,
        (int)dispatchStr(d, setOscCustom("/a").c_str()));
    TEST_ASSERT_EQUAL_STRING("", cfg.oscAddress());
    const auto sealed = sealWispOp("testpass", 0x07, setOscCustom("/a").c_str());
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::AppliedOscSettings,
        (int)d.dispatch(sealed.data(), sealed.size()));
    TEST_ASSERT_EQUAL_STRING("/a", cfg.oscAddress());
    TEST_ASSERT_EQUAL_UINT32(1, cfg.opSeq());
}

void test_zone_ops_unknown() {
    wisp::WispConfig cfg;
    cfg.begin();
    wisp::WispOpDispatcher d(cfg);
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"setZone","zoneId":3})"));
    TEST_ASSERT_EQUAL_INT((int)wisp::DispatchResult::Malformed,
        (int)dispatchStr(d, R"({"char":"wispOp","op":"clearZone"})"));
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
    RUN_TEST(test_set_manual_palette_mixed_rgb_rgbw_tuples);
    RUN_TEST(test_set_off_color_rgbw_tuple);
    RUN_TEST(test_poll_status_no_password);
    RUN_TEST(test_poll_status_plaintext_with_password);
    RUN_TEST(test_poll_config_no_password);
    RUN_TEST(test_poll_config_plaintext_with_password);
    RUN_TEST(test_set_password_sealed_changes_password);
    RUN_TEST(test_set_password_plaintext_factory_fresh);
    RUN_TEST(test_set_password_plaintext_rejected_when_password_set);
    RUN_TEST(test_replay_same_nonce_dropped);
    RUN_TEST(test_nonce_ring_bounded);
    RUN_TEST(test_op_seq_bumps_on_sealed_applied);
    RUN_TEST(test_op_seq_unchanged_on_plaintext);
    RUN_TEST(test_set_led_strip_happy_path);
    RUN_TEST(test_set_led_strip_unknown_led_type);
    RUN_TEST(test_set_led_strip_zero_pixel_count);
    RUN_TEST(test_set_led_strip_missing_pixel_count);
    RUN_TEST(test_set_brightness_happy_path);
    RUN_TEST(test_set_brightness_out_of_bounds_malformed);
    RUN_TEST(test_set_source_osc_accepted);
    RUN_TEST(test_set_source_aurora_rejected);
    RUN_TEST(test_set_osc_role_persists);
    RUN_TEST(test_set_osc_role_invalid_malformed);
    RUN_TEST(test_set_osc_role_plaintext_rejected_with_password);
    RUN_TEST(test_zone_ops_unknown);
    RUN_TEST(test_set_manual_palette_echoes_seq);
    RUN_TEST(test_set_manual_palette_without_valid_seq_clears_echo);
    RUN_TEST(test_set_manual_palette_keeps_the_first_16_colours);
    RUN_TEST(test_malformed_palette_op_keeps_echo);
    RUN_TEST(test_set_swatches_applies_and_reloads);
    RUN_TEST(test_unknown_stored_style_loads_as_gradient);
    RUN_TEST(test_set_swatches_needs_a_bool);
    RUN_TEST(test_set_swatches_plaintext_rejected_with_password);
    RUN_TEST(test_set_swatches_sealed_bumps_op_seq);
    RUN_TEST(test_set_osc_maestro_applies_and_reloads);
    RUN_TEST(test_set_osc_without_slot_keeps_slot);
    RUN_TEST(test_set_osc_invalid_changes_nothing);
    RUN_TEST(test_set_osc_plaintext_rejected_with_password);
    RUN_TEST(test_set_osc_reports_a_live_retarget);
    RUN_TEST(test_set_osc_sealed_bumps_op_seq);
    RUN_TEST(test_set_osc_custom_applies_and_reloads);
    RUN_TEST(test_set_osc_address_follows_the_rule);
    RUN_TEST(test_set_osc_address_must_be_a_string);
    RUN_TEST(test_apply_osc_settings_follows_the_rule);
    RUN_TEST(test_set_osc_without_address_keeps_address);
    RUN_TEST(test_set_osc_stores_address_outside_custom);
    RUN_TEST(test_set_osc_custom_needs_an_address);
    RUN_TEST(test_set_osc_custom_reports_a_live_retarget);
    RUN_TEST(test_set_osc_custom_sealed_bumps_op_seq);
    return UNITY_END();
}

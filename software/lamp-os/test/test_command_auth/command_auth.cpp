// Native-host tests for command_auth (the HMAC-SHA256 trailer on MSG_EVENT /
// MSG_COMMAND). Uses the loadKeyForTest / clearKeyForTest seams to exercise
// both the keyed and keyless paths from one binary.
//
// Pins:
//   1. build-body + appendTag + parse round-trip: payload survives, tag stripped.
//   2. keyed verify() passes on a good tag, fails on a tampered body or tag.
//   3. keyless => appendTag writes zeros, verify() returns true (permissive).
//   4. EVENT / COMMAND MAX_PAYLOAD + tag fits in the 250-byte frame budget.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/protocol/command.hpp"
#include "components/network/protocol/event.hpp"

// Native tests don't build src/; compile the real implementation into this TU.
// The <mbedtls/md.h> it includes resolves to the HMAC shim in this test dir.
#include "../../src/components/network/protocol/command_auth.cpp"

namespace lp = lamp_protocol;
namespace ca = lamp_protocol::command_auth;

static const uint8_t kSrc[6]    = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kTarget[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};

static const uint8_t kKey[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

void setUp(void) { ca::clearKeyForTest(); }
void tearDown(void) { ca::clearKeyForTest(); }

// --- Round-trip ---

void test_event_roundtrip_tag_stripped() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  const char json[] = R"({"type":"glitchy"})";
  const size_t jsonLen = sizeof(json) - 1;

  const size_t body = lp::buildEvent(buf, sizeof(buf), 0xABCD, kSrc,
                                     reinterpret_cast<const uint8_t*>(json), jsonLen);
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_FIXED_SIZE + jsonLen, body);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(body + lp::EVENT_TAG_SIZE, framed);

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, framed, out));
  TEST_ASSERT_EQUAL_UINT32(jsonLen, out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(json),
                                out.payload, jsonLen);
  TEST_ASSERT_TRUE(ca::verify(buf, framed - lp::EVENT_TAG_SIZE, out.tag));
}

void test_command_roundtrip_tag_stripped() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD + lp::COMMAND_TAG_SIZE];
  const char json[] = R"({"type":"waltz"})";
  const size_t jsonLen = sizeof(json) - 1;

  const size_t body = lp::buildCommand(buf, sizeof(buf), 7, kSrc, kTarget,
                                       reinterpret_cast<const uint8_t*>(json), jsonLen);
  TEST_ASSERT_EQUAL_UINT32(lp::COMMAND_FIXED_SIZE + jsonLen, body);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));

  lp::ParsedCommand out;
  TEST_ASSERT_TRUE(lp::parseCommand(buf, framed, out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTarget, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT32(jsonLen, out.payloadLen);
  TEST_ASSERT_TRUE(ca::verify(buf, framed - lp::COMMAND_TAG_SIZE, out.tag));
}

// --- Keyed verify tamper detection ---

void test_keyed_verify_rejects_tampered_body() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  const uint8_t pay[4] = {'{', '}', 0, 0};
  const size_t body = lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, 2);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, framed, out));
  buf[lp::EVENT_FIXED_SIZE] ^= 0x01;  // flip a payload bit; out.tag unchanged
  TEST_ASSERT_FALSE(ca::verify(buf, framed - lp::EVENT_TAG_SIZE, out.tag));
}

void test_keyed_verify_rejects_tampered_tag() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  const uint8_t pay[4] = {'{', '}', 0, 0};
  const size_t body = lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, 2);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));

  buf[framed - 1] ^= 0x80;  // corrupt last tag byte
  uint8_t bad[lp::EVENT_TAG_SIZE];
  std::memcpy(bad, &buf[framed - lp::EVENT_TAG_SIZE], lp::EVENT_TAG_SIZE);
  TEST_ASSERT_FALSE(ca::verify(buf, body, bad));
}

// --- Keyless permissiveness ---

void test_keyless_appendtag_writes_zeros() {
  ca::clearKeyForTest();
  TEST_ASSERT_FALSE(ca::enabled());
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  const uint8_t pay[4] = {'{', '}', 0, 0};
  const size_t body = lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, 2);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(body + lp::EVENT_TAG_SIZE, framed);
  for (size_t i = 0; i < lp::EVENT_TAG_SIZE; ++i)
    TEST_ASSERT_EQUAL_UINT8(0, buf[body + i]);
}

void test_keyless_verify_permissive() {
  ca::clearKeyForTest();
  const uint8_t body[3] = {1, 2, 3};
  const uint8_t tag[lp::EVENT_TAG_SIZE] = {9, 9, 9, 9, 9, 9, 9, 9};  // garbage
  TEST_ASSERT_TRUE(ca::verify(body, sizeof(body), tag));
}

// --- MAX_PAYLOAD boundary ---

void test_event_max_payload_fits() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  TEST_ASSERT_TRUE(sizeof(buf) <= 250);
  uint8_t pay[lp::EVENT_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof(pay); ++i) pay[i] = static_cast<uint8_t>(i);
  const size_t body = lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, sizeof(pay));
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(sizeof(buf), framed);
  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, framed, out));
  TEST_ASSERT_TRUE(ca::verify(buf, framed - lp::EVENT_TAG_SIZE, out.tag));
}

void test_command_max_payload_fits() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD + lp::COMMAND_TAG_SIZE];
  TEST_ASSERT_TRUE(sizeof(buf) <= lp::ESPNOW_V2_FRAME_MAX);
  uint8_t pay[lp::COMMAND_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof(pay); ++i) pay[i] = static_cast<uint8_t>(i);
  const size_t body = lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget, pay, sizeof(pay));
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  TEST_ASSERT_EQUAL_UINT32(sizeof(buf), framed);
  lp::ParsedCommand out;
  TEST_ASSERT_TRUE(lp::parseCommand(buf, framed, out));
  TEST_ASSERT_TRUE(ca::verify(buf, framed - lp::COMMAND_TAG_SIZE, out.tag));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_event_roundtrip_tag_stripped);
  RUN_TEST(test_command_roundtrip_tag_stripped);
  RUN_TEST(test_keyed_verify_rejects_tampered_body);
  RUN_TEST(test_keyed_verify_rejects_tampered_tag);
  RUN_TEST(test_keyless_appendtag_writes_zeros);
  RUN_TEST(test_keyless_verify_permissive);
  RUN_TEST(test_event_max_payload_fits);
  RUN_TEST(test_command_max_payload_fits);
  return UNITY_END();
}

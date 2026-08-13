// Native-host round-trip tests for MSG_EVENT (0x30).
//
// Pins:
//   1. build → parse round-trip: sourceMac + payload survive.
//   2. parseEvent rejects frames that are too short, too long, or wrong msgType.
//   3. Size constants lock-in.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/protocol/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

// Append a keyless (zero) command_auth tag so parseEvent accepts the frame.
static size_t frame(uint8_t* buf, size_t body) {
  std::memset(buf + body, 0, lp::EVENT_TAG_SIZE);
  return body + lp::EVENT_TAG_SIZE;
}

static const uint8_t kSrc[6]  = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kOther[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

// --- Round-trip ---

void test_roundtrip_payload_survives() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  const char json[] = R"({"type":"glitchy","target":"shade"})";
  const size_t jsonLen = sizeof(json) - 1;

  const size_t body = lp::buildEvent(buf, sizeof(buf), 0xABCD,
                                  kSrc,
                                  reinterpret_cast<const uint8_t*>(json),
                                  jsonLen);
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_FIXED_SIZE + jsonLen, body);
  const size_t n = frame(buf, body);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_EVENT, lp::inspect(buf, n));

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0xABCD, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT32(jsonLen, out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(json),
                                 out.payload, jsonLen);
}

void test_roundtrip_max_payload() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE];
  uint8_t payload[lp::EVENT_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);

  const size_t body = lp::buildEvent(buf, sizeof(buf), 1, kSrc,
                                  payload, lp::EVENT_MAX_PAYLOAD);
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD, body);
  const size_t n = frame(buf, body);

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_MAX_PAYLOAD, out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload, lp::EVENT_MAX_PAYLOAD);
}

void test_sourcemac_survives() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + 2 + lp::EVENT_TAG_SIZE];
  const uint8_t pay[2] = {'{', '}'};
  const size_t n = frame(buf, lp::buildEvent(buf, sizeof(buf), 5, kOther, pay, 2));
  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kOther, out.sourceMac, 6);
}

// --- Rejection cases ---

void test_parse_rejects_zero_payload() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + 1];
  const uint8_t dummy[1] = {'{'};
  lp::buildEvent(buf, sizeof(buf), 1, kSrc, dummy, 1);
  lp::ParsedEvent out;
  TEST_ASSERT_FALSE(lp::parseEvent(buf, lp::EVENT_FIXED_SIZE, out));
}

void test_build_rejects_zero_payload() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD];
  const uint8_t dummy[1] = {0};
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildEvent(buf, sizeof(buf), 1, kSrc, dummy, 0));
}

void test_build_rejects_oversized_payload() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + 1];
  uint8_t big[lp::EVENT_MAX_PAYLOAD + 1];
  std::memset(big, 0, sizeof(big));
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildEvent(buf, sizeof(buf), 1, kSrc,
                     big, lp::EVENT_MAX_PAYLOAD + 1));
}

void test_parse_rejects_wrong_msg_type() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + 2 + lp::EVENT_TAG_SIZE];
  const uint8_t pay[2] = {'{', '}'};
  const size_t n = frame(buf, lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, 2));
  buf[3] = lp::MSG_COMMAND;  // wrong type
  lp::ParsedEvent out;
  TEST_ASSERT_FALSE(lp::parseEvent(buf, n, out));
}

void test_parse_rejects_oversized_frame() {
  uint8_t buf[lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE + 1];
  const uint8_t pay[1] = {'{'};
  lp::buildEvent(buf, sizeof(buf), 1, kSrc, pay, 1);
  lp::ParsedEvent out;
  TEST_ASSERT_FALSE(lp::parseEvent(buf,
      lp::EVENT_FIXED_SIZE + lp::EVENT_MAX_PAYLOAD + lp::EVENT_TAG_SIZE + 1, out));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_payload_survives);
  RUN_TEST(test_roundtrip_max_payload);
  RUN_TEST(test_sourcemac_survives);
  RUN_TEST(test_parse_rejects_zero_payload);
  RUN_TEST(test_build_rejects_zero_payload);
  RUN_TEST(test_build_rejects_oversized_payload);
  RUN_TEST(test_parse_rejects_wrong_msg_type);
  RUN_TEST(test_parse_rejects_oversized_frame);
  return UNITY_END();
}

// Native-host round-trip tests for MSG_COMMAND (0x31).
//
// Pins:
//   1. build → parse round-trip: targetMac + sourceMac + payload survive.
//   2. addressedToUs filter: exact MAC match, broadcast FF:FF:..., non-target drops.
//   3. parseCommand rejects frames that are too short, too long, or wrong msgType.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/protocol/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

static const uint8_t kSrc[6]    = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kTarget[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
static const uint8_t kOther[6]  = {0xBB, 0xBB, 0xBB, 0xBB, 0xBB, 0xBB};
static const uint8_t kBcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// addressedToUs mirror (matches mesh_link.cpp).
static bool addressedToUs(const uint8_t targetMac[6], const uint8_t myMac[6]) {
  return std::memcmp(targetMac, myMac, 6) == 0 ||
         std::memcmp(targetMac, kBcast, 6) == 0;
}

// --- Round-trip ---

void test_roundtrip_payload_survives() {
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD];
  const char json[] = R"({"type":"pulse","target":"base"})";
  const size_t jsonLen = sizeof(json) - 1;

  const size_t n = lp::buildCommand(buf, sizeof(buf), 0x1234,
                                    kSrc, kTarget,
                                    reinterpret_cast<const uint8_t*>(json),
                                    jsonLen);
  TEST_ASSERT_EQUAL_UINT32(lp::COMMAND_FIXED_SIZE + jsonLen, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_COMMAND, lp::inspect(buf, n));

  lp::ParsedCommand out;
  TEST_ASSERT_TRUE(lp::parseCommand(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x1234, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc,    out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTarget, out.targetMac, 6);
  TEST_ASSERT_EQUAL_UINT32(jsonLen, out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reinterpret_cast<const uint8_t*>(json),
                                 out.payload, jsonLen);
}

void test_roundtrip_max_payload() {
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD];
  uint8_t payload[lp::COMMAND_MAX_PAYLOAD];
  for (size_t i = 0; i < sizeof(payload); ++i) payload[i] = static_cast<uint8_t>(i);

  const size_t n = lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget,
                                    payload, lp::COMMAND_MAX_PAYLOAD);
  TEST_ASSERT_EQUAL_UINT32(lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD, n);

  lp::ParsedCommand out;
  TEST_ASSERT_TRUE(lp::parseCommand(buf, n, out));
  TEST_ASSERT_EQUAL_UINT32(lp::COMMAND_MAX_PAYLOAD, out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload, lp::COMMAND_MAX_PAYLOAD);
}

// --- addressedToUs filter ---

void test_addressed_to_us_exact_mac() {
  TEST_ASSERT_TRUE(addressedToUs(kTarget, kTarget));
}

void test_addressed_to_us_broadcast() {
  TEST_ASSERT_TRUE(addressedToUs(kBcast, kTarget));
}

void test_not_addressed_to_us() {
  TEST_ASSERT_FALSE(addressedToUs(kOther, kTarget));
}

// --- Rejection cases ---

void test_parse_rejects_zero_payload() {
  uint8_t buf[lp::COMMAND_FIXED_SIZE + 1];
  const uint8_t dummy[1] = {'{' };
  // Build with 1-byte payload, then manually truncate to fixed size only.
  lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget, dummy, 1);
  lp::ParsedCommand out;
  TEST_ASSERT_FALSE(lp::parseCommand(buf, lp::COMMAND_FIXED_SIZE, out));
}

void test_build_rejects_zero_payload() {
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD];
  const uint8_t dummy[1] = {0};
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget, dummy, 0));
}

void test_build_rejects_oversized_payload() {
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD + 1];
  uint8_t big[lp::COMMAND_MAX_PAYLOAD + 1];
  std::memset(big, 0, sizeof(big));
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget,
                       big, lp::COMMAND_MAX_PAYLOAD + 1));
}

void test_parse_rejects_wrong_msg_type() {
  // Build a valid COMMAND frame then corrupt the msgType byte.
  uint8_t buf[lp::COMMAND_FIXED_SIZE + 4];
  const uint8_t pay[4] = {'{', '}', 0, 0};
  lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget, pay, 2);
  buf[3] = lp::MSG_EVENT;  // wrong type
  lp::ParsedCommand out;
  TEST_ASSERT_FALSE(lp::parseCommand(buf, lp::COMMAND_FIXED_SIZE + 2, out));
}

void test_parse_rejects_oversized_frame() {
  // Claim a frame larger than COMMAND_FIXED_SIZE + COMMAND_MAX_PAYLOAD.
  uint8_t buf[lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD + 1];
  const uint8_t pay[1] = {'{' };
  lp::buildCommand(buf, sizeof(buf), 1, kSrc, kTarget, pay, 1);
  lp::ParsedCommand out;
  TEST_ASSERT_FALSE(lp::parseCommand(buf,
      lp::COMMAND_FIXED_SIZE + lp::COMMAND_MAX_PAYLOAD + 1, out));
}

// --- Size lock-in ---

void test_command_size_constants() {
  TEST_ASSERT_EQUAL_UINT32(18,  lp::COMMAND_FIXED_SIZE);
  TEST_ASSERT_EQUAL_UINT32(232, lp::COMMAND_MAX_PAYLOAD);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_payload_survives);
  RUN_TEST(test_roundtrip_max_payload);
  RUN_TEST(test_addressed_to_us_exact_mac);
  RUN_TEST(test_addressed_to_us_broadcast);
  RUN_TEST(test_not_addressed_to_us);
  RUN_TEST(test_parse_rejects_zero_payload);
  RUN_TEST(test_build_rejects_zero_payload);
  RUN_TEST(test_build_rejects_oversized_payload);
  RUN_TEST(test_parse_rejects_wrong_msg_type);
  RUN_TEST(test_parse_rejects_oversized_frame);
  RUN_TEST(test_command_size_constants);
  return UNITY_END();
}

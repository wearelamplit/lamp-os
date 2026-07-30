// Native-host round-trip + auth tests for MSG_BID (0x34).
//
// Pins:
//   1. build → parse round-trip: sourceMac + bidType survive; size lock.
//   2. parseBid rejects wrong msgType / wrong length.
//   3. keyed command_auth: verify passes on a good tag, fails on tamper.
//   4. unknown-type-skip: inspect() surfaces 0x34 verbatim; an out-of-range
//      version drops the frame at inspect (the recv-dispatch fall-through).
//   5. sender cooldown mirror (mesh_link.cpp is Arduino-coupled, not native).

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/protocol/lamp_protocol.hpp"

#include "../../src/components/network/protocol/command_auth.cpp"

namespace lp = lamp_protocol;
namespace ca = lamp_protocol::command_auth;

void setUp(void) { ca::clearKeyForTest(); }
void tearDown(void) { ca::clearKeyForTest(); }

static const uint8_t kSrc[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kKey[32] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

// Append a keyless (zero) tag so parseBid accepts the frame.
static size_t frameKeyless(uint8_t* buf, size_t body) {
  std::memset(buf + body, 0, lp::BID_TAG_SIZE);
  return body + lp::BID_TAG_SIZE;
}

// --- Round-trip ---

void test_roundtrip_fields_survive() {
  uint8_t buf[lp::BID_SIZE];
  const size_t body = lp::buildBid(buf, sizeof(buf), 0xABCD, kSrc, lp::BID_GREETING);
  TEST_ASSERT_EQUAL_UINT32(lp::BID_FIXED_SIZE, body);
  const size_t n = frameKeyless(buf, body);
  TEST_ASSERT_EQUAL_UINT32(lp::BID_SIZE, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_BID, lp::inspect(buf, n));

  lp::ParsedBid out;
  TEST_ASSERT_TRUE(lp::parseBid(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0xABCD, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrc, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8(lp::BID_GREETING, out.bidType);
}

void test_size_locks() {
  TEST_ASSERT_EQUAL_UINT32(13, lp::BID_FIXED_SIZE);
  TEST_ASSERT_EQUAL_UINT32(8,  lp::BID_TAG_SIZE);
  TEST_ASSERT_EQUAL_UINT32(21, lp::BID_SIZE);
}

// --- Rejection cases ---

void test_parse_rejects_wrong_msg_type() {
  uint8_t buf[lp::BID_SIZE];
  const size_t n = frameKeyless(buf, lp::buildBid(buf, sizeof(buf), 1, kSrc, lp::BID_GREETING));
  buf[3] = lp::MSG_EVENT;  // wrong type
  lp::ParsedBid out;
  TEST_ASSERT_FALSE(lp::parseBid(buf, n, out));
}

void test_parse_rejects_wrong_length() {
  uint8_t buf[lp::BID_SIZE + 1];
  frameKeyless(buf, lp::buildBid(buf, sizeof(buf), 1, kSrc, lp::BID_GREETING));
  lp::ParsedBid out;
  TEST_ASSERT_FALSE(lp::parseBid(buf, lp::BID_SIZE - 1, out));  // short
  TEST_ASSERT_FALSE(lp::parseBid(buf, lp::BID_SIZE + 1, out));  // long
}

// --- Auth ---

void test_keyed_verify_accepts_good_tag() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::BID_SIZE];
  const size_t body = lp::buildBid(buf, sizeof(buf), 7, kSrc, lp::BID_GREETING);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  lp::ParsedBid out;
  TEST_ASSERT_TRUE(lp::parseBid(buf, framed, out));
  TEST_ASSERT_TRUE(ca::verify(buf, framed - lp::BID_TAG_SIZE, out.tag));
}

void test_keyed_verify_rejects_tampered_body() {
  ca::loadKeyForTest(kKey);
  uint8_t buf[lp::BID_SIZE];
  const size_t body = lp::buildBid(buf, sizeof(buf), 7, kSrc, lp::BID_GREETING);
  const size_t framed = ca::appendTag(buf, body, sizeof(buf));
  buf[12] ^= 0xFF;  // flip the bidType byte after tagging
  lp::ParsedBid out;
  TEST_ASSERT_TRUE(lp::parseBid(buf, framed, out));
  TEST_ASSERT_FALSE(ca::verify(buf, framed - lp::BID_TAG_SIZE, out.tag));
}

// --- Forward-compat: an unknown 0x34 at a version outside RX range drops at
// inspect(); at an in-range version it surfaces verbatim (recv dispatch that
// doesn't know 0x34 falls through and drops it). ---

void test_inspect_surfaces_bid_type_in_range() {
  uint8_t buf[lp::BID_SIZE];
  frameKeyless(buf, lp::buildBid(buf, sizeof(buf), 1, kSrc, lp::BID_GREETING));
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_BID, lp::inspect(buf, lp::BID_SIZE));
}

void test_inspect_drops_out_of_range_version() {
  uint8_t buf[lp::BID_SIZE];
  frameKeyless(buf, lp::buildBid(buf, sizeof(buf), 1, kSrc, lp::BID_GREETING));
  buf[2] = lp::PROTOCOL_VERSION_RX_MAX + 1;
  TEST_ASSERT_EQUAL_UINT8(0, lp::inspect(buf, lp::BID_SIZE));
}

// --- Sender cooldown (mirror of mesh_link.cpp sendBid; keep in sync) ---

namespace {
constexpr uint32_t kBidSendCooldownMs = 3000;

// Mirrors sendBid's gate: first send always passes; a send within the window
// of the last is suppressed.
struct SendCooldown {
  uint32_t lastMs = 0;
  bool allow(uint32_t nowMs) {
    if (lastMs != 0 && (nowMs - lastMs) < kBidSendCooldownMs) return false;
    lastMs = nowMs;
    return true;
  }
};
}  // namespace

void test_sender_cooldown_suppresses_back_to_back() {
  SendCooldown c;
  TEST_ASSERT_TRUE(c.allow(1000));            // first passes
  TEST_ASSERT_FALSE(c.allow(1500));           // within window
  TEST_ASSERT_FALSE(c.allow(1000 + kBidSendCooldownMs - 1));
  TEST_ASSERT_TRUE(c.allow(1000 + kBidSendCooldownMs));  // window elapsed
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_fields_survive);
  RUN_TEST(test_size_locks);
  RUN_TEST(test_parse_rejects_wrong_msg_type);
  RUN_TEST(test_parse_rejects_wrong_length);
  RUN_TEST(test_keyed_verify_accepts_good_tag);
  RUN_TEST(test_keyed_verify_rejects_tampered_body);
  RUN_TEST(test_inspect_surfaces_bid_type_in_range);
  RUN_TEST(test_inspect_drops_out_of_range_version);
  RUN_TEST(test_sender_cooldown_suppresses_back_to_back);
  return UNITY_END();
}

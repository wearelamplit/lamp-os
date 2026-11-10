// Native tests for ControlOpInbox, the wisp's single pending CONTROL_OP slot.
//
// Pins the contract:
//   - an app op addressed to this wisp is taken once.
//   - a peer wisp's broadcast status, arriving before the loop takes the app
//     op, leaves the app op pending.
//   - this wisp's own broadcast relayed back leaves the app op pending.
//   - the app op's resend copies (same seq) never re-post it.
//   - a newer op addressed to this wisp replaces one not yet taken.
//   - before the self MAC is set no op addressed to the wisp is admitted.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "net/control_op_inbox.hpp"

namespace {

const uint8_t kSelf[6]      = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t kPeerWisp[6]  = {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};
const uint8_t kLamp[6]      = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
const uint8_t kBroadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

const char kAppOp[]     = R"({"char":"wispOp","op":"setSource","mode":"manual"})";
const char kAppOp2[]    = R"({"char":"wispOp","op":"setSource","mode":"off"})";
const char kPeerStatus[] = R"({"char":"wispStatus","name":"wisp-b","src":"osc"})";

lamp_protocol::ParsedControlOp op(const uint8_t target[6],
                                  const uint8_t source[6], uint16_t seq,
                                  const char* json) {
  lamp_protocol::ParsedControlOp o{};
  o.seq = seq;
  std::memcpy(o.targetMac, target, 6);
  std::memcpy(o.sourceMac, source, 6);
  o.payloadLen = static_cast<uint16_t>(std::strlen(json));
  o.payload = reinterpret_cast<const uint8_t*>(json);
  return o;
}

void assertTakes(wisp::ControlOpInbox& inbox, const char* json) {
  uint8_t out[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t len = 0;
  TEST_ASSERT_TRUE(inbox.take(out, len));
  TEST_ASSERT_EQUAL_UINT16(std::strlen(json), len);
  TEST_ASSERT_EQUAL_MEMORY(json, out, len);
}

void assertEmpty(wisp::ControlOpInbox& inbox) {
  uint8_t out[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t len = 0;
  TEST_ASSERT_FALSE(inbox.take(out, len));
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_app_op_is_taken_once(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  assertTakes(inbox, kAppOp);
  assertEmpty(inbox);
}

void test_peer_wisp_broadcast_leaves_app_op_pending(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  inbox.offer(op(kBroadcast, kPeerWisp, 900, kPeerStatus));
  inbox.offer(op(kBroadcast, kPeerWisp, 901, kPeerStatus));
  assertTakes(inbox, kAppOp);
  assertEmpty(inbox);
}

void test_own_broadcast_echo_leaves_app_op_pending(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  inbox.offer(op(kBroadcast, kSelf, 77, kPeerStatus));
  assertTakes(inbox, kAppOp);
}

void test_resend_copies_do_not_repost(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  assertTakes(inbox, kAppOp);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  assertEmpty(inbox);
}

void test_newer_app_op_replaces_untaken_one(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  inbox.offer(op(kSelf, kLamp, 42, kAppOp2));
  assertTakes(inbox, kAppOp2);
  assertEmpty(inbox);
}

void test_nothing_admitted_before_self_mac(void) {
  wisp::ControlOpInbox inbox;
  inbox.offer(op(kSelf, kLamp, 41, kAppOp));
  inbox.offer(op(kBroadcast, kPeerWisp, 900, kPeerStatus));
  assertEmpty(inbox);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_app_op_is_taken_once);
  RUN_TEST(test_peer_wisp_broadcast_leaves_app_op_pending);
  RUN_TEST(test_own_broadcast_echo_leaves_app_op_pending);
  RUN_TEST(test_resend_copies_do_not_repost);
  RUN_TEST(test_newer_app_op_replaces_untaken_one);
  RUN_TEST(test_nothing_admitted_before_self_mac);
  return UNITY_END();
}

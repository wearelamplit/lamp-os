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
//   - a sender whose seq restarts at boot gets through once its old tuples
//     pass the dedup age, and not before.

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

// Off zero and near the top of millis() so an age that ignored wrap would fail.
constexpr uint32_t kT0 = 0xFFFFC000u;

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
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  assertTakes(inbox, kAppOp);
  assertEmpty(inbox);
}

void test_peer_wisp_broadcast_leaves_app_op_pending(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  inbox.offer(op(kBroadcast, kPeerWisp, 900, kPeerStatus), kT0);
  inbox.offer(op(kBroadcast, kPeerWisp, 901, kPeerStatus), kT0);
  assertTakes(inbox, kAppOp);
  assertEmpty(inbox);
}

void test_own_broadcast_echo_leaves_app_op_pending(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  inbox.offer(op(kBroadcast, kSelf, 77, kPeerStatus), kT0);
  assertTakes(inbox, kAppOp);
}

void test_resend_copies_do_not_repost(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  assertTakes(inbox, kAppOp);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  assertEmpty(inbox);
}

void test_newer_app_op_replaces_untaken_one(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  inbox.offer(op(kSelf, kLamp, 42, kAppOp2), kT0);
  assertTakes(inbox, kAppOp2);
  assertEmpty(inbox);
}

void test_nothing_admitted_before_self_mac(void) {
  wisp::ControlOpInbox inbox;
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  inbox.offer(op(kBroadcast, kPeerWisp, 900, kPeerStatus), kT0);
  assertEmpty(inbox);
}

// The lamp's ops before a reboot fill the ring with seq 0..13; after the reboot
// its seq restarts at 0 and the same tuples arrive again.
void fillWithPreRebootOps(wisp::ControlOpInbox& inbox) {
  uint8_t out[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t len = 0;
  for (uint16_t seq = 0; seq < 14; ++seq) {
    inbox.offer(op(kSelf, kLamp, seq, kAppOp), kT0 + seq * 1000u);
    TEST_ASSERT_TRUE(inbox.take(out, len));
  }
}

void test_restarted_sender_admitted_after_dedup_age(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  fillWithPreRebootOps(inbox);
  const uint32_t lastSeen = kT0 + 13u * 1000u;
  inbox.offer(op(kSelf, kLamp, 13, kAppOp2),
              lastSeen + lamp_protocol::kDedupMaxAgeMs + 1);
  assertTakes(inbox, kAppOp2);
}

void test_restarted_sender_held_inside_dedup_age(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  fillWithPreRebootOps(inbox);
  const uint32_t lastSeen = kT0 + 13u * 1000u;
  inbox.offer(op(kSelf, kLamp, 13, kAppOp2),
              lastSeen + lamp_protocol::kDedupMaxAgeMs);
  assertEmpty(inbox);
}

void test_resend_copy_late_in_window_does_not_repost(void) {
  wisp::ControlOpInbox inbox;
  inbox.setSelfMac(kSelf);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0);
  assertTakes(inbox, kAppOp);
  inbox.offer(op(kSelf, kLamp, 41, kAppOp), kT0 + 7000);
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
  RUN_TEST(test_restarted_sender_admitted_after_dedup_age);
  RUN_TEST(test_restarted_sender_held_inside_dedup_age);
  RUN_TEST(test_resend_copy_late_in_window_does_not_repost);
  return UNITY_END();
}

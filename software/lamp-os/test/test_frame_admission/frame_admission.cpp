// Pins MeshLink's receive-side admission: which frames may take a slot in a
// DedupRing. A frame for another lamp, or this lamp's own echoed HELLO, never
// does, so it cannot evict a tuple that dedups this lamp's real traffic.

#include <unity.h>

#include <cstdint>

#include "components/network/mesh/frame_admission.hpp"  // -I src on native
#include "components/network/protocol/fw_ota.hpp"

namespace lp = lamp_protocol;
using lamp::admitAddressed;
using lamp::admitHello;
using lamp::HelloAdmit;

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kMe[6]     = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
const uint8_t kOther[6]  = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
const uint8_t kBcast[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
const uint8_t kSender[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
}  // namespace

void test_addressed_frame_admitted_once() {
  lp::DedupRing<16> ring;
  TEST_ASSERT_TRUE(admitAddressed(ring, kSender, lp::MSG_FW_OFFER, 7, kMe, kMe, 1000));
  TEST_ASSERT_FALSE(admitAddressed(ring, kSender, lp::MSG_FW_OFFER, 7, kMe, kMe, 1200));
  TEST_ASSERT_TRUE(admitAddressed(ring, kSender, lp::MSG_FW_OFFER, 8, kBcast, kMe, 1400));
}

// A neighbour session's full ring of chunks for another lamp, between an OFFER
// for this lamp and its retry: the retry must still read as a repeat.
void test_frames_for_another_lamp_take_no_slot() {
  lp::DedupRing<16> ring;
  TEST_ASSERT_TRUE(admitAddressed(ring, kSender, lp::MSG_FW_OFFER, 7, kMe, kMe, 1000));
  for (uint16_t seq = 100; seq < 100 + 16; ++seq) {
    TEST_ASSERT_FALSE(
        admitAddressed(ring, kOther, lp::MSG_FW_CHUNK, seq, kOther, kMe, 1050));
  }
  TEST_ASSERT_FALSE(admitAddressed(ring, kSender, lp::MSG_FW_OFFER, 7, kMe, kMe, 1200));
}

void test_hello_from_peer_fresh_then_repeat() {
  lp::DedupRing<4> ring;
  TEST_ASSERT_EQUAL(HelloAdmit::Fresh, admitHello(ring, kSender, 42, kMe, 1000));
  TEST_ASSERT_EQUAL(HelloAdmit::Repeat, admitHello(ring, kSender, 42, kMe, 1100));
}

// Two relayers echo this lamp's HELLOs back into a two-slot ring holding one
// peer tuple: the echoes classify Own and the peer's repeat still dedups.
void test_own_echoed_hello_takes_no_slot() {
  lp::DedupRing<2> ring;
  TEST_ASSERT_EQUAL(HelloAdmit::Fresh, admitHello(ring, kSender, 42, kMe, 1000));
  TEST_ASSERT_EQUAL(HelloAdmit::Own, admitHello(ring, kMe, 3, kMe, 1050));
  TEST_ASSERT_EQUAL(HelloAdmit::Own, admitHello(ring, kMe, 4, kMe, 1060));
  TEST_ASSERT_EQUAL(HelloAdmit::Own, admitHello(ring, kMe, 4, kMe, 1070));
  TEST_ASSERT_EQUAL(HelloAdmit::Repeat, admitHello(ring, kSender, 42, kMe, 1100));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_addressed_frame_admitted_once);
  RUN_TEST(test_frames_for_another_lamp_take_no_slot);
  RUN_TEST(test_hello_from_peer_fresh_then_repeat);
  RUN_TEST(test_own_echoed_hello_takes_no_slot);
  return UNITY_END();
}

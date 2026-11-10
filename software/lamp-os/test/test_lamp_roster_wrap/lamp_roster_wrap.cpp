// Native proof that roster eviction and pruning judge a peer by its most
// recent sighting across the 32-bit millis() wrap.

#include <unity.h>

#include <cstdint>
#include <cstring>

// Native-test seam: exercise the shipped class.
#include "components/network/mesh/lamp_roster.cpp"

using namespace lamp;

namespace {

const Color kNoColor = Color();
constexpr uint32_t kMaxAgeMs = 240000;

void macFor(uint8_t i, uint8_t out[6]) {
  const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x07, i};
  std::memcpy(out, mac, 6);
}

void hello(LampRoster& r, uint8_t i) {
  uint8_t mac[6];
  macFor(i, mac);
  r.addOrUpdateFromEspNow("peer", mac, kNoColor, kNoColor, 0x010203, 0, 5,
                          nullptr, nullptr, false, 0, false, nullptr, false,
                          -60);
}

bool listed(LampRoster& r, uint8_t i) {
  uint8_t mac[6];
  macFor(i, mac);
  for (const auto& p : r.getAll()) {
    if (std::memcmp(p.mac, mac, 6) == 0) return true;
  }
  return false;
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// A full roster heard before the wrap; peer 0, the first heard, is heard again
// just after it. The next newcomer must evict peer 1, now the stalest, and
// keep peer 0.
void test_eviction_keeps_a_peer_heard_after_the_wrap() {
  LampRoster r;
  const uint32_t start = UINT32_MAX - 100000;
  for (uint8_t i = 0; i < LampRoster::kCapacity; i++) {
    set_mock_millis(start + i * 10u);
    hello(r, i);
  }
  set_mock_millis(100);
  hello(r, 0);
  set_mock_millis(200);
  hello(r, LampRoster::kCapacity);

  TEST_ASSERT_TRUE(listed(r, 0));
  TEST_ASSERT_FALSE(listed(r, 1));
  TEST_ASSERT_TRUE(listed(r, LampRoster::kCapacity));
}

// Seen nearby long before the wrap, heard over the mesh just after it: the
// mesh sighting is the recent one, so prune keeps the peer.
void test_prune_keeps_a_peer_heard_after_the_wrap() {
  LampRoster r;
  uint8_t mac[6];
  macFor(1, mac);
  set_mock_millis(UINT32_MAX - (kMaxAgeMs + 60000));
  hello(r, 1);
  r.markNear(mac);
  set_mock_millis(500);
  hello(r, 1);

  set_mock_millis(1000);
  r.prune(kMaxAgeMs);
  TEST_ASSERT_TRUE(listed(r, 1));

  set_mock_millis(500 + kMaxAgeMs + 1);
  r.prune(kMaxAgeMs);
  TEST_ASSERT_FALSE(listed(r, 1));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_eviction_keeps_a_peer_heard_after_the_wrap);
  RUN_TEST(test_prune_keeps_a_peer_heard_after_the_wrap);
  return UNITY_END();
}

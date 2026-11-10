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

// Steps the clock from `fromMs` to just short of one full wrap later, a prune
// and retire every 2^28 ms (about three days), running `keep` before each so
// the peer stays listed on its other transport.
template <typename Keep>
void pruneThroughAWrap(LampRoster& r, uint32_t fromMs, Keep keep) {
  for (uint64_t t = fromMs + (1ull << 28); t < fromMs + (1ull << 32); t += 1ull << 28) {
    set_mock_millis(static_cast<uint32_t>(t));
    keep();
    r.prune(kMaxAgeMs);
    r.retireSightings(static_cast<uint32_t>(t));
  }
}

bool meshListed(LampRoster& r, uint8_t i) {
  uint8_t mac[6];
  macFor(i, mac);
  for (const auto& p : r.getMesh(kMaxAgeMs)) {
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

// Seen nearby once, then heard only over the mesh for a whole wrap: one wrap
// on, the old near sighting must not read as 5 s old.
void test_near_sighting_stays_stale_a_wrap_later() {
  LampRoster r;
  uint8_t mac[6];
  macFor(1, mac);
  const uint32_t seenMs = 1000;
  set_mock_millis(seenMs);
  hello(r, 1);
  r.markNear(mac);
  pruneThroughAWrap(r, seenMs, [&] { hello(r, 1); });

  set_mock_millis(seenMs + 5000);
  hello(r, 1);
  TEST_ASSERT_TRUE(listed(r, 1));
  TEST_ASSERT_EQUAL_size_t(0, r.snapshotNear(kMaxAgeMs).size());
}

// Heard over the mesh once, then only seen nearby for a whole wrap.
void test_mesh_sighting_stays_stale_a_wrap_later() {
  LampRoster r;
  uint8_t mac[6];
  macFor(2, mac);
  const uint32_t heardMs = 2000;
  set_mock_millis(heardMs);
  hello(r, 2);
  r.markNear(mac);
  pruneThroughAWrap(r, heardMs, [&] { r.markNear(mac); });

  set_mock_millis(heardMs + 5000);
  r.markNear(mac);
  TEST_ASSERT_TRUE(listed(r, 2));
  TEST_ASSERT_FALSE(meshListed(r, 2));
}

// A BT sighting once, then near only through direct HELLOs for a whole wrap:
// the BT stamp must read as old, not 5 s.
void test_bt_sighting_stays_stale_a_wrap_later() {
  LampRoster r;
  const uint8_t ble[6] = {0x02, 0x00, 0x00, 0x00, 0x07, 0x33};
  const uint32_t seenMs = 3000;
  set_mock_millis(seenMs);
  r.addOrUpdateFromBle("peer", ble, kNoColor, kNoColor, -50);
  uint8_t mac[6];
  std::memcpy(mac, r.snapshotNear(kMaxAgeMs).front().mac, 6);
  pruneThroughAWrap(r, seenMs, [&] { r.markNear(mac); });

  const uint32_t nowMs = seenMs + 5000;
  set_mock_millis(nowMs);
  r.markNear(mac);
  const auto& near = r.snapshotNear(kMaxAgeMs);
  TEST_ASSERT_EQUAL_size_t(1, near.size());
  TEST_ASSERT_TRUE(lampos::msSince(nowMs, near.front().lastSeenBleMs) > kMaxAgeMs);
  // Never heard over the mesh: retiring keeps that a "never".
  TEST_ASSERT_EQUAL_UINT32(0, r.getAll().front().lastSeenMeshMs);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_eviction_keeps_a_peer_heard_after_the_wrap);
  RUN_TEST(test_prune_keeps_a_peer_heard_after_the_wrap);
  RUN_TEST(test_near_sighting_stays_stale_a_wrap_later);
  RUN_TEST(test_mesh_sighting_stays_stale_a_wrap_later);
  RUN_TEST(test_bt_sighting_stays_stale_a_wrap_later);
  return UNITY_END();
}

// Native-host unit tests for the presence-edge return of
// LampRoster::cacheWispHello. The real method reaches through a FreeRTOS
// mutex and ::millis(); following the test_lamp_roster_rssi /
// test_transient_override convention the admission logic is mirrored
// inline with an injectable nowMs so the sticky-slot window is testable.
//
// The edge drives HELLO_TLV re-announce / app-notify triggers: true on a
// first sighting or a MAC takeover, false on a same-MAC keepalive and
// false when a fresh rival is rejected.

#include <unity.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr uint32_t kWispDisplayStaleMs = 12000;

// Mirror of WispCache's display-slot fields relevant to admission.
struct WispCache {
  bool present = false;
  uint8_t mac[6] = {0};
  uint32_t lastHelloMs = 0;
  uint32_t slotAdoptedMs = 0;
};

bool freshWithin(uint32_t stampMs, uint32_t nowMs) {
  return stampMs != 0 && (nowMs - stampMs) <= kWispDisplayStaleMs;
}

bool wispSlotFresh(const WispCache& cache, uint32_t nowMs) {
  return freshWithin(cache.lastHelloMs, nowMs) ||
         freshWithin(cache.slotAdoptedMs, nowMs);
}

void adoptWisp(WispCache& c, const uint8_t mac[6], uint32_t nowMs) {
  c.lastHelloMs = 0;
  c.slotAdoptedMs = nowMs;
  std::memcpy(c.mac, mac, 6);
  c.present = true;
}

bool admitWisp(WispCache& c, const uint8_t mac[6], uint32_t nowMs) {
  if (c.present && std::memcmp(c.mac, mac, 6) == 0) return true;
  if (c.present && wispSlotFresh(c, nowMs)) return false;
  adoptWisp(c, mac, nowMs);
  return true;
}

// Mirror of LampRoster::cacheWispHello's edge computation + admission.
bool cacheWispHello(WispCache& c, const uint8_t mac[6], uint32_t nowMs) {
  const bool wasPresent = c.present;
  uint8_t prevMac[6];
  std::memcpy(prevMac, c.mac, 6);
  if (!admitWisp(c, mac, nowMs)) return false;
  const bool presenceEdge =
      !wasPresent || std::memcmp(prevMac, mac, 6) != 0;
  c.lastHelloMs = nowMs;
  return presenceEdge;
}

const uint8_t kMacA[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0A};
const uint8_t kMacB[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x0B};

}  // namespace

void setUp() {}
void tearDown() {}

// First sighting of any wisp is a presence edge.
void test_first_sighting_is_edge() {
  WispCache c;
  TEST_ASSERT_TRUE(cacheWispHello(c, kMacA, 1000));
}

// A repeat HELLO from the same wisp is a keepalive, not an edge.
void test_same_mac_keepalive_is_not_edge() {
  WispCache c;
  cacheWispHello(c, kMacA, 1000);
  TEST_ASSERT_FALSE(cacheWispHello(c, kMacA, 2000));
}

// A different wisp taking a STALE slot is an edge (the slot's occupant moved).
void test_takeover_different_mac_is_edge() {
  WispCache c;
  cacheWispHello(c, kMacA, 1000);
  const uint32_t afterStale = 1000 + kWispDisplayStaleMs + 1;
  TEST_ASSERT_TRUE(cacheWispHello(c, kMacB, afterStale));
}

// A rival HELLO rejected while the slot is fresh is not an edge (dropped).
void test_rejected_rival_is_not_edge() {
  WispCache c;
  cacheWispHello(c, kMacA, 1000);
  TEST_ASSERT_FALSE(cacheWispHello(c, kMacB, 2000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_sighting_is_edge);
  RUN_TEST(test_same_mac_keepalive_is_not_edge);
  RUN_TEST(test_takeover_different_mac_is_edge);
  RUN_TEST(test_rejected_rival_is_not_edge);
  return UNITY_END();
}

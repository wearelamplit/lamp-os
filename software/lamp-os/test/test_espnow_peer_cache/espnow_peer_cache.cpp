// Native tests for the single cached peer slot in EspNowLink::send.
// EspNowLink is ESP-NOW/Arduino-coupled and not natively buildable; the
// re-add decision (add on first use, replace when the MAC changes) is the
// pure predicate send() gates on, so this pins that logic in isolation.

#include <unity.h>

#include <cstdint>

#include "components/network/transport/espnow_link.hpp"

namespace {

const uint8_t kWispA[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
const uint8_t kWispB[6] = {0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0};

// First use: nothing cached yet, so the peer must be added.
void test_first_use_adds(void) {
  const uint8_t cached[6] = {0, 0, 0, 0, 0, 0};
  TEST_ASSERT_TRUE(lamp::peerCacheNeedsReadd(false, cached, kWispA));
}

// Same MAC as cached: no re-add.
void test_same_mac_no_readd(void) {
  TEST_ASSERT_FALSE(lamp::peerCacheNeedsReadd(true, kWispA, kWispA));
}

// Different MAC: evict + re-add.
void test_changed_mac_readds(void) {
  TEST_ASSERT_TRUE(lamp::peerCacheNeedsReadd(true, kWispA, kWispB));
}

}  // namespace

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_use_adds);
  RUN_TEST(test_same_mac_no_readd);
  RUN_TEST(test_changed_mac_readds);
  return UNITY_END();
}

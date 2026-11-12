// Native pins for RollCooldownTable: one roll per peer per window, fixed RAM
// that fails closed under a crowd, and stamps that stay expired across a
// millis() wrap while retire() runs.

#include <unity.h>

#include <cstdint>

#include "behaviors/roll_cooldown.cpp"

using lamp::RollCooldownTable;

namespace {

constexpr uint32_t kWindowMs = 180000;

void macFor(uint32_t i, uint8_t mac[6]) {
  mac[0] = 0x24;
  mac[1] = 0x6F;
  mac[2] = static_cast<uint8_t>(i >> 16);
  mac[3] = static_cast<uint8_t>(i >> 8);
  mac[4] = static_cast<uint8_t>(i);
  mac[5] = static_cast<uint8_t>(i * 37);
}

}  // namespace

void setUp() {}
void tearDown() {}

// A peer asking every second for ten minutes rolls at 0, 180, 360 and 540 s.
void test_one_roll_per_window() {
  RollCooldownTable t;
  uint8_t mac[6];
  macFor(7, mac);
  const uint32_t start = 12345;
  unsigned rolls = 0;
  uint32_t lastRollMs = 0;
  for (uint32_t s = 0; s < 600; ++s) {
    const uint32_t now = start + s * 1000;
    t.retire(now, kWindowMs);
    if (t.tryRoll(mac, now, kWindowMs)) {
      if (rolls > 0) TEST_ASSERT_TRUE(now - lastRollMs >= kWindowMs);
      lastRollMs = now;
      ++rolls;
    }
  }
  TEST_ASSERT_EQUAL_UINT(4, rolls);
}

// Each peer keeps its own window.
void test_peers_do_not_share_a_window() {
  RollCooldownTable t;
  uint8_t a[6], b[6];
  macFor(1, a);
  macFor(2, b);
  TEST_ASSERT_TRUE(t.tryRoll(a, 5000, kWindowMs));
  TEST_ASSERT_TRUE(t.tryRoll(b, 5001, kWindowMs));
  TEST_ASSERT_FALSE(t.tryRoll(a, 5002, kWindowMs));
  TEST_ASSERT_FALSE(t.tryRoll(b, 5000 + kWindowMs, kWindowMs));
  TEST_ASSERT_TRUE(t.tryRoll(a, 5000 + kWindowMs, kWindowMs));
}

// 500 distinct peers inside one window: the first kSlots roll, the rest are
// refused until a slot's roll ages out, and each later window admits at most
// kSlots more.
void test_crowd_fails_closed_at_capacity() {
  RollCooldownTable t;
  uint8_t mac[6];
  const uint32_t start = 777;
  for (uint32_t window = 0; window < 3; ++window) {
    unsigned rolls = 0;
    for (uint32_t i = 0; i < 500; ++i) {
      const uint32_t now = start + window * kWindowMs + i * 100;
      t.retire(now, kWindowMs);
      macFor(i + window * 1000, mac);
      if (t.tryRoll(mac, now, kWindowMs)) ++rolls;
    }
    TEST_ASSERT_EQUAL_UINT(RollCooldownTable::kSlots, rolls);
  }
}

// Holding kSlots peers, a newcomer gets in only once one of their rolls ages out.
void test_full_table_admits_once_a_roll_expires() {
  RollCooldownTable t;
  uint8_t mac[6];
  for (uint32_t i = 0; i < RollCooldownTable::kSlots; ++i) {
    macFor(i, mac);
    TEST_ASSERT_TRUE(t.tryRoll(mac, 1000 + i * 1000, kWindowMs));
  }
  macFor(999, mac);
  TEST_ASSERT_FALSE(t.tryRoll(mac, 1000 + kWindowMs - 1, kWindowMs));
  TEST_ASSERT_TRUE(t.tryRoll(mac, 1000 + kWindowMs, kWindowMs));
}

// A peer that rolled once and went quiet for a whole millis() wrap may roll
// again; without retire() its stamp would read 10 ms old.
void test_retired_roll_stays_expired_a_wrap_later() {
  RollCooldownTable t;
  uint8_t mac[6];
  macFor(3, mac);
  const uint32_t rolledMs = 1000;
  TEST_ASSERT_TRUE(t.tryRoll(mac, rolledMs, kWindowMs));
  for (uint64_t now = rolledMs; now < rolledMs + (1ull << 32); now += 1ull << 28) {
    t.retire(static_cast<uint32_t>(now), kWindowMs);
  }
  TEST_ASSERT_TRUE(t.tryRoll(mac, rolledMs + 10, kWindowMs));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_one_roll_per_window);
  RUN_TEST(test_peers_do_not_share_a_window);
  RUN_TEST(test_crowd_fails_closed_at_capacity);
  RUN_TEST(test_full_table_admits_once_a_roll_expires);
  RUN_TEST(test_retired_roll_stays_expired_a_wrap_later);
  return UNITY_END();
}

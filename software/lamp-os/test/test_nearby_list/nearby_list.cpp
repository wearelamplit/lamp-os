// NearbyList: the app's nearby list is held only while the app reads it, and
// when held its roster copy fits the lamps there and its rebuild reuses the
// served buffer.

#include <unity.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "components/network/mesh/lamp_roster.cpp"
#include "components/network/ble/nearby_list.cpp"
#include "../replaced_new/replaced_new.hpp"

using ble_control::NearbyList;
using lamp::LampRoster;
using lamp::Color;
using lamp::ListedPeer;

static size_t g_allocs = 0;
static size_t g_liveBytes = 0;
static bool g_refuse = false;

constexpr size_t kHeader = alignof(std::max_align_t);

void* testAllocate(size_t n) {
  if (g_refuse) return nullptr;
  ++g_allocs;
  auto* block = static_cast<unsigned char*>(std::malloc(n + kHeader));
  if (!block) return nullptr;
  *reinterpret_cast<size_t*>(block) = n;
  g_liveBytes += n;
  return block + kHeader;
}
void testRelease(void* p) noexcept {
  auto* block = static_cast<unsigned char*>(p) - kHeader;
  g_liveBytes -= *reinterpret_cast<size_t*>(block);
  std::free(block);
}

namespace {

constexpr uint32_t kT0 = 100000;

LampRoster* g_roster = nullptr;
NearbyList* g_list = nullptr;
uint32_t g_now = kT0;
bool g_client = true;

void touch(LampRoster& r, uint8_t from, uint8_t to) {
  for (uint8_t i = from; i < to; i++) {
    char name[8];
    std::snprintf(name, sizeof(name), "lamp%02u", i);
    const uint8_t mac[6] = {0x02, 0x00, 0x00, 0x00, 0x03, i};
    r.addOrUpdateFromEspNow(name, mac, Color(i, 1, 2, 3), Color(4, 5, 6, i), 0x010203);
  }
}

// The loop's tick, run while a read waits.
void loopTick() { g_list->tick(*g_roster, g_client, g_now); }

void at(uint32_t ms) {
  g_now = ms;
  set_mock_millis(ms);
}

size_t entries(const std::string& json) {
  size_t n = 0;
  for (size_t pos = json.find("{\"name\":"); pos != std::string::npos;
       pos = json.find("{\"name\":", pos + 1)) {
    ++n;
  }
  return n;
}

struct Fixture {
  LampRoster roster;
  NearbyList list;
  Fixture() {
    g_roster = &roster;
    g_list = &list;
    at(kT0);
    touch(roster, 0, 22);
  }
};

}  // namespace

void setUp(void) {
  g_refuse = false;
  g_client = true;
  g_taskDelayHook = loopTick;
  g_mock_largest_free_block = 1 << 20;
}
void tearDown(void) {
  g_taskDelayHook = nullptr;
  g_refuse = false;
}

// N1: a connected phone that never reads nearby costs no list and no roster copy.
void test_connected_without_a_read_holds_nothing() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  for (uint32_t t = kT0; t < kT0 + 60000; t += 250) {
    at(t);
    touch(f.roster, 0, 22);
    TEST_ASSERT_FALSE(f.list.tick(f.roster, true, t));
  }
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  std::string out;
  TEST_ASSERT_FALSE(f.list.copy(out));
  TEST_ASSERT_TRUE(out.empty());
}

// N2: the read that asks for the list gets it, built by the loop while it waits.
void test_first_read_returns_the_list() {
  Fixture f;
  std::string out;
  TEST_ASSERT_TRUE(f.list.read(out, g_now));
  TEST_ASSERT_EQUAL_UINT(22, entries(out));
  TEST_ASSERT_EQUAL_CHAR('[', out.front());
  TEST_ASSERT_EQUAL_CHAR(']', out.back());
  TEST_ASSERT_EQUAL_UINT32(1, f.list.rev());
}

// N1: held while reads keep coming, freed kHoldMs after the last one.
void test_released_a_hold_after_the_last_read() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  out = std::string();
  TEST_ASSERT_TRUE(g_liveBytes > baseline);
  at(kT0 + NearbyList::kHoldMs - 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_TRUE(f.list.built());
  f.list.read(out, g_now);
  out = std::string();
  at(g_now + NearbyList::kHoldMs - 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_TRUE(f.list.built());
  at(g_now + 1);
  f.list.tick(f.roster, true, g_now);
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
}

// A disconnect frees it; a reconnect inside the hold builds it again at once,
// and every fresh build moves the rev so the app re-reads.
void test_disconnect_frees_and_reconnect_rebuilds() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  out = std::string();
  at(g_now + 100);
  f.list.tick(f.roster, false, g_now);
  TEST_ASSERT_FALSE(f.list.built());
  TEST_ASSERT_EQUAL_UINT(baseline, g_liveBytes);
  at(g_now + 5000);
  TEST_ASSERT_TRUE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_TRUE(f.list.built());
  TEST_ASSERT_EQUAL_UINT32(2, f.list.rev());
}

void test_read_at_millis_zero_counts() {
  Fixture f;
  g_taskDelayHook = nullptr;
  std::string out;
  f.list.read(out, 0);
  f.list.tick(f.roster, true, 1);
  TEST_ASSERT_TRUE(f.list.built());
}

// A stamp left behind expires even with no phone connected, so a millis
// wrap ~49.7 days later cannot revive it.
void test_expired_read_never_revives_after_wrap() {
  Fixture f;
  g_taskDelayHook = nullptr;
  std::string out;
  f.list.read(out, kT0);
  f.list.tick(f.roster, false, kT0 + NearbyList::kHoldMs);
  f.list.tick(f.roster, true, kT0 + 1);  // kT0 + 2^32 + 1
  TEST_ASSERT_FALSE(f.list.built());
}

// N2: while built, a roster change rebuilds within a second and moves the rev;
// an rssi or lastSeen refresh rebuilds without moving it.
void test_change_while_open_moves_the_rev() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  const uint32_t rev = f.list.rev();
  touch(f.roster, 22, 23);
  at(g_now + 500);
  TEST_ASSERT_FALSE(f.list.tick(f.roster, true, g_now));
  at(g_now + 500);
  TEST_ASSERT_TRUE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, f.list.rev());
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL_UINT(23, entries(out));

  at(g_now + 1000);
  touch(f.roster, 0, 23);
  TEST_ASSERT_FALSE(f.list.tick(f.roster, true, g_now));
  TEST_ASSERT_EQUAL_UINT32(rev + 1, f.list.rev());
}

// N3: steady rebuilds render into the buffer already held.
void test_steady_rebuilds_allocate_nothing() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  for (int i = 0; i < 20; i++) {
    at(g_now + 1000);
    touch(f.roster, 0, 22);
    g_allocs = 0;
    f.list.tick(f.roster, true, g_now);
    TEST_ASSERT_EQUAL_UINT(0, g_allocs);
  }
  f.list.read(out, g_now);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        out.find("\"lastSeenMs\":" + std::to_string(g_now)));
}

// N3: the held list follows the lamps there, growing and shrinking.
void test_held_list_follows_the_roster() {
  Fixture f;
  const size_t baseline = g_liveBytes;
  std::string out;
  f.list.read(out, g_now);
  out = std::string();
  const size_t at22 = g_liveBytes - baseline;
  touch(f.roster, 22, 25);
  at(g_now + 1000);
  f.list.tick(f.roster, true, g_now);
  const size_t at25 = g_liveBytes - baseline;
  TEST_ASSERT_TRUE(at25 > at22);
  at(g_now + 300000);
  touch(f.roster, 0, 4);
  f.roster.prune(LAMP_PRUNE_TIME_MS);
  f.list.read(out, g_now);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_EQUAL_UINT(4, entries(out));
  out = std::string();
  const size_t at4 = g_liveBytes - baseline;
  TEST_ASSERT_TRUE(at4 < at22 / 4);
}

// A heap too tight for a bigger buffer still refreshes into the one held.
void test_low_heap_still_refreshes_in_place() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  g_mock_largest_free_block = 0;
  at(g_now + 1000);
  touch(f.roster, 0, 22);
  f.list.tick(f.roster, true, g_now);
  f.list.read(out, g_now);
  TEST_ASSERT_NOT_EQUAL(std::string::npos,
                        out.find("\"lastSeenMs\":" + std::to_string(g_now)));
}

// No heap for the first build: the read ends empty after its wait.
void test_low_heap_first_read_is_empty() {
  Fixture f;
  g_mock_largest_free_block = 0;
  std::string out = "stale";
  TEST_ASSERT_FALSE(f.list.read(out, g_now));
  TEST_ASSERT_TRUE(out.empty());
}

// Every field at its longest still fits the bound the build reserves from.
void test_longest_entry_fits_its_bound() {
  ListedPeer p;
  std::memset(p.name, 0x01, sizeof(p.name) - 1);
  std::memset(p.fwChannel, 0x02, sizeof(p.fwChannel) - 1);
  std::memset(p.mac, 0xAB, 6);
  std::memset(p.otaSendingTo, 0xCD, 6);
  p.hasMac = true;
  p.hasOtaSendingTo = true;
  p.lastSeenNearMs = 4294967295u;
  p.lastSeenMeshMs = 4294967295u;
  p.firmwareVersion = 4294967295u;
  p.lastRssi = -128;
  p.otaState = 255;
  std::vector<ListedPeer> lamps(NearbyList::kMaxLamps, p);
  std::string out;
  NearbyList::render(lamps, 4294967295u, out);
  const size_t bound = 2 + NearbyList::kMaxLamps * NearbyList::entryBound(p);
  TEST_ASSERT_TRUE(out.size() <= bound);
}

// fwChannel arrives over the air, so a quote in it cannot end the string.
void test_fw_channel_is_escaped() {
  ListedPeer p;
  std::strcpy(p.name, "x");
  std::strcpy(p.fwChannel, "a\",\"z");
  std::string out;
  NearbyList::render({p}, 0, out);
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("\"fwChannel\":\"a\\\",\\\"z\""));
}

// A copy the heap refuses gives the lock back, so the loop is never stuck.
void test_refused_copy_gives_the_lock_back() {
  Fixture f;
  std::string out;
  f.list.read(out, g_now);
  out = std::string();
  g_refuse = true;
  bool threw = false;
  try {
    f.list.copy(out);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  g_refuse = false;
  TEST_ASSERT_TRUE(threw);
  TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_connected_without_a_read_holds_nothing);
  RUN_TEST(test_first_read_returns_the_list);
  RUN_TEST(test_released_a_hold_after_the_last_read);
  RUN_TEST(test_disconnect_frees_and_reconnect_rebuilds);
  RUN_TEST(test_read_at_millis_zero_counts);
  RUN_TEST(test_expired_read_never_revives_after_wrap);
  RUN_TEST(test_change_while_open_moves_the_rev);
  RUN_TEST(test_steady_rebuilds_allocate_nothing);
  RUN_TEST(test_held_list_follows_the_roster);
  RUN_TEST(test_low_heap_still_refreshes_in_place);
  RUN_TEST(test_low_heap_first_read_is_empty);
  RUN_TEST(test_longest_entry_fits_its_bound);
  RUN_TEST(test_fw_channel_is_escaped);
  RUN_TEST(test_refused_copy_gives_the_lock_back);
  return UNITY_END();
}

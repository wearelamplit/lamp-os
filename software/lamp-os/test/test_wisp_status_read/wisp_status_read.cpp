// LampRoster::getWispStatusReadJson under heap failure: a build the heap
// cannot hold reads as a refusal, gives its lock back, and never throws.

#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "components/network/mesh/lamp_roster.cpp"
#include "components/network/mesh/pending_slots.cpp"
#include "components/network/mesh/wisp_cache.cpp"
#include "components/network/mesh/wisp_fleet_cache.cpp"
#include "components/network/mesh/wisp_status_merge.cpp"
#include "util/heap_guard.cpp"
#include "../replaced_new/replaced_new.hpp"

using lamp::LampRoster;

// Allocations still granted before the next one is refused; negative grants
// every one.
static int g_grants = -1;
static size_t g_allocs = 0;

void* testAllocate(size_t n) {
  if (g_grants == 0) return nullptr;
  ++g_allocs;
  if (g_grants > 0) --g_grants;
  return std::malloc(n);
}
void testRelease(void* p) noexcept { std::free(p); }

namespace {

const uint8_t kWisp[6] = {0xE4, 0xB3, 0x23, 0xB4, 0x95, 0x20};
// Past the small-string buffer, so copying it out allocates.
const char kStatus[] =
    "{\"char\":\"wispStatus\",\"mode\":\"palette\",\"paletteName\":\"aurora borealis\"}";

LampRoster::LampWispState painting() {
  LampRoster::LampWispState s;
  s.controllingBase = true;
  s.baseWispColor = "#11223344";
  return s;
}

void seed(LampRoster& r) {
  r.cacheWispStatus(kWisp, kStatus, sizeof(kStatus) - 1);
  r.setLampWispStateProvider(painting);
}

}  // namespace

void setUp(void) {
  g_grants = -1;
  g_semaphoresHeld = 0;
  g_mock_largest_free_block = 1 << 20;
}
void tearDown(void) { g_grants = -1; }

void test_fills_with_heap_to_spare() {
  LampRoster r;
  seed(r);
  std::string out;
  TEST_ASSERT_TRUE(r.getWispStatusReadJson(out, lamp::kWispStatusCharMaxLen));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("\"controllingBase\":true"));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("aurora borealis"));
}

// Every allocation from the copy under the lock to the last serialize
// append is refused in turn; each refusal comes back as false, never as a
// throw, with the lock given back.
void test_refused_allocation_at_each_step_is_a_refusal() {
  LampRoster r;
  seed(r);
  std::string out;
  bool reachedSuccess = false;
  for (int grants = 0; grants < 64 && !reachedSuccess; ++grants) {
    g_grants = grants;
    bool ok = false;
    try {
      ok = r.getWispStatusReadJson(out, lamp::kWispStatusCharMaxLen);
    } catch (...) {
      g_grants = -1;
      TEST_FAIL_MESSAGE("getWispStatusReadJson threw");
    }
    g_grants = -1;
    TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
    if (ok) {
      reachedSuccess = true;
    } else {
      TEST_ASSERT_TRUE(out.empty());
    }
  }
  TEST_ASSERT_TRUE(reachedSuccess);
}

void test_low_heap_refuses_without_allocating() {
  LampRoster r;
  seed(r);
  g_mock_largest_free_block = lamp::kHeapHeadroom;
  std::string out = "stale";
  g_allocs = 0;
  TEST_ASSERT_FALSE(r.getWispStatusReadJson(out, 0));
  TEST_ASSERT_EQUAL_size_t(0, g_allocs);
  TEST_ASSERT_TRUE(out.empty());
  TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
}

// Several times the cached status, so storing it must grow the buffer.
const char kNewStatus[] =
    "{\"char\":\"wispStatus\",\"mode\":\"manual\",\"paletteName\":\"midnight garden\","
    "\"note\":\"a status long enough that no rounded-up capacity of the cached one holds it, "
    "padded well past two hundred bytes so the store has to take a fresh block\"}";
const char kConfig[] = "{\"char\":\"wispConfig\",\"strip\":\"sk6812\",\"pixels\":120}";

// A status or config the heap cannot store keeps the one cached, gives the
// roster lock back, and never throws.
void test_refused_cache_writes_keep_the_last_and_the_lock() {
  LampRoster r;
  seed(r);
  TEST_ASSERT_TRUE(r.cacheWispConfig(kWisp, kConfig, sizeof(kConfig) - 1));
  g_grants = 0;
  bool threw = false;
  bool changed = true;
  try {
    r.cacheWispStatus(kWisp, kNewStatus, sizeof(kNewStatus) - 1);
    changed = r.cacheWispConfig(kWisp, kNewStatus, sizeof(kNewStatus) - 1);
  } catch (...) {
    threw = true;
  }
  g_grants = -1;
  TEST_ASSERT_FALSE(threw);
  TEST_ASSERT_FALSE(changed);
  TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
  std::string out;
  TEST_ASSERT_TRUE(r.getWispStatusReadJson(out, 0));
  TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("aurora borealis"));
  r.copyWispConfigJson(out);
  TEST_ASSERT_EQUAL_STRING(kConfig, out.c_str());
}

// The wispconfig section's copy throws bad_alloc for the page to serve empty,
// with the roster lock already given back.
void test_refused_config_copy_throws_with_the_lock_given_back() {
  LampRoster r;
  seed(r);
  TEST_ASSERT_TRUE(r.cacheWispConfig(kWisp, kConfig, sizeof(kConfig) - 1));
  std::string out;
  g_grants = 0;
  bool threw = false;
  try {
    r.copyWispConfigJson(out);
  } catch (const std::bad_alloc&) {
    threw = true;
  }
  g_grants = -1;
  TEST_ASSERT_TRUE(threw);
  TEST_ASSERT_EQUAL_INT(0, g_semaphoresHeld);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_fills_with_heap_to_spare);
  RUN_TEST(test_refused_allocation_at_each_step_is_a_refusal);
  RUN_TEST(test_low_heap_refuses_without_allocating);
  RUN_TEST(test_refused_cache_writes_keep_the_last_and_the_lock);
  RUN_TEST(test_refused_config_copy_throws_with_the_lock_given_back);
  return UNITY_END();
}

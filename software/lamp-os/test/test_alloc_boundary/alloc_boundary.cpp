// The task-level allocation boundary: a std::bad_alloc thrown by one stage of
// a tick drops the rest of that tick, and the next tick runs whole.

#include <unity.h>

#include <new>

#include "util/alloc_boundary.hpp"

namespace {

int g_ticks = 0;
int g_completed = 0;
bool g_failNext = false;

void drainThatAllocates() {
  if (g_failNext) {
    g_failNext = false;
    throw std::bad_alloc();
  }
}

void tick() {
  ++g_ticks;
  drainThatAllocates();
  ++g_completed;
}

bool loopOnce() {
  try {
    lamp::catchBadAlloc("Lamp::tick", [] { tick(); });
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace

void setUp(void) {
  g_ticks = 0;
  g_completed = 0;
  g_failNext = false;
}
void tearDown(void) {}

void test_bad_alloc_in_a_drain_skips_the_tick_and_the_next_runs() {
  g_failNext = true;
  TEST_ASSERT_TRUE_MESSAGE(loopOnce(), "bad_alloc escaped the boundary");
  TEST_ASSERT_EQUAL_INT(1, g_ticks);
  TEST_ASSERT_EQUAL_INT(0, g_completed);
  TEST_ASSERT_TRUE(loopOnce());
  TEST_ASSERT_EQUAL_INT(2, g_ticks);
  TEST_ASSERT_EQUAL_INT(1, g_completed);
}

void test_reports_whether_it_caught() {
  TEST_ASSERT_TRUE(lamp::catchBadAlloc("ok", [] {}));
  TEST_ASSERT_FALSE(lamp::catchBadAlloc("oom", [] { throw std::bad_alloc(); }));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bad_alloc_in_a_drain_skips_the_tick_and_the_next_runs);
  RUN_TEST(test_reports_whether_it_caught);
  return UNITY_END();
}

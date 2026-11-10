// Pins the debug log rate limiter: a burst cap per window, an exact count of
// the lines it refused, and a window that survives the millis() wrap.

#include <unity.h>
#include <cstdint>

#include "util/log_rate_limiter.hpp"
#include "../../src/util/log_rate_limiter.cpp"

using lamp::LogRateLimiter;

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t kWindowMs = 1000;
constexpr uint32_t kLines = 3;

uint32_t countPrinted(LogRateLimiter& l, uint32_t fromMs, uint32_t n, uint32_t stepMs) {
  uint32_t printed = 0;
  for (uint32_t i = 0; i < n; ++i) {
    if (l.admit(fromMs + i * stepMs).print) ++printed;
  }
  return printed;
}

}  // namespace

void test_admits_the_cap_then_refuses_within_a_window() {
  LogRateLimiter l(kWindowMs, kLines);
  TEST_ASSERT_EQUAL_UINT32(kLines, countPrinted(l, 5000, 10, 7));
}

void test_first_admit_of_next_window_reports_the_refused_count() {
  LogRateLimiter l(kWindowMs, kLines);
  const uint32_t sent = 11;
  countPrinted(l, 5000, sent, 13);
  const LogRateLimiter::Admission a = l.admit(5000 + kWindowMs + 1);
  TEST_ASSERT_TRUE(a.print);
  TEST_ASSERT_EQUAL_UINT32(sent - kLines, a.suppressed);
  // The count is handed over once.
  const LogRateLimiter::Admission b = l.admit(5000 + kWindowMs + 2);
  TEST_ASSERT_TRUE(b.print);
  TEST_ASSERT_EQUAL_UINT32(0, b.suppressed);
}

void test_window_opens_at_the_first_line_after_the_last_expired() {
  LogRateLimiter l(kWindowMs, kLines);
  const uint32_t start = 123457;
  TEST_ASSERT_EQUAL_UINT32(kLines, countPrinted(l, start, kLines, 1));
  // Still inside the window opened at `start`, past a whole-second boundary.
  TEST_ASSERT_FALSE(l.admit(start + kWindowMs - 1).print);
  TEST_ASSERT_TRUE(l.admit(start + kWindowMs).print);
}

void test_spaced_lines_never_suppress() {
  LogRateLimiter l(kWindowMs, kLines);
  TEST_ASSERT_EQUAL_UINT32(9, countPrinted(l, 777, 9, kWindowMs));
}

void test_window_survives_millis_wrap() {
  LogRateLimiter l(kWindowMs, kLines);
  const uint32_t nearWrap = UINT32_MAX - 300;
  TEST_ASSERT_EQUAL_UINT32(kLines, countPrinted(l, nearWrap, kLines + 2, 50));
  // 600 ms past the window's start, across the wrap: still refused.
  TEST_ASSERT_FALSE(l.admit(nearWrap + 600).print);
  const LogRateLimiter::Admission a = l.admit(nearWrap + kWindowMs);
  TEST_ASSERT_TRUE(a.print);
  TEST_ASSERT_EQUAL_UINT32(3, a.suppressed);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_admits_the_cap_then_refuses_within_a_window);
  RUN_TEST(test_first_admit_of_next_window_reports_the_refused_count);
  RUN_TEST(test_window_opens_at_the_first_line_after_the_last_expired);
  RUN_TEST(test_spaced_lines_never_suppress);
  RUN_TEST(test_window_survives_millis_wrap);
  return UNITY_END();
}

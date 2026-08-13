// Native-host test for GlitchyExpression's millis-driven duration gate.
//
// Following the test_transient_lifetime convention: the draw() decision is
// mirrored inline with a mock clock (the real draw() needs Arduino + a frame
// buffer). Pins the observable contract: every glitch paints at least one
// frame before the deadline-driven restore, even when the duration is
// shorter than one compositor flush window.
//
// The paint/restore gate mirrors glitchy_expression.cpp::draw(); the
// deadline compare is the real lamp::timeReached.

#include <unity.h>

#include <cstdint>

#include "expressions/primitives.hpp"

namespace test {

struct GlitchRig {
  bool painted = false;
  int paints = 0;
  int restores = 0;

  void draw(uint32_t nowMs, uint32_t glitchEndMs) {
    if (painted && lamp::timeReached(nowMs, glitchEndMs)) {
      restores++;
    } else {
      paints++;
      painted = true;
    }
  }
};

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

void test_expired_before_first_draw_still_paints_once() {
  // 30 ms glitch, first draw arrives 40 ms after trigger (a coex dip).
  test::GlitchRig rig;
  rig.draw(/*nowMs=*/1040, /*glitchEndMs=*/1030);
  TEST_ASSERT_EQUAL_INT(1, rig.paints);
  TEST_ASSERT_EQUAL_INT(0, rig.restores);
  rig.draw(/*nowMs=*/1056, /*glitchEndMs=*/1030);
  TEST_ASSERT_EQUAL_INT(1, rig.paints);
  TEST_ASSERT_EQUAL_INT(1, rig.restores);
}

void test_paints_until_deadline_then_restores() {
  // 100 ms glitch across 16 ms flush windows.
  test::GlitchRig rig;
  const uint32_t endMs = 1100;
  uint32_t nowMs = 1000;
  while (!rig.restores) {
    rig.draw(nowMs, endMs);
    nowMs += 16;
  }
  TEST_ASSERT_EQUAL_INT(7, rig.paints);  // 1000..1096 inclusive
  TEST_ASSERT_EQUAL_INT(1, rig.restores);
}

void test_deadline_survives_millis_wrap() {
  // Trigger just before the uint32 wrap; deadline lands after it.
  test::GlitchRig rig;
  const uint32_t endMs = 0xFFFFFFF0u + 100u;  // wraps to 0x54
  rig.draw(0xFFFFFFF0u, endMs);
  TEST_ASSERT_EQUAL_INT(1, rig.paints);
  rig.draw(0x00000010u, endMs);   // wrapped, deadline not yet reached
  TEST_ASSERT_EQUAL_INT(2, rig.paints);
  rig.draw(0x00000060u, endMs);   // wrapped, past deadline
  TEST_ASSERT_EQUAL_INT(1, rig.restores);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_expired_before_first_draw_still_paints_once);
  RUN_TEST(test_paints_until_deadline_then_restores);
  RUN_TEST(test_deadline_survives_millis_wrap);
  return UNITY_END();
}

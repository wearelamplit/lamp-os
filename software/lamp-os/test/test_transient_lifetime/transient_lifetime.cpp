// Native-host unit test for the cascade-transient lifetime backstop in
// ExpressionManager::gcTransients().
//
// Following test_transient_override / test_cascade_dedup convention: the
// eviction logic is mirrored inline with a mock clock so the rig doesn't
// link Arduino / ArduinoJson / the compositor. The shape pins the OBSERVABLE
// contract — a transient whose animation never signals complete is still
// reaped once it passes the lifetime cap, so a cascaded expression can't
// latch on the compositor forever (the "stuck white-blue" report).
//
// kTransientMaxLifetimeMs mirrors expression_manager.cpp. If it drifts,
// mirror here.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace test {

static constexpr uint32_t kTransientMaxLifetimeMs = 180000;

// Fake Expression: reports isAnimationComplete() from a fixed flag so a test
// can model the never-completing case the backstop exists for.
struct FakeExpression {
  bool complete = false;
  bool isAnimationComplete() const { return complete; }
};

struct Transient {
  std::string type;
  std::unique_ptr<FakeExpression> expression;
  uint32_t createdMs = 0;
};

// Mirror of ExpressionManager::gcTransients(). Returns the number of live
// transients removed so the test can assert eviction happened.
static size_t gcTransients(std::vector<Transient>& transients, uint32_t nowMs) {
  size_t removed = 0;
  for (auto it = transients.begin(); it != transients.end();) {
    const bool complete = it->expression && it->expression->isAnimationComplete();
    const bool expired = nowMs - it->createdMs >= kTransientMaxLifetimeMs;
    if (complete || expired) {
      it = transients.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

static Transient makeTransient(uint32_t createdMs, bool complete) {
  Transient t;
  t.type = "glitchy";
  t.expression = std::make_unique<FakeExpression>();
  t.expression->complete = complete;
  t.createdMs = createdMs;
  return t;
}

}  // namespace test

void setUp(void) {}
void tearDown(void) {}

void test_never_completing_transient_evicted_after_cap() {
  std::vector<test::Transient> transients;
  transients.push_back(test::makeTransient(/*createdMs=*/1000, /*complete=*/false));

  // Just under the cap: latched, animation never completes → keep waiting.
  test::gcTransients(transients, 1000 + test::kTransientMaxLifetimeMs - 1);
  TEST_ASSERT_EQUAL_UINT32(1, transients.size());

  // At the cap boundary: forced eviction backstops the never-completing one.
  const size_t removed =
      test::gcTransients(transients, 1000 + test::kTransientMaxLifetimeMs);
  TEST_ASSERT_EQUAL_UINT32(1, removed);
  TEST_ASSERT_EQUAL_UINT32(0, transients.size());
}

void test_completed_transient_evicted_immediately() {
  std::vector<test::Transient> transients;
  transients.push_back(test::makeTransient(/*createdMs=*/500, /*complete=*/true));

  // Well within the cap, but the animation reports complete → normal reap.
  const size_t removed = test::gcTransients(transients, 600);
  TEST_ASSERT_EQUAL_UINT32(1, removed);
  TEST_ASSERT_EQUAL_UINT32(0, transients.size());
}

void test_live_transient_survives_before_cap() {
  std::vector<test::Transient> transients;
  transients.push_back(test::makeTransient(/*createdMs=*/0, /*complete=*/false));

  const size_t removed = test::gcTransients(transients, 1);
  TEST_ASSERT_EQUAL_UINT32(0, removed);
  TEST_ASSERT_EQUAL_UINT32(1, transients.size());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_never_completing_transient_evicted_after_cap);
  RUN_TEST(test_completed_transient_evicted_immediately);
  RUN_TEST(test_live_transient_survives_before_cap);

  return UNITY_END();
}

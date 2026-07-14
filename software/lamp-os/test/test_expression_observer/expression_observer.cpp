// Native-host tests for ExpressionObserverRegistry.
//
// Pins:
//   1. register N observers → fanOut calls each with right sourceMac + invocation.
//   2. unregister removes exactly the target; remaining observers still fire.
//   3. zero observers → fanOut is a no-op.
//   4. overflow (> MAX_OBSERVERS) → registration is dropped, no crash.
//   5. duplicate registration → only one call on fanOut.

#include <unity.h>

#include <cstring>

// Native-test seam: include the .cpp to get method definitions + global.
// expression_observer.hpp pulls expression_invocation.hpp (ArduinoJson +
// stdlib only; no Arduino.h) and color.hpp (pure struct). Compiles cleanly
// in the native env.
#include "expressions/expression_observer.cpp"

void setUp(void) {}
void tearDown(void) {}

static const uint8_t kMac1[6] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
static const uint8_t kMac2[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

struct TestObserver : lamp::IExpressionObserver {
  int callCount = 0;
  uint8_t lastMac[6] = {};
  std::string lastType;

  void onPeerExpression(const uint8_t sourceMac[6],
                        const lamp::ExpressionInvocation& inv) override {
    callCount++;
    std::memcpy(lastMac, sourceMac, 6);
    lastType = inv.type;
  }
};

// --- fan-out to registered observers ---

void test_fanout_calls_each_observer() {
  lamp::ExpressionObserverRegistry reg;
  TestObserver a, b;
  reg.registerObserver(&a);
  reg.registerObserver(&b);

  lamp::ExpressionInvocation inv;
  inv.type = "glitchy";
  reg.fanOut(kMac1, inv);

  TEST_ASSERT_EQUAL_INT(1, a.callCount);
  TEST_ASSERT_EQUAL_INT(1, b.callCount);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMac1, a.lastMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMac1, b.lastMac, 6);
  TEST_ASSERT_EQUAL_STRING("glitchy", a.lastType.c_str());
  TEST_ASSERT_EQUAL_STRING("glitchy", b.lastType.c_str());
}

void test_fanout_passes_correct_sourcemac() {
  lamp::ExpressionObserverRegistry reg;
  TestObserver obs;
  reg.registerObserver(&obs);

  lamp::ExpressionInvocation inv;
  inv.type = "pulse";
  reg.fanOut(kMac2, inv);

  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMac2, obs.lastMac, 6);
}

// --- unregister ---

void test_unregister_removes_observer() {
  lamp::ExpressionObserverRegistry reg;
  TestObserver a, b;
  reg.registerObserver(&a);
  reg.registerObserver(&b);
  reg.unregisterObserver(&a);

  lamp::ExpressionInvocation inv;
  inv.type = "pulse";
  reg.fanOut(kMac1, inv);

  TEST_ASSERT_EQUAL_INT(0, a.callCount);
  TEST_ASSERT_EQUAL_INT(1, b.callCount);
}

void test_unregister_nonexistent_is_noop() {
  lamp::ExpressionObserverRegistry reg;
  TestObserver obs;
  // Should not crash.
  reg.unregisterObserver(&obs);
  // Register + unregister twice — also fine.
  reg.registerObserver(&obs);
  reg.unregisterObserver(&obs);
  reg.unregisterObserver(&obs);

  lamp::ExpressionInvocation inv;
  inv.type = "pulse";
  reg.fanOut(kMac1, inv);
  TEST_ASSERT_EQUAL_INT(0, obs.callCount);
}

// --- zero observers ---

void test_zero_observers_noop() {
  lamp::ExpressionObserverRegistry reg;
  lamp::ExpressionInvocation inv;
  inv.type = "breathing";
  // Must not crash.
  reg.fanOut(kMac1, inv);
}

// --- overflow ---

void test_overflow_drops_excess() {
  lamp::ExpressionObserverRegistry reg;
  static constexpr size_t N = lamp::ExpressionObserverRegistry::MAX_OBSERVERS + 2;
  TestObserver obs[N];

  for (size_t i = 0; i < N; ++i) reg.registerObserver(&obs[i]);

  lamp::ExpressionInvocation inv;
  inv.type = "shifty";
  reg.fanOut(kMac1, inv);

  // Exactly MAX_OBSERVERS fired; extras were dropped.
  int fired = 0;
  for (size_t i = 0; i < N; ++i) fired += obs[i].callCount;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(lamp::ExpressionObserverRegistry::MAX_OBSERVERS), fired);
}

// --- duplicate registration ---

void test_duplicate_registration_fires_once() {
  lamp::ExpressionObserverRegistry reg;
  TestObserver obs;
  reg.registerObserver(&obs);
  reg.registerObserver(&obs);  // duplicate

  lamp::ExpressionInvocation inv;
  inv.type = "pulse";
  reg.fanOut(kMac1, inv);

  TEST_ASSERT_EQUAL_INT(1, obs.callCount);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fanout_calls_each_observer);
  RUN_TEST(test_fanout_passes_correct_sourcemac);
  RUN_TEST(test_unregister_removes_observer);
  RUN_TEST(test_unregister_nonexistent_is_noop);
  RUN_TEST(test_zero_observers_noop);
  RUN_TEST(test_overflow_drops_excess);
  RUN_TEST(test_duplicate_registration_fires_once);
  return UNITY_END();
}

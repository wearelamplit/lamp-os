// The GATT callback wrapper: a std::bad_alloc from a wrapped callback never
// leaves it; a read that threw answers empty, and calls that do not throw pass
// through untouched.

#include <unity.h>

#include <cstdint>
#include <new>
#include <string>

#include "components/network/ble/alloc_guarded_callbacks.hpp"

namespace {

struct FakeConnInfo {};

struct FakeCharacteristic {
  std::string value = "stale";
  void setValue(const char* v) { value = v; }
};

struct FakeCallbacks {
  virtual ~FakeCallbacks() = default;
  virtual void onRead(FakeCharacteristic*, FakeConnInfo&) {}
  virtual void onWrite(FakeCharacteristic*, FakeConnInfo&) {}
  virtual void onSubscribe(FakeCharacteristic*, FakeConnInfo&, uint16_t) {}
};

struct Inner : FakeCallbacks {
  bool fail = false;
  int reads = 0, writes = 0, subscribes = 0;
  void onRead(FakeCharacteristic* c, FakeConnInfo&) override {
    ++reads;
    if (fail) throw std::bad_alloc();
    c->setValue("fresh");
  }
  void onWrite(FakeCharacteristic*, FakeConnInfo&) override {
    ++writes;
    if (fail) throw std::bad_alloc();
  }
  void onSubscribe(FakeCharacteristic*, FakeConnInfo&, uint16_t) override {
    ++subscribes;
    if (fail) throw std::bad_alloc();
  }
};

using Guarded = ble_control::AllocGuarded<FakeCallbacks, FakeCharacteristic, FakeConnInfo>;

}  // namespace

void setUp(void) {}
void tearDown(void) {}

void test_calls_pass_through() {
  Inner inner;
  Guarded guarded(&inner);
  FakeCallbacks& cb = guarded;
  FakeCharacteristic c;
  FakeConnInfo ci;
  cb.onRead(&c, ci);
  cb.onWrite(&c, ci);
  cb.onSubscribe(&c, ci, 1);
  TEST_ASSERT_EQUAL_STRING("fresh", c.value.c_str());
  TEST_ASSERT_EQUAL_INT(1, inner.reads);
  TEST_ASSERT_EQUAL_INT(1, inner.writes);
  TEST_ASSERT_EQUAL_INT(1, inner.subscribes);
}

void test_bad_alloc_never_leaves_and_a_read_answers_empty() {
  Inner inner;
  inner.fail = true;
  Guarded guarded(&inner);
  FakeCallbacks& cb = guarded;
  FakeCharacteristic c;
  FakeConnInfo ci;
  try {
    cb.onRead(&c, ci);
    cb.onWrite(&c, ci);
    cb.onSubscribe(&c, ci, 1);
  } catch (...) {
    TEST_FAIL_MESSAGE("bad_alloc escaped the callback wrapper");
  }
  TEST_ASSERT_EQUAL_STRING("", c.value.c_str());
  TEST_ASSERT_EQUAL_INT(1, inner.subscribes);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_calls_pass_through);
  RUN_TEST(test_bad_alloc_never_leaves_and_a_read_answers_empty);
  return UNITY_END();
}

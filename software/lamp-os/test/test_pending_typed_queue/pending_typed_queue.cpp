// PendingTypedQueue: the recv-task to loop-task FIFO for per-peer messages.

#include <unity.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace {
int gMuxDepth = 0;
}  // namespace

struct TestMux {};
#define portENTER_CRITICAL(x) do { (void)(x); ++gMuxDepth; } while (0)
#define portEXIT_CRITICAL(x)  do { (void)(x); --gMuxDepth; } while (0)
using portMUX_TYPE = TestMux;

#include "core/pending_typed_queue.hpp"

using lamp::PendingTypedQueue;

void setUp(void) { gMuxDepth = 0; }
void tearDown(void) {}

namespace {
portMUX_TYPE gMux{};

struct Msg {
  uint8_t mac[6];
  uint16_t value;
};

Msg msg(uint16_t v) {
  Msg m{};
  for (uint8_t i = 0; i < 6; ++i) m.mac[i] = static_cast<uint8_t>(v * 11 + i * 3 + 1);
  m.value = v;
  return m;
}

void assertMsg(uint16_t expected, const Msg& got) {
  const Msg want = msg(expected);
  TEST_ASSERT_EQUAL_UINT16(want.value, got.value);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want.mac, got.mac, 6);
}

template <size_t N>
std::vector<uint16_t> drainAll(PendingTypedQueue<Msg, N>& q) {
  std::vector<uint16_t> out;
  q.drainWhile(gMux, [] { return true; }, [&](const Msg& m) {
    TEST_ASSERT_EQUAL_INT(0, gMuxDepth);
    assertMsg(m.value, m);
    out.push_back(m.value);
  });
  return out;
}
}  // namespace

// Every message posted before a drain comes out, oldest first, none overwritten.
void test_posts_before_a_drain_all_come_out_in_order() {
  PendingTypedQueue<Msg, 3> q;
  for (uint16_t v : {70, 12, 905}) TEST_ASSERT_TRUE(q.post(gMux, msg(v)));
  const std::vector<uint16_t> got = drainAll(q);
  TEST_ASSERT_EQUAL_size_t(3, got.size());
  TEST_ASSERT_EQUAL_UINT16(70, got[0]);
  TEST_ASSERT_EQUAL_UINT16(12, got[1]);
  TEST_ASSERT_EQUAL_UINT16(905, got[2]);
  TEST_ASSERT_TRUE(drainAll(q).empty());
}

// A post to a full queue is refused and leaves the waiting messages intact.
void test_full_queue_refuses_the_newest() {
  PendingTypedQueue<Msg, 4> q;
  for (uint16_t v = 1; v <= 4; ++v) TEST_ASSERT_TRUE(q.post(gMux, msg(v * 100)));
  TEST_ASSERT_FALSE(q.post(gMux, msg(999)));
  TEST_ASSERT_EQUAL_INT(0, gMuxDepth);
  const std::vector<uint16_t> got = drainAll(q);
  TEST_ASSERT_EQUAL_size_t(4, got.size());
  for (uint16_t v = 1; v <= 4; ++v) TEST_ASSERT_EQUAL_UINT16(v * 100, got[v - 1]);
  TEST_ASSERT_TRUE(q.post(gMux, msg(999)));
}

// drainWhile stops as soon as ready() turns false; the rest wait, in order,
// for a later drain.
void test_drain_stops_when_not_ready() {
  PendingTypedQueue<Msg, 4> q;
  for (uint16_t v : {5, 6, 7, 8}) q.post(gMux, msg(v));
  int budget = 2;
  std::vector<uint16_t> got;
  q.drainWhile(gMux, [&] { return budget > 0; },
               [&](const Msg& m) { got.push_back(m.value); --budget; });
  TEST_ASSERT_EQUAL_size_t(2, got.size());
  TEST_ASSERT_EQUAL_UINT16(5, got[0]);
  TEST_ASSERT_EQUAL_UINT16(6, got[1]);

  int asked = 0;
  q.drainWhile(gMux, [&] { ++asked; return false; },
               [&](const Msg&) { TEST_FAIL_MESSAGE("drained while not ready"); });
  TEST_ASSERT_EQUAL_INT(1, asked);

  const std::vector<uint16_t> rest = drainAll(q);
  TEST_ASSERT_EQUAL_size_t(2, rest.size());
  TEST_ASSERT_EQUAL_UINT16(7, rest[0]);
  TEST_ASSERT_EQUAL_UINT16(8, rest[1]);
}

// Posts interleaved with partial drains wrap round the storage and keep FIFO
// order and capacity across many laps.
void test_interleaved_posts_and_drains_keep_order_across_wraps() {
  PendingTypedQueue<Msg, 3> q;
  uint16_t next = 1, expect = 1;
  for (int lap = 0; lap < 40; ++lap) {
    const int posts = 1 + lap % 3;
    for (int k = 0; k < posts; ++k) {
      if (q.post(gMux, msg(next))) ++next;
    }
    int take = 1 + (lap * 7) % 3;
    q.drainWhile(gMux, [&] { return take > 0; }, [&](const Msg& m) {
      assertMsg(expect, m);
      ++expect;
      --take;
    });
  }
  for (uint16_t v : drainAll(q)) TEST_ASSERT_EQUAL_UINT16(expect++, v);
  TEST_ASSERT_EQUAL_UINT16(next, expect);
  TEST_ASSERT_TRUE(next > 40);
}

// A callback that throws loses only the message it was handed.
void test_throwing_callback_keeps_the_rest() {
  PendingTypedQueue<Msg, 3> q;
  for (uint16_t v : {31, 32, 33}) q.post(gMux, msg(v));
  try {
    q.drainWhile(gMux, [] { return true; },
                 [](const Msg&) { throw std::runtime_error("refused"); });
    TEST_FAIL_MESSAGE("no throw");
  } catch (const std::runtime_error&) {
  }
  TEST_ASSERT_EQUAL_INT(0, gMuxDepth);
  const std::vector<uint16_t> rest = drainAll(q);
  TEST_ASSERT_EQUAL_size_t(2, rest.size());
  TEST_ASSERT_EQUAL_UINT16(32, rest[0]);
  TEST_ASSERT_EQUAL_UINT16(33, rest[1]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_posts_before_a_drain_all_come_out_in_order);
  RUN_TEST(test_full_queue_refuses_the_newest);
  RUN_TEST(test_drain_stops_when_not_ready);
  RUN_TEST(test_interleaved_posts_and_drains_keep_order_across_wraps);
  RUN_TEST(test_throwing_callback_keeps_the_rest);
  return UNITY_END();
}

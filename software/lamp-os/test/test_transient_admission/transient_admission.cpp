// Native tests for the crowd guards on the expression manager's hot paths:
// the transient admission gate (cap, per-source slots, heap gate), the
// lookup that hands a configured entry its stored parameters, and the upsert
// mirror that decides whether an entry is config-backed.

#include <unity.h>

#include <vector>

#include "config/config_codec.hpp"
#include "expressions/transient_admission.hpp"

#include "../../src/util/color.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/util/json_writer.cpp"
#include "../../src/config/config_codec.cpp"

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

void test_cap_bounds_every_source() {
  TEST_ASSERT_TRUE(admitTransient(2, 0, 2, TransientSource::Local, true));
  TEST_ASSERT_FALSE(admitTransient(3, 0, 2, TransientSource::Local, true));
  TEST_ASSERT_FALSE(admitTransient(TransientSlots::kMax, 0, 1, TransientSource::Local, true));
}

// A crowd's echoes and cascades fill at most the remote slots, so a Test
// press or a greeting still has room.
void test_remote_cannot_starve_local() {
  TEST_ASSERT_TRUE(admitTransient(0, 0, 2, TransientSource::Remote, true));
  TEST_ASSERT_FALSE(admitTransient(2, 2, 1, TransientSource::Remote, true));
  TEST_ASSERT_TRUE(admitTransient(2, 2, 2, TransientSource::Local, true));
}

void test_heap_gate_refuses_any_source() {
  TEST_ASSERT_FALSE(admitTransient(0, 0, 1, TransientSource::Local, false));
  TEST_ASSERT_FALSE(admitTransient(0, 0, 1, TransientSource::Remote, false));
}

// The call-site form counts sources from the live transients themselves.
void test_admission_counts_live_sources() {
  struct Live {
    TransientSource source;
  };
  std::vector<Live> live = {{TransientSource::Remote}, {TransientSource::Remote}};
  TEST_ASSERT_FALSE(admitTransientAmong(live, 1, TransientSource::Remote, true));
  TEST_ASSERT_TRUE(admitTransientAmong(live, 2, TransientSource::Local, true));
  live.push_back({TransientSource::Local});
  live.push_back({TransientSource::Local});
  TEST_ASSERT_FALSE(admitTransientAmong(live, 1, TransientSource::Local, true));
}

void test_lookup_finds_only_the_matching_entry() {
  ExpressionSettings s;
  ExpressionConfig a;
  a.type = "pulse";
  a.target = 1;
  a.setParameter("size", 3);
  ExpressionConfig b = a;
  b.target = 3;
  b.setParameter("size", 7);
  s.expressions = {a, b};
  const ExpressionParams* p = config_codec::findExpressionParams(s, "pulse", 3);
  TEST_ASSERT_NOT_NULL(p);
  TEST_ASSERT_EQUAL_UINT32(7, p->get("size", 0));
  TEST_ASSERT_NULL(config_codec::findExpressionParams(s, "pulse", 2));
  TEST_ASSERT_NULL(config_codec::findExpressionParams(s, "shifty", 1));
}

void test_mirror_replaces_appends_and_refuses_when_full() {
  ExpressionSettings s;
  ExpressionConfig e;
  e.type = "pulse";
  e.target = 1;
  TEST_ASSERT_TRUE(config_codec::mirrorUpsert(s, e));
  e.setParameter("size", 9);
  TEST_ASSERT_TRUE(config_codec::mirrorUpsert(s, e));
  TEST_ASSERT_EQUAL(1, s.expressions.size());
  TEST_ASSERT_EQUAL_UINT32(9, s.expressions[0].getParameter("size", 0));
  for (size_t i = 1; i < kMaxExpressions; ++i) {
    e.target = static_cast<uint8_t>(10 + i);
    TEST_ASSERT_TRUE(config_codec::mirrorUpsert(s, e));
    TEST_ASSERT_TRUE(s.expressions.capacity() <= kMaxExpressions);
  }
  e.target = 99;
  TEST_ASSERT_FALSE(config_codec::mirrorUpsert(s, e));
  TEST_ASSERT_EQUAL(kMaxExpressions, s.expressions.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cap_bounds_every_source);
  RUN_TEST(test_remote_cannot_starve_local);
  RUN_TEST(test_heap_gate_refuses_any_source);
  RUN_TEST(test_admission_counts_live_sources);
  RUN_TEST(test_lookup_finds_only_the_matching_entry);
  RUN_TEST(test_mirror_replaces_appends_and_refuses_when_full);
  return UNITY_END();
}

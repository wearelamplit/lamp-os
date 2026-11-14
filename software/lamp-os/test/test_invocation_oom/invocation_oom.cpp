// serializeInvocation under heap failure: a payload the heap cannot hold
// reads empty and never throws.

#include <unity.h>

#include <cstdlib>
#include <string>

#include "expressions/expression_invocation.cpp"
#include "util/color.cpp"
#include "../replaced_new/replaced_new.hpp"

static bool g_refuse = false;

void* testAllocate(size_t n) { return g_refuse ? nullptr : std::malloc(n); }
void testRelease(void* p) noexcept { std::free(p); }

void setUp(void) { g_refuse = false; }
void tearDown(void) { g_refuse = false; }

void test_refused_serialize_reads_empty() {
  lamp::ExpressionInvocation inv;
  inv.type = "rainbow_cascade_long_type";
  inv.colors = {lamp::Color(1, 2, 3, 4), lamp::Color(5, 6, 7, 8)};
  std::string out;
  lamp::serializeInvocation(inv, out);
  TEST_ASSERT_FALSE(out.empty());

  std::string refused;
  g_refuse = true;
  try {
    lamp::serializeInvocation(inv, refused);
  } catch (...) {
    g_refuse = false;
    TEST_FAIL_MESSAGE("serializeInvocation threw");
  }
  g_refuse = false;
  TEST_ASSERT_TRUE(refused.empty());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_refused_serialize_reads_empty);
  return UNITY_END();
}

// Counts heap allocations to pin that expression parameters live inline:
// copying a full ExpressionParams allocates nothing, and an ExpressionConfig
// copy allocates only its colour list. The std::map it replaced allocated a
// node per parameter, plus the key for any key past the small-string buffer.

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <map>
#include <new>
#include <string>

#include "config/config_types.hpp"
#include "../replaced_new/replaced_new.hpp"

static size_t g_allocs = 0;

void* testAllocate(size_t n) {
  ++g_allocs;
  return std::malloc(n);
}
void testRelease(void* p) noexcept { std::free(p); }

using namespace lamp;

void setUp(void) {}
void tearDown(void) {}

static ExpressionConfig maxedConfig() {
  ExpressionConfig c;
  c.type = "shifty";
  c.colors.assign(kMaxExpressionColors, Color(1, 2, 3, 4));
  for (size_t i = 0; i < kMaxExpressionParams; ++i) {
    c.setParameter(kExpressionParamKeys[i], static_cast<uint32_t>(i));
  }
  return c;
}

void test_params_copy_never_allocates() {
  const ExpressionConfig src = maxedConfig();
  g_allocs = 0;
  ExpressionParams copy = src.parameters;
  copy.set("size", 9);
  TEST_ASSERT_EQUAL_UINT(0, g_allocs);
  TEST_ASSERT_EQUAL_UINT(kMaxExpressionParams, src.parameters.size());
}

void test_config_copy_allocates_only_colours() {
  const ExpressionConfig src = maxedConfig();
  g_allocs = 0;
  ExpressionConfig copy = src;
  TEST_ASSERT_EQUAL_UINT(1, g_allocs);
  TEST_ASSERT_TRUE(copy.parameters == src.parameters);
}

// The replaced container, for the before figure in the report.
void test_legacy_map_allocation_count() {
  std::map<std::string, uint32_t> legacy;
  g_allocs = 0;
  for (size_t i = 0; i < kMaxExpressionParams; ++i) legacy[kExpressionParamKeys[i]] = 1;
  printf("[alloc] legacy map, %zu params: %zu allocations\n", kMaxExpressionParams, g_allocs);
  TEST_ASSERT_TRUE(g_allocs >= kMaxExpressionParams);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_params_copy_never_allocates);
  RUN_TEST(test_config_copy_allocates_only_colours);
  RUN_TEST(test_legacy_map_allocation_count);
  return UNITY_END();
}

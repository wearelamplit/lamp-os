#include <unity.h>

#include "expressions/expression_schema.hpp"

using namespace lamp;

static constexpr EnumOption kOpts[] = {{0, "Uniform"}, {1, "Up", true}};

static constexpr ParamSpec kParams[] = {
  { .key = "count", .kind = ParamKind::Int, .label = "Dots",
    .min = 1, .max = Bound::pixels(10), .def = 9,
    .invert = true, .leftLabel = "Slow", .rightLabel = "Fast", .requiresZoning = true },
  { .key = "fill", .kind = ParamKind::Enum, .label = "Fill", .options = kOpts },
};

static constexpr Surface kExclude[] = {Surface::Base};

static constexpr ExpressionDescriptor d{
  .id = "dots",
  .name = "Dots",
  .continuous = true,
  .colors = {.max = 8, .label = "Dot Colors", .inheritsSurface = true},
  .interval = RangeSpec{.min = 600, .max = 2700, .step = 60, .unit = "s", .defLo = 1800, .defHi = 2700},
  .hasZone = true,
  .zoneOptional = true,
  .excludeTargets = kExclude,
  .params = kParams,
};

static_assert(d.params.size() == 2);
static_assert(d.params[0].max.kind == Bound::Pixels && d.params[0].max.v == 10);
static_assert(d.params[0].invert && d.params[0].requiresZoning);
static_assert(d.params[1].kind == ParamKind::Enum && d.params[1].options.size() == 2);
static_assert(d.params[1].options[1].zoning);
static_assert(Bound{10}.kind == Bound::Literal && Bound{10}.v == 10);
static_assert(d.interval.has_value());
static_assert(d.hasZone && d.zoneOptional);
static_assert(d.continuous);

void setUp(void) {}
void tearDown(void) {}

void test_params_size() {
  TEST_ASSERT_EQUAL_size_t(2, d.params.size());
}

void test_max_bound_kind_and_value() {
  TEST_ASSERT_EQUAL_INT(Bound::Pixels, d.params[0].max.kind);
  TEST_ASSERT_EQUAL_INT32(10, d.params[0].max.v);
}

void test_literal_bound() {
  constexpr Bound b{10};
  TEST_ASSERT_EQUAL_INT(Bound::Literal, b.kind);
  TEST_ASSERT_EQUAL_INT32(10, b.v);
}

void test_interval_present() {
  TEST_ASSERT_TRUE(d.interval.has_value());
  TEST_ASSERT_EQUAL_INT32(600, d.interval->min);
  TEST_ASSERT_EQUAL_INT32(2700, d.interval->max);
}

void test_continuous_flag() {
  TEST_ASSERT_TRUE(d.continuous);
}

void test_enum_options() {
  TEST_ASSERT_EQUAL_INT(ParamKind::Enum, d.params[1].kind);
  TEST_ASSERT_EQUAL_size_t(2, d.params[1].options.size());
  TEST_ASSERT_EQUAL_STRING("Uniform", d.params[1].options[0].label);
  TEST_ASSERT_TRUE(d.params[1].options[1].zoning);
}

void test_int_modifiers_and_zoning() {
  TEST_ASSERT_TRUE(d.params[0].invert);
  TEST_ASSERT_TRUE(d.params[0].requiresZoning);
  TEST_ASSERT_EQUAL_STRING("Slow", d.params[0].leftLabel);
  TEST_ASSERT_TRUE(d.zoneOptional);
  TEST_ASSERT_TRUE(d.hasZone);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_params_size);
  RUN_TEST(test_max_bound_kind_and_value);
  RUN_TEST(test_literal_bound);
  RUN_TEST(test_interval_present);
  RUN_TEST(test_continuous_flag);
  RUN_TEST(test_enum_options);
  RUN_TEST(test_int_modifiers_and_zoning);
  return UNITY_END();
}

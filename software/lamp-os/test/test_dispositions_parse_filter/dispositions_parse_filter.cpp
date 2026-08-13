// Native-host unit tests for the BD_ADDR-only parse filter applied
// in Config::loadDispositionsFromPrefs_ and setDispositionsFromJson.
//
// Context: pre-Phase-C lamps had name-keyed disposition NVS blobs.
// The "no migration" strategy is a silent parse-time filter: any
// JSON key that doesn't match canonical BD_ADDR form is dropped
// during load + bulk-write. This file tests that filter logic in
// isolation against an inline mirror of the parse loop.

#include <unity.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "util/bd_addr.hpp"

namespace lamp {

// Inline mirror of the production parse loop (config.cpp:393-401 + 248-256
// post-Phase-C). Both paths share the same isValidBdAddr filter; this
// helper covers both.
std::vector<std::pair<std::string, uint8_t>> parseFiltered(
    const std::vector<std::pair<std::string, int>>& jsonPairs) {
  constexpr size_t kMax = 100;
  constexpr uint8_t kDefault = 3;
  std::vector<std::pair<std::string, uint8_t>> next;
  next.reserve(kMax);
  for (const auto& kv : jsonPairs) {
    if (next.size() >= kMax) break;
    if (!isValidBdAddr(kv.first.c_str())) continue;
    // Value handling differs from production: production uses
    // ArduinoJson's `kv.value() | (uint32_t)kDefault` overload (default-value
    // semantics) followed by clamp-to-boundary 1/5. This mirror operates on
    // plain ints — `|` would be bitwise OR, so we map out-of-range to the
    // neutral default instead. Doesn't affect any test in this file (all
    // inputs are valid 1..5), but flag if you extend with out-of-range cases.
    int v = kv.second;
    if (v < 1) v = kDefault;
    if (v > 5) v = kDefault;
    next.emplace_back(kv.first, static_cast<uint8_t>(v));
  }
  return next;
}

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_all_bd_addr_entries_retained() {
  // Forward-compat happy path: every key is canonical BD_ADDR form,
  // all retained.
  std::vector<std::pair<std::string, int>> input = {
      {"AA:BB:CC:DD:EE:FF", 4},
      {"11:22:33:44:55:66", 1},
      {"FF:EE:DD:CC:BB:AA", 5},
  };
  auto out = lamp::parseFiltered(input);
  TEST_ASSERT_EQUAL_UINT32(3, out.size());
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", out[0].first.c_str());
  TEST_ASSERT_EQUAL_UINT8(4, out[0].second);
}

void test_all_name_entries_dropped() {
  // Pre-Phase-C blob — every key is a name. Filter drops them all.
  // This is the silent-migration case: the user loses their existing
  // dispositions, the lamp boots with an empty map, the next slider
  // write overwrites NVS in the new shape.
  std::vector<std::pair<std::string, int>> input = {
      {"jacko", 4},
      {"floral", 5},
      {"anonymous-lamp", 2},
  };
  auto out = lamp::parseFiltered(input);
  TEST_ASSERT_EQUAL_UINT32(0, out.size());
}

void test_mixed_input_only_bd_addr_retained() {
  // Hybrid case — partial migration in flight (which we don't expect
  // in practice but is defensible). Only BD_ADDR-shaped keys survive.
  std::vector<std::pair<std::string, int>> input = {
      {"jacko", 4},                  // dropped: not BD_ADDR
      {"AA:BB:CC:DD:EE:FF", 5},      // kept
      {"floral", 2},                 // dropped: not BD_ADDR
      {"11:22:33:44:55:66", 1},      // kept
      {"", 3},                       // dropped: not BD_ADDR (empty)
  };
  auto out = lamp::parseFiltered(input);
  TEST_ASSERT_EQUAL_UINT32(2, out.size());
  // parseFiltered preserves input order — kept entries are at
  // out[0] (AA:BB...) and out[1] (11:22...). The production code
  // sorts after this stage; we're testing only the filter logic.
  TEST_ASSERT_EQUAL_STRING("AA:BB:CC:DD:EE:FF", out[0].first.c_str());
  TEST_ASSERT_EQUAL_UINT8(5, out[0].second);
  TEST_ASSERT_EQUAL_STRING("11:22:33:44:55:66", out[1].first.c_str());
  TEST_ASSERT_EQUAL_UINT8(1, out[1].second);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_all_bd_addr_entries_retained);
  RUN_TEST(test_all_name_entries_dropped);
  RUN_TEST(test_mixed_input_only_bd_addr_retained);

  return UNITY_END();
}

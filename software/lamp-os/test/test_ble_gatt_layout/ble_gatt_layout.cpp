// Guards the frozen GATT attribute layout. Handles are positional, so any
// add / remove / reorder of a characteristic (or a NOTIFY→CCCD change) shifts
// the wire contract and can stale-out already-paired app installs.
//
// This test makes that impossible to do SILENTLY: it pins a hash of the
// ordered (uuid, props) table to the schema version. Change the layout and the
// hash changes — to make this test pass again you MUST bump kGattSchemaVersion
// in gatt_layout.hpp and add the new (version → hash) row in expectedHash()
// below. That bump is the signal the app keys off for legacy fallback, and the
// reviewer sees both the version bump and the new hash in the same diff.

#include <unity.h>

#include <cstdio>

#include "components/network/ble/gatt_layout.hpp"

using namespace ble_control;

void setUp(void) {}
void tearDown(void) {}

// FNV-1a over each entry's uuid bytes + props byte, in registration order.
static uint32_t layoutHash() {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < kGattLayoutCount; ++i) {
    for (const char* p = kGattLayout[i].uuid; *p; ++p) {
      h ^= static_cast<uint8_t>(*p);
      h *= 16777619u;
    }
    h ^= kGattLayout[i].props;
    h *= 16777619u;
  }
  return h;
}

// One pinned hash per schema version. Adding a row is the deliberate act of
// freezing a new layout.
static bool expectedHash(uint8_t version, uint32_t& out) {
  switch (version) {
    case 1: out = 0x8386B522u; return true;
    case 2: out = 0xBE2909BFu; return true;
    case 3: out = 0x3204C7A8u; return true;
    case 4: out = 0xBE2909BFu; return true;
    default: return false;
  }
}

void test_schema_version_has_a_pinned_hash() {
  uint32_t expected = 0;
  const bool pinned = expectedHash(kGattSchemaVersion, expected);
  TEST_ASSERT_TRUE_MESSAGE(
      pinned,
      "kGattSchemaVersion has no pinned layout hash — add a row in expectedHash()");
}

void test_layout_matches_pinned_hash() {
  uint32_t expected = 0;
  if (!expectedHash(kGattSchemaVersion, expected)) return;  // reported above
  const uint32_t actual = layoutHash();
  // Helps when re-pinning: the failure prints the value you need.
  if (expected != actual) {
    printf("layoutHash() = 0x%08x for kGattSchemaVersion=%u\n", actual,
           kGattSchemaVersion);
  }
  TEST_ASSERT_EQUAL_HEX32_MESSAGE(
      expected, actual,
      "GATT layout changed without re-pinning — bump kGattSchemaVersion and "
      "update expectedHash() (see CLAUDE.md frozen-layout lock-in)");
}

void test_schema_version_char_is_tail() {
  // The most-recently-appended characteristic must be last; the app relies
  // on append-only growth to preserve handle positions.
  TEST_ASSERT_EQUAL_STRING(CHAR_WISP_CLAIMS,
                           kGattLayout[kGattLayoutCount - 1].uuid);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_schema_version_has_a_pinned_hash);
  RUN_TEST(test_layout_matches_pinned_hash);
  RUN_TEST(test_schema_version_char_is_tail);
  return UNITY_END();
}

// MSG_WISP_STATE frame -> PendingWispState slot -> this lamp's RGBW base/shade
// -> wisp layer render. Pins W delivery, the no-trailer W = 0 fallback, and
// that a black entry still paints.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/mesh/pending_slots.cpp"
#include "render/layer_stack.hpp"
#include "util/fade.cpp"

namespace lp = lamp_protocol;
using lamp::Color;
using lamp::PendingWispState;

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kSrc[6]   = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
const uint8_t kMine[6]  = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
const uint8_t kOther[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};

PendingWispState gSlot;

// Two-entry frame: kOther at index 0, kMine at index 1.
size_t buildTwo(uint8_t* buf, size_t cap, const uint8_t mineRgb[6],
                const uint8_t w[4]) {
  uint8_t entries[2 * lp::WISP_STATE_ENTRY_SIZE];
  std::memcpy(entries, kOther, 6);
  const uint8_t otherRgb[6] = {7, 7, 7, 7, 7, 7};
  std::memcpy(entries + 6, otherRgb, 6);
  std::memcpy(entries + 12, kMine, 6);
  std::memcpy(entries + 18, mineRgb, 6);
  return lp::buildWispState(buf, cap, 1, kSrc, 100, 60000, 0, entries, w, 2);
}

void fillFrom(const uint8_t* buf, size_t len) {
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, len, ws));
  lamp::fillPendingWispState(gSlot, ws, nullptr);
}

void assertColor(Color want, Color got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
  TEST_ASSERT_EQUAL_UINT8(want.w, got.w);
}
}  // namespace

void test_slot_delivers_w_to_own_entry() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};  // other: 11/22, mine: C8/37
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = buildTwo(buf, sizeof(buf), rgb, w);
  fillFrom(buf, n);

  Color base;
  Color shade;
  TEST_ASSERT_TRUE(lamp::wispStateColorsFor(gSlot, kMine, base, shade));
  assertColor(Color(0, 0, 0, 0xC8), base);
  assertColor(Color(0, 0, 0, 0x37), shade);
}

void test_slot_without_w_plane_reads_w_zero() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = buildTwo(buf, sizeof(buf), rgb, w);
  // Leave a W plane in the reused slot first, as the static recv slot would.
  fillFrom(buf, n);
  fillFrom(buf, n - 4);  // an older wisp's frame: same entries, no W plane

  Color base;
  Color shade;
  TEST_ASSERT_TRUE(lamp::wispStateColorsFor(gSlot, kMine, base, shade));
  assertColor(Color(40, 50, 60, 0), base);
  assertColor(Color(70, 80, 90, 0), shade);
}

void test_slot_black_entry_paints_black() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {9, 9, 0, 0};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = buildTwo(buf, sizeof(buf), rgb, w);
  fillFrom(buf, n);

  Color base(1, 1, 1, 1);
  Color shade(1, 1, 1, 1);
  TEST_ASSERT_TRUE(lamp::wispStateColorsFor(gSlot, kMine, base, shade));
  assertColor(Color(0, 0, 0, 0), base);
  assertColor(Color(0, 0, 0, 0), shade);
}

void test_slot_absent_mac_is_unpainted() {
  const uint8_t kStranger[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t w[4] = {0, 0, 0, 0};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  fillFrom(buf, buildTwo(buf, sizeof(buf), rgb, w));
  Color base;
  Color shade;
  TEST_ASSERT_FALSE(lamp::wispStateColorsFor(gSlot, kStranger, base, shade));
}

void test_wisp_layer_renders_state_w() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {0, 0, 0xC8, 0x37};
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  fillFrom(buf, buildTwo(buf, sizeof(buf), rgb, w));
  Color base;
  Color shade;
  TEST_ASSERT_TRUE(lamp::wispStateColorsFor(gSlot, kMine, base, shade));

  lamp::LayerStack stack;
  stack.setWispTarget(base);
  for (int i = 0; i < 600; ++i) stack.tick(1.0f, 0);
  // Own scene is saturated RGB with no W; full wisp presence replaces it.
  assertColor(Color(0, 0, 0, 0xC8), stack.composite(Color(200, 100, 50, 0)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_slot_delivers_w_to_own_entry);
  RUN_TEST(test_slot_without_w_plane_reads_w_zero);
  RUN_TEST(test_slot_black_entry_paints_black);
  RUN_TEST(test_slot_absent_mac_is_unpainted);
  RUN_TEST(test_wisp_layer_renders_state_w);
  return UNITY_END();
}

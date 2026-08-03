#include <unity.h>
#include <array>
#include <vector>
#include "../../src/lamps/lioness/lions_scene.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

using namespace lamp;
using namespace lamp::lioness;

void setUp() {}
void tearDown() {}

// Three lions, distinct single-stop peer palettes: each 6px zone is that solid
// peer colour, no cross-zone bleed.
void test_three_zones_each_peer_colour() {
  std::array<std::vector<Color>, 3> stops = {
    std::vector<Color>{Color(0xF0, 0, 0, 0)},
    std::vector<Color>{Color(0, 0xF0, 0, 0)},
    std::vector<Color>{Color(0, 0, 0xF0, 0)},
  };
  std::vector<Color> out;
  renderLions(out, 18, stops, Color(0, 0, 0, 0xFF));
  TEST_ASSERT_EQUAL_UINT16(18, out.size());
  for (uint16_t i = 0; i < 6; i++)  TEST_ASSERT_EQUAL_UINT8(0xF0, out[i].r);
  for (uint16_t i = 6; i < 12; i++) TEST_ASSERT_EQUAL_UINT8(0xF0, out[i].g);
  for (uint16_t i = 12; i < 18; i++) TEST_ASSERT_EQUAL_UINT8(0xF0, out[i].b);
}

// An idle lion (empty stops) mirrors the Main colour.
void test_idle_lion_mirrors_main() {
  std::array<std::vector<Color>, 3> stops = {
    std::vector<Color>{},                       // idle
    std::vector<Color>{Color(0, 0xF0, 0, 0)},
    std::vector<Color>{},                       // idle
  };
  const Color main(0x20, 0x08, 0x80, 0);
  std::vector<Color> out;
  renderLions(out, 18, stops, main);
  for (uint16_t i = 0; i < 6; i++)  TEST_ASSERT_TRUE(out[i] == main);
  for (uint16_t i = 12; i < 18; i++) TEST_ASSERT_TRUE(out[i] == main);
  for (uint16_t i = 6; i < 12; i++)  TEST_ASSERT_EQUAL_UINT8(0xF0, out[i].g);
}

// A multi-stop peer palette becomes a 6px gradient within its zone (endpoints
// differ from the midpoint).
void test_multistop_peer_is_gradient() {
  std::array<std::vector<Color>, 3> stops = {
    std::vector<Color>{Color(0xFF, 0, 0, 0), Color(0, 0, 0xFF, 0)},
    std::vector<Color>{},
    std::vector<Color>{},
  };
  std::vector<Color> out;
  renderLions(out, 18, stops, Color(0, 0, 0, 0));
  TEST_ASSERT_FALSE(out[0] == out[5]);          // gradient spans the zone
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_three_zones_each_peer_colour);
  RUN_TEST(test_idle_lion_mirrors_main);
  RUN_TEST(test_multistop_peer_is_gradient);
  return UNITY_END();
}

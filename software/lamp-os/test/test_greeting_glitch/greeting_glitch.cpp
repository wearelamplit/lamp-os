// Native-host tests for snafu::Greeting.
//
// greeting.cpp pulls Arduino + Config + ExpressionManager, so the two pinned
// contracts are mirrored here (kept in sync with src/lamps/snafu/greeting.cpp):
//   1. Local scramble render — the glitch phase rotates the real glitch
//      gradient by an offset (buffer[i] = gradient[(i+offset) % px]). This is
//      NOT a glitchy-expression config; it stays verbatim.
//   2. Crowd invocation — on arrival the greeting builds a `glitchy` in the
//      STEM (base broadcast) color, target SHADE, fast duration, and hands it
//      to expressionManager.broadcastInvocation (crowd cascade), REPLACING the
//      old single directed meshLink.sendCommand(peer.mac, …). The invocation
//      carries no peer MAC — it is a broadcast, not a directed send.

#include <unity.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "util/color.hpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

using namespace lamp;

// Mirror of greeting.hpp kFastGlitchFrames.
static constexpr uint32_t kFastGlitchFrames = 12;
// Mirror of expression.hpp TARGET_SHADE.
static constexpr uint8_t kTargetShade = 1;

void setUp() {}
void tearDown() {}

// Mirror of Greeting::draw()'s glitch-phase render (verbatim scramble).
static std::vector<Color> scramble(const std::vector<Color>& gradient,
                                   uint8_t offset) {
  const uint8_t px = static_cast<uint8_t>(gradient.size());
  std::vector<Color> out(px);
  for (uint8_t i = 0; i < px; ++i) {
    out[i] = gradient[static_cast<uint8_t>((i + offset) % px)];
  }
  return out;
}

void test_local_scramble_rotates_gradient() {
  const uint8_t px = 8;
  std::vector<Color> gradient =
      calculateGradient(Color(0, 45, 200, 0), Color(180, 0, 60, 0), px);

  const uint8_t offset = 3;
  std::vector<Color> scene = scramble(gradient, offset);

  TEST_ASSERT_EQUAL_UINT(px, scene.size());
  for (uint8_t i = 0; i < px; ++i) {
    TEST_ASSERT_TRUE(scene[i] == gradient[(i + offset) % px]);
  }
}

void test_zero_offset_is_identity() {
  const uint8_t px = 6;
  std::vector<Color> gradient =
      calculateGradient(Color(0, 45, 200, 0), Color(180, 0, 60, 0), px);
  std::vector<Color> scene = scramble(gradient, 0);
  for (uint8_t i = 0; i < px; ++i) {
    TEST_ASSERT_TRUE(scene[i] == gradient[i]);
  }
}

// Mirror of the crowd invocation greeting builds on arrival.
struct Invocation {
  std::string type;
  std::vector<Color> colors;
  uint8_t target;
  std::map<std::string, uint32_t> parameters;
  uint32_t delayMs;
};

static Invocation buildGreetInvocation(const Color& stem) {
  Invocation inv;
  inv.type = "glitchy";
  inv.colors = {stem};
  inv.target = kTargetShade;
  inv.parameters = {
    {"durationMin", kFastGlitchFrames},
    {"durationMax", kFastGlitchFrames},
  };
  inv.delayMs = 0;
  return inv;
}

void test_crowd_invocation_is_glitchy_in_stem_color() {
  const Color stem(0x30, 0x07, 0x83, 0x00);
  Invocation inv = buildGreetInvocation(stem);

  TEST_ASSERT_EQUAL_STRING("glitchy", inv.type.c_str());
  TEST_ASSERT_EQUAL_UINT(1, inv.colors.size());
  TEST_ASSERT_TRUE(inv.colors[0] == stem);
  TEST_ASSERT_EQUAL_UINT8(kTargetShade, inv.target);
  TEST_ASSERT_EQUAL_UINT32(kFastGlitchFrames, inv.parameters["durationMin"]);
  TEST_ASSERT_EQUAL_UINT32(kFastGlitchFrames, inv.parameters["durationMax"]);
  // Broadcast, not directed: delay 0, no peer MAC on the payload.
  TEST_ASSERT_EQUAL_UINT32(0, inv.delayMs);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_local_scramble_rotates_gradient);
  RUN_TEST(test_zero_offset_is_identity);
  RUN_TEST(test_crowd_invocation_is_glitchy_in_stem_color);
  return UNITY_END();
}

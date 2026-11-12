// The pure rotation helpers: the closed loop, the role-scene choice, the
// shift, and the clock's timing, direction and bounds.

#include <unity.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "../../src/util/color.cpp"
#include "../../src/util/color_rotation.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"

using namespace lamp;

void setUp() {}
void tearDown() {}

static constexpr uint64_t kLoop = uint64_t{1} << 32;

static uint32_t perMs(uint8_t periodS) {
  return static_cast<uint32_t>(kLoop / (uint32_t{periodS} * 1000u));
}

static uint8_t channel(const Color& c, int ch) { return (&c.r)[ch]; }

static void assertColor(const Color& expected, const Color& actual) {
  TEST_ASSERT_EQUAL_HEX8(expected.r, actual.r);
  TEST_ASSERT_EQUAL_HEX8(expected.g, actual.g);
  TEST_ASSERT_EQUAL_HEX8(expected.b, actual.b);
  TEST_ASSERT_EQUAL_HEX8(expected.w, actual.w);
}

static void assertFrame(const std::vector<Color>& expected, const std::vector<Color>& actual) {
  TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
  for (size_t i = 0; i < expected.size(); ++i) assertColor(expected[i], actual[i]);
}

// Twelve stops with every channel rising and falling between neighbours.
static std::vector<Color> stops(size_t n) {
  static const Color kStops[] = {
      {0xFF, 0x00, 0x10, 0x80}, {0x00, 0xFF, 0x90, 0x05}, {0x7A, 0x13, 0xFF, 0xC4},
      {0x21, 0xE8, 0x02, 0x3B}, {0xD9, 0x44, 0x6E, 0xFF}, {0x03, 0x9B, 0xD1, 0x00},
      {0xA6, 0x01, 0x37, 0x92}, {0x58, 0xC7, 0xF0, 0x1D}, {0xEE, 0x2A, 0x05, 0x66},
      {0x11, 0x77, 0xB3, 0xDA}, {0x9C, 0xFE, 0x48, 0x0F}, {0x40, 0x09, 0x8D, 0xA1},
  };
  return std::vector<Color>(kStops, kStops + n);
}

static std::vector<Color> shifted(const std::vector<Color>& loop, uint32_t offset) {
  std::vector<Color> out(loop.size());
  for (size_t i = 0; i < loop.size(); ++i) out[i] = sampleLoop(loop, offset, i);
  return out;
}

// N segments over L pixels, split as the open gradient splits its N-1,
// each filled by the same linear fade, the closing one back to stop 0.
void test_loop_segments() {
  for (size_t n = 2; n <= 8; ++n) {
    for (int len : {1, 3, 7, 8, 9, 35, 38, 255}) {
      const auto px = static_cast<uint8_t>(len);
      const auto s = stops(n);
      const auto loop = buildGradientLoop(px, s);
      TEST_ASSERT_EQUAL_size_t(len, loop.size());
      size_t at = 0;
      for (size_t k = 0; k < n; ++k) {
        const size_t segLen = len / n + (k < len % n ? 1 : 0);
        for (size_t i = 0; i < segLen; ++i) {
          assertColor(fadeLinear(s[k], s[(k + 1) % n], segLen, i), loop[at + i]);
        }
        if (segLen > 0) assertColor(s[k], loop[at]);
        at += segLen;
      }
      TEST_ASSERT_EQUAL_size_t(len, at);
    }
  }
}

void test_loop_cuts_past_eight_stops() {
  for (int len : {12, 38, 255}) {
    const auto px = static_cast<uint8_t>(len);
    assertFrame(buildGradientLoop(px, stops(8)), buildGradientLoop(px, stops(12)));
  }
  const auto loop = buildGradientLoop(38, stops(12));
  // 38 px over 8 segments puts the last 4 px from stop 7 toward stop 0.
  for (size_t i = 0; i < 4; ++i) {
    assertColor(fadeLinear(stops(12)[7], stops(12)[0], 4, i), loop[34 + i]);
  }
}

void test_loop_shorter_than_its_stops() {
  assertFrame({stops(3)[0]}, buildGradientLoop(1, stops(3)));
  assertFrame({stops(8)[0], stops(8)[1], stops(8)[2]}, buildGradientLoop(3, stops(8)));
  TEST_ASSERT_EQUAL_size_t(0, buildGradientLoop(0, stops(4)).size());
}

// The wrap is an ordinary stop boundary. L-1 to 0 of loop(A, B) steps as
// L/2-1 to L/2 of loop(B, A).
void test_wrap_steps_like_a_stop_boundary() {
  const Color a(0x00, 0xFF, 0x00, 0xFF);
  const Color b(0xFF, 0x00, 0xFF, 0x00);
  for (int len : {2, 4, 38, 254}) {
    const auto px = static_cast<uint8_t>(len);
    const auto ab = buildGradientLoop(px, {a, b});
    const auto ba = buildGradientLoop(px, {b, a});
    for (int ch = 0; ch < 4; ++ch) {
      const int wrap = channel(ab[0], ch) - channel(ab[len - 1], ch);
      const int boundary = channel(ba[len / 2], ch) - channel(ba[len / 2 - 1], ch);
      TEST_ASSERT_EQUAL_INT(boundary, wrap);
    }
  }
}

void test_one_stop_with_fewer_than_two_stops_falls_back() {
  assertFrame(buildGradientWithStops(38, stops(1)), buildGradientLoop(38, stops(1)));
  assertFrame(buildGradientWithStops(38, {}), buildGradientLoop(38, {}));
}

void test_role_scene_one_stop_is_solid_and_keeps_the_setting() {
  const ColorRotation on{true, 17, RotationDir::Down};
  const Color c(0x12, 0xF0, 0x08, 0x77);
  const RoleScene scene = buildRoleScene(38, {c}, on);
  assertFrame(std::vector<Color>(38, c), scene.colors);
  TEST_ASSERT_FALSE(scene.rotation.on);
  TEST_ASSERT_EQUAL_UINT8(17, scene.rotation.periodS);
  TEST_ASSERT_TRUE(scene.rotation.dir == RotationDir::Down);
  TEST_ASSERT_TRUE(on.on);

  const RoleScene empty = buildRoleScene(38, {}, on);
  assertFrame(std::vector<Color>(38, Color()), empty.colors);
  TEST_ASSERT_FALSE(empty.rotation.on);
}

void test_role_scene_picks_loop_only_when_on() {
  const ColorRotation on{true, 9, RotationDir::Down};
  const RoleScene loop = buildRoleScene(35, stops(3), on);
  assertFrame(buildGradientLoop(35, stops(3)), loop.colors);
  TEST_ASSERT_TRUE(loop.rotation.on);
  TEST_ASSERT_EQUAL_UINT8(9, loop.rotation.periodS);
  TEST_ASSERT_TRUE(loop.rotation.dir == RotationDir::Down);

  const RoleScene open = buildRoleScene(35, stops(3), ColorRotation{});
  assertFrame(buildGradientWithStops(35, stops(3)), open.colors);
  TEST_ASSERT_FALSE(open.rotation.on);
}

// The scene keeps `on` exactly when the builders would close a loop.
void test_role_scene_counts_stops_as_the_builders_do() {
  for (size_t n : {0, 1, 2, 3, 8, 9, 255, 256, 257, 258, 300}) {
    std::vector<Color> s(n);
    for (size_t k = 0; k < n; ++k) {
      s[k] = Color(static_cast<uint8_t>(k * 37), static_cast<uint8_t>(255 - k * 11),
                   static_cast<uint8_t>(k * 5), static_cast<uint8_t>(k));
    }
    const RoleScene scene = buildRoleScene(20, s, ColorRotation{true, 9, RotationDir::Up});
    TEST_ASSERT_EQUAL(gradientStopCount(s) >= 2, scene.rotation.on);
    const auto loop = buildGradientLoop(20, s);
    const auto open = buildGradientWithStops(20, s);
    TEST_ASSERT_EQUAL(scene.rotation.on, !(loop == open));
    assertFrame(scene.rotation.on ? loop : open, scene.colors);
  }
  TEST_ASSERT_EQUAL_UINT8(8, gradientStopCount(std::vector<Color>(12)));
  TEST_ASSERT_EQUAL_UINT8(0, gradientStopCount(std::vector<Color>(256)));

  const ColorRotation on{true, 9, RotationDir::Up};
  for (size_t n : {256, 257}) {
    std::vector<Color> many(n, Color(1, 2, 3, 4));
    many[0] = Color(200, 0, 0, 0);
    const RoleScene scene = buildRoleScene(35, many, on);
    assertFrame(buildGradientWithStops(35, many), scene.colors);
    TEST_ASSERT_FALSE(scene.rotation.on);
  }
  std::vector<Color> wrapsToTwo(258, Color(1, 2, 3, 4));
  wrapsToTwo[0] = Color(200, 0, 0, 0);
  const RoleScene loop = buildRoleScene(35, wrapsToTwo, on);
  assertFrame(buildGradientLoop(35, wrapsToTwo), loop.colors);
  TEST_ASSERT_TRUE(loop.rotation.on);
}

void test_default_rotation() {
  const ColorRotation r;
  TEST_ASSERT_FALSE(r.on);
  TEST_ASSERT_EQUAL_UINT8(40, r.periodS);
  TEST_ASSERT_TRUE(r.dir == RotationDir::Up);
}

void test_shift_whole_pixels() {
  const auto loop = buildGradientLoop(38, stops(4));
  assertFrame(loop, shifted(loop, 0));
  for (uint32_t j : {1u, 5u, 37u}) {
    const auto out = shifted(loop, j * 256);
    for (size_t i = 0; i < loop.size(); ++i) assertColor(loop[(i + j) % 38], out[i]);
  }
}

void test_shift_between_pixels_blends_neighbours() {
  const auto loop = buildGradientLoop(38, stops(4));
  for (uint32_t frac : {1u, 64u, 128u, 200u, 255u}) {
    for (uint32_t j : {0u, 3u, 37u}) {
      const auto out = shifted(loop, j * 256 + frac);
      for (size_t i = 0; i < loop.size(); ++i) {
        const Color& a = loop[(i + j) % 38];
        const Color& b = loop[(i + j + 1) % 38];
        for (int ch = 0; ch < 4; ++ch) {
          const double exact =
              channel(a, ch) + (channel(b, ch) - channel(a, ch)) * (frac / 256.0);
          TEST_ASSERT_TRUE(std::abs(channel(out[i], ch) - exact) < 1.0);
        }
      }
    }
  }
}

void test_loop_offset_spans_the_loop() {
  TEST_ASSERT_EQUAL_UINT32(0, loopOffset(0, 38));
  TEST_ASSERT_EQUAL_UINT32(19 * 256, loopOffset(1u << 31, 38));
  TEST_ASSERT_EQUAL_UINT32(255 * 256 - 1, loopOffset(UINT32_MAX, 255));
  TEST_ASSERT_EQUAL_UINT32(0, loopOffset(UINT32_MAX, 1) >> 8);
}

// At the clock, toward index 0, stops reach pixel 0 in list order. Away,
// in reverse.
static std::vector<int> arrivalsAtPixelZero(bool towardZero) {
  const std::vector<Color> rgb = {{255, 0, 0, 0}, {0, 255, 0, 0}, {0, 0, 255, 0}};
  const auto loop = buildGradientLoop(38, rgb);
  RotationClock clock;
  clock.advance(0, 5, towardZero);
  std::vector<int> seen;
  for (uint32_t t = 16; t <= 5016; t += 16) {
    clock.advance(t, 5, towardZero);
    const Color c = sampleLoop(loop, loopOffset(clock.phase(), 38), 0);
    for (int ch = 0; ch < 3; ++ch) {
      if (channel(c, ch) >= 240 && (seen.empty() || seen.back() != ch)) seen.push_back(ch);
    }
  }
  return seen;
}

void test_direction_order_at_pixel_zero() {
  const std::vector<int> listOrder = {0, 1, 2, 0};
  const std::vector<int> reverse = {0, 2, 1, 0};
  const auto toward = arrivalsAtPixelZero(true);
  const auto away = arrivalsAtPixelZero(false);
  TEST_ASSERT_EQUAL_size_t(4, toward.size());
  TEST_ASSERT_EQUAL_INT_ARRAY(listOrder.data(), toward.data(), 4);
  TEST_ASSERT_EQUAL_size_t(4, away.size());
  TEST_ASSERT_EQUAL_INT_ARRAY(reverse.data(), away.data(), 4);
}

// Three spacings summing to the same time land on the same phase, across
// the millis() wrap.
void test_frame_rate_independence() {
  for (bool toward : {true, false}) {
    for (uint8_t period : {5, 40, 120}) {
      const uint32_t start = 0xFFFFF000u;
      const uint32_t total = 77777;
      RotationClock ones, frames, jitter;
      for (RotationClock* c : {&ones, &frames, &jitter}) c->advance(start, period, toward);
      for (uint32_t t = 1; t <= total; ++t) ones.advance(start + t, period, toward);
      uint32_t t = 0;
      while (t + 16 <= total) frames.advance(start + (t += 16), period, toward);
      frames.advance(start + total, period, toward);
      uint32_t seed = 12345;
      t = 0;
      while (t < total) {
        seed = seed * 1103515245u + 12345u;
        const uint32_t dt = 1 + (seed >> 16) % lampos::kMaxFrameDeltaMs;
        t = t + dt > total ? total : t + dt;
        jitter.advance(start + t, period, toward);
      }
      TEST_ASSERT_EQUAL_HEX32(ones.phase(), frames.phase());
      TEST_ASSERT_EQUAL_HEX32(ones.phase(), jitter.phase());
      const uint32_t expected = static_cast<uint32_t>(uint64_t{total} * perMs(period) +
                                                      lampos::kMaxFrameDeltaMs * perMs(period));
      TEST_ASSERT_EQUAL_HEX32(toward ? expected : 0u - expected, ones.phase());
    }
  }
}

// One period of draws comes back within 0.01 px on any strip up to 255.
void test_one_period_is_one_loop() {
  for (int period = kRotationMinPeriodS; period <= kRotationMaxPeriodS; ++period) {
    for (bool toward : {true, false}) {
      const auto p = static_cast<uint8_t>(period);
      RotationClock clock;
      clock.advance(1000, p, toward);
      clock.advance(1077, p, toward);
      const uint32_t start = clock.phase();
      const uint32_t end = 1077 + period * 1000u;
      uint32_t t = 1077;
      while (t + 16 <= end) clock.advance(t += 16, p, toward);
      clock.advance(end, p, toward);
      const uint32_t forward = clock.phase() - start;
      const uint32_t gap = forward < (1u << 31) ? forward : 0u - forward;
      TEST_ASSERT_TRUE(static_cast<double>(gap) * 255 / kLoop < 0.01);
    }
  }
}

void test_a_stall_resumes_where_it_stopped() {
  RotationClock fresh;
  fresh.advance(50000, 40, true);
  TEST_ASSERT_EQUAL_HEX32(lampos::kMaxFrameDeltaMs * perMs(40), fresh.phase());

  RotationClock clock;
  clock.advance(1000, 40, true);
  clock.advance(1016, 40, true);
  const uint32_t before = clock.phase();
  clock.advance(91016, 40, true);
  TEST_ASSERT_EQUAL_HEX32(lampos::kMaxFrameDeltaMs * perMs(40), clock.phase() - before);
  clock.advance(91032, 40, true);
  TEST_ASSERT_EQUAL_HEX32((lampos::kMaxFrameDeltaMs + 16) * perMs(40), clock.phase() - before);
}

void test_period_change_keeps_the_position() {
  RotationClock clock;
  uint32_t t = 1000;
  clock.advance(t, 40, true);
  while (t < 3000) clock.advance(t += 16, 40, true);
  const uint32_t before = clock.phase();
  clock.advance(t, 5, true);
  TEST_ASSERT_EQUAL_HEX32(before, clock.phase());
  clock.advance(t + 16, 5, true);
  TEST_ASSERT_EQUAL_HEX32(16 * perMs(5), clock.phase() - before);
}

void test_direction_flip_reverses_in_place() {
  RotationClock clock;
  uint32_t t = 1000;
  clock.advance(t, 7, true);
  while (t < 2000) clock.advance(t += 16, 7, true);
  const uint32_t before = clock.phase();
  clock.advance(t, 7, false);
  TEST_ASSERT_EQUAL_HEX32(before, clock.phase());
  for (int k = 0; k < 25; ++k) clock.advance(t += 16, 7, false);
  TEST_ASSERT_EQUAL_HEX32(before - 400 * perMs(7), clock.phase());
  for (int k = 0; k < 25; ++k) clock.advance(t += 16, 7, true);
  TEST_ASSERT_EQUAL_HEX32(before, clock.phase());
}

void test_reset_returns_to_the_settled_position() {
  RotationClock clock;
  clock.advance(1000, 5, true);
  clock.advance(1050, 5, true);
  TEST_ASSERT_NOT_EQUAL(0, clock.phase());
  clock.reset();
  TEST_ASSERT_EQUAL_HEX32(0, clock.phase());
  clock.advance(1066, 5, true);
  TEST_ASSERT_EQUAL_HEX32(16 * perMs(5), clock.phase());
}

void test_period_clamp() {
  struct Case {
    int32_t in;
    uint8_t out;
  };
  const Case cases[] = {{0, 5},     {-1, 5},     {4, 5},    {5, 5},       {6, 6},
                        {40, 40},   {119, 119},  {120, 120}, {121, 120}, {266, 120},
                        {70000, 120}, {INT32_MIN, 5}, {INT32_MAX, 120}};
  for (const Case& c : cases) TEST_ASSERT_EQUAL_UINT8(c.out, clampRotationPeriodS(c.in));
}

void test_clock_clamps_the_period_it_is_given() {
  for (uint8_t in : {0, 1, 4}) {
    RotationClock clock;
    clock.advance(1000, in, true);
    TEST_ASSERT_EQUAL_HEX32(lampos::kMaxFrameDeltaMs * perMs(5), clock.phase());
  }
  for (uint8_t in : {121, 255}) {
    RotationClock clock;
    clock.advance(1000, in, true);
    TEST_ASSERT_EQUAL_HEX32(lampos::kMaxFrameDeltaMs * perMs(120), clock.phase());
  }
}

// Equal 16 ms steps across a full loop that crosses the wrap move the
// offset by equal amounts within one unit, none of them zero.
void test_constant_flow() {
  for (int len : {38, 35, 255}) {
    for (uint8_t period : {5, 120}) {
      for (bool toward : {true, false}) {
        const uint32_t span = static_cast<uint32_t>(len) * 256;
        RotationClock clock;
        uint32_t t = 0;
        clock.advance(t, period, toward);
        for (uint32_t ms = 0; ms < period * 400u; ms += 100) clock.advance(t += 100, period, toward);
        uint32_t prev = loopOffset(clock.phase(), len);
        uint32_t lo = UINT32_MAX, hi = 0;
        bool wrapped = false;
        for (uint32_t ms = 0; ms <= period * 1000u + 16; ms += 16) {
          clock.advance(t += 16, period, toward);
          const uint32_t now = loopOffset(clock.phase(), len);
          TEST_ASSERT_TRUE(now < span);
          wrapped |= toward ? now < prev : now > prev;
          const uint32_t step = toward ? (now + span - prev) % span : (prev + span - now) % span;
          lo = step < lo ? step : lo;
          hi = step > hi ? step : hi;
          prev = now;
        }
        TEST_ASSERT_TRUE(wrapped);
        TEST_ASSERT_TRUE(lo > 0);
        TEST_ASSERT_TRUE(hi - lo <= 1);
      }
    }
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_loop_segments);
  RUN_TEST(test_loop_cuts_past_eight_stops);
  RUN_TEST(test_loop_shorter_than_its_stops);
  RUN_TEST(test_wrap_steps_like_a_stop_boundary);
  RUN_TEST(test_one_stop_with_fewer_than_two_stops_falls_back);
  RUN_TEST(test_role_scene_one_stop_is_solid_and_keeps_the_setting);
  RUN_TEST(test_role_scene_picks_loop_only_when_on);
  RUN_TEST(test_role_scene_counts_stops_as_the_builders_do);
  RUN_TEST(test_default_rotation);
  RUN_TEST(test_shift_whole_pixels);
  RUN_TEST(test_shift_between_pixels_blends_neighbours);
  RUN_TEST(test_loop_offset_spans_the_loop);
  RUN_TEST(test_direction_order_at_pixel_zero);
  RUN_TEST(test_frame_rate_independence);
  RUN_TEST(test_one_period_is_one_loop);
  RUN_TEST(test_a_stall_resumes_where_it_stopped);
  RUN_TEST(test_period_change_keeps_the_position);
  RUN_TEST(test_direction_flip_reverses_in_place);
  RUN_TEST(test_reset_returns_to_the_settled_position);
  RUN_TEST(test_period_clamp);
  RUN_TEST(test_clock_clamps_the_period_it_is_given);
  RUN_TEST(test_constant_flow);
  return UNITY_END();
}

// Native-host unit tests for hexStringToColor parsing.
//
// Audit finding [HIGH]: the previous implementation called std::stoul on
// substrings of the input, which throws std::invalid_argument on non-hex
// chars. Under Arduino-ESP32's default -fno-exceptions that throw turns
// into abort() — and the payload bytes here are attacker-reachable via
// BLE / ESP-NOW (shadeColors, baseColors, expressionOp.entry.colors,
// settings_blob.{base,shade}.colors). These tests pin down the expected
// behavior: a default-constructed Color on any invalid input, no abort.
//
// Following the convention established by test/test_gradient/gradient.cpp
// and test/test_invocation/expression_invocation.cpp, the function under
// test is re-implemented inline here. The native test env doesn't link
// against src/ (test_build_src = no for the upesy_wroom hardware test env,
// and the native env builds only the test sources). Production code in
// src/util/color.cpp mirrors the same contract.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace lamp {

class Color {
 public:
  uint8_t r, g, b, w;
  Color() : r(0), g(0), b(0), w(0) {}
  Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW)
      : r(inR), g(inG), b(inB), w(inW) {}
  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && w == o.w;
  }
};

Color hexStringToColor(std::string inHexString);
std::string colorToHexString(Color inColor);
Color colorFromHue(uint16_t hueDeg);

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// Valid input
// ---------------------------------------------------------------------------

void test_valid_uppercase_hex_parses_correctly() {
  lamp::Color result = lamp::hexStringToColor("#FF0080FF");
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, result.g);
  TEST_ASSERT_EQUAL_UINT8(0x80, result.b);
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.w);
}

void test_valid_lowercase_hex_parses_correctly() {
  lamp::Color result = lamp::hexStringToColor("#ff0080ff");
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, result.g);
  TEST_ASSERT_EQUAL_UINT8(0x80, result.b);
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.w);
}

void test_valid_mixed_case_hex_parses_correctly() {
  lamp::Color result = lamp::hexStringToColor("#Ff0A80fE");
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.r);
  TEST_ASSERT_EQUAL_UINT8(0x0A, result.g);
  TEST_ASSERT_EQUAL_UINT8(0x80, result.b);
  TEST_ASSERT_EQUAL_UINT8(0xFE, result.w);
}

void test_all_zeros_parses_correctly() {
  lamp::Color result = lamp::hexStringToColor("#00000000");
  TEST_ASSERT_EQUAL_UINT8(0x00, result.r);
  TEST_ASSERT_EQUAL_UINT8(0x00, result.g);
  TEST_ASSERT_EQUAL_UINT8(0x00, result.b);
  TEST_ASSERT_EQUAL_UINT8(0x00, result.w);
}

void test_all_ff_parses_correctly() {
  lamp::Color result = lamp::hexStringToColor("#FFFFFFFF");
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.r);
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.g);
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.b);
  TEST_ASSERT_EQUAL_UINT8(0xFF, result.w);
}

// ---------------------------------------------------------------------------
// Length mismatch (already current behavior; pinning to prevent regression)
// ---------------------------------------------------------------------------

void test_empty_string_returns_default() {
  lamp::Color result = lamp::hexStringToColor("");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_too_short_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FF00");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_too_long_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FF0080FFFF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_missing_hash_prefix_returns_default() {
  // Right length but no leading '#' — also invalid.
  lamp::Color result = lamp::hexStringToColor("FF0080FFF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

// ---------------------------------------------------------------------------
// Invalid hex chars — THIS is the audit-finding bug.
// Previously: std::stoul throws, abort() under -fno-exceptions.
// Required: return default Color, never abort.
// ---------------------------------------------------------------------------

void test_invalid_hex_all_g_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#GGGGGGGG");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_in_r_nibble_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#ZZ0080FF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_in_g_nibble_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FFZZ80FF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_in_b_nibble_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FF00ZZFF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_in_w_nibble_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FF0080ZZ");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_single_bad_char_returns_default() {
  // One bad char buried in the middle.
  lamp::Color result = lamp::hexStringToColor("#FF0X80FF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hex_special_chars_returns_default() {
  lamp::Color result = lamp::hexStringToColor("#FF00 0FF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

void test_invalid_hash_at_wrong_position_returns_default() {
  // Has 9 chars but '#' isn't first.
  lamp::Color result = lamp::hexStringToColor("F#F0080FF");
  TEST_ASSERT_TRUE(result == lamp::Color());
}

// ---------------------------------------------------------------------------
// colorToHexString — audit fix [HIGH]: replaced std::format with snprintf
// to drop tens of KB of <format>/locale machinery from flash. These pin the
// exact byte-for-byte output (lowercase, 9 chars including '#') so we don't
// accidentally regress case, length, or any downstream JSON consumer.
// ---------------------------------------------------------------------------

void test_color_to_hex_known_value_lowercase() {
  std::string s = lamp::colorToHexString(lamp::Color(0xFF, 0x00, 0x80, 0xFF));
  TEST_ASSERT_EQUAL_STRING("#ff0080ff", s.c_str());
  TEST_ASSERT_EQUAL_UINT32(9, s.size());
}

void test_color_to_hex_all_zeros() {
  std::string s = lamp::colorToHexString(lamp::Color(0, 0, 0, 0));
  TEST_ASSERT_EQUAL_STRING("#00000000", s.c_str());
  TEST_ASSERT_EQUAL_UINT32(9, s.size());
}

void test_color_to_hex_all_ff() {
  std::string s = lamp::colorToHexString(lamp::Color(0xFF, 0xFF, 0xFF, 0xFF));
  TEST_ASSERT_EQUAL_STRING("#ffffffff", s.c_str());
}

void test_color_round_trip() {
  lamp::Color in(0x12, 0x34, 0x56, 0x78);
  std::string s = lamp::colorToHexString(in);
  lamp::Color out = lamp::hexStringToColor(s);
  TEST_ASSERT_TRUE(in == out);
}

// ---------------------------------------------------------------------------
// colorFromHue — vivid HSV(hue, S=1, V=1) → RGB for fresh-lamp random colors.
// ---------------------------------------------------------------------------

void test_color_from_hue_primaries() {
  TEST_ASSERT_TRUE(lamp::colorFromHue(0)   == lamp::Color(255, 0, 0, 0));
  TEST_ASSERT_TRUE(lamp::colorFromHue(120) == lamp::Color(0, 255, 0, 0));
  TEST_ASSERT_TRUE(lamp::colorFromHue(240) == lamp::Color(0, 0, 255, 0));
}

void test_color_from_hue_secondary_and_wrap() {
  TEST_ASSERT_TRUE(lamp::colorFromHue(60) == lamp::Color(255, 255, 0, 0));
  TEST_ASSERT_TRUE(lamp::colorFromHue(360) == lamp::colorFromHue(0));  // wraps
}

void test_color_from_hue_is_always_vivid() {
  // Every hue maxes at least one channel and never touches white.
  for (uint16_t h = 0; h < 360; h += 17) {
    lamp::Color c = lamp::colorFromHue(h);
    TEST_ASSERT_TRUE(c.r == 255 || c.g == 255 || c.b == 255);
    TEST_ASSERT_EQUAL_UINT8(0, c.w);
  }
}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_valid_uppercase_hex_parses_correctly);
  RUN_TEST(test_valid_lowercase_hex_parses_correctly);
  RUN_TEST(test_valid_mixed_case_hex_parses_correctly);
  RUN_TEST(test_all_zeros_parses_correctly);
  RUN_TEST(test_all_ff_parses_correctly);

  RUN_TEST(test_empty_string_returns_default);
  RUN_TEST(test_too_short_returns_default);
  RUN_TEST(test_too_long_returns_default);
  RUN_TEST(test_missing_hash_prefix_returns_default);

  RUN_TEST(test_invalid_hex_all_g_returns_default);
  RUN_TEST(test_invalid_hex_in_r_nibble_returns_default);
  RUN_TEST(test_invalid_hex_in_g_nibble_returns_default);
  RUN_TEST(test_invalid_hex_in_b_nibble_returns_default);
  RUN_TEST(test_invalid_hex_in_w_nibble_returns_default);
  RUN_TEST(test_invalid_hex_single_bad_char_returns_default);
  RUN_TEST(test_invalid_hex_special_chars_returns_default);
  RUN_TEST(test_invalid_hash_at_wrong_position_returns_default);

  RUN_TEST(test_color_to_hex_known_value_lowercase);
  RUN_TEST(test_color_to_hex_all_zeros);
  RUN_TEST(test_color_to_hex_all_ff);
  RUN_TEST(test_color_round_trip);

  RUN_TEST(test_color_from_hue_primaries);
  RUN_TEST(test_color_from_hue_secondary_and_wrap);
  RUN_TEST(test_color_from_hue_is_always_vivid);

  return UNITY_END();
}

// ---------------------------------------------------------------------------
// Implementation under test.
//
// STEP 1 (RED): start with a port of the CURRENT broken implementation so
// the suite genuinely demonstrates the abort/throw bug. The "invalid hex"
// tests should fail (abort) here.
//
// STEP 2 (GREEN): swap to the validating 2-char-nibble parser below to
// make them pass, then mirror the same fix into src/util/color.cpp.
// ---------------------------------------------------------------------------

namespace lamp {

// GREEN: validating 2-char-nibble parser. No heap allocations (no substr),
// no std::stoul, no exception machinery. Any invalid char → default Color,
// matching the old wrong-length fallback. Production code in
// src/util/color.cpp mirrors this.
static inline int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static inline bool parseHexByte(const char* p, uint8_t& out) {
  int hi = hexNibble(p[0]);
  int lo = hexNibble(p[1]);
  if (hi < 0 || lo < 0) return false;
  out = (uint8_t)((hi << 4) | lo);
  return true;
}

Color hexStringToColor(std::string inHexString) {
  Color output;
  if (inHexString.size() != 9) return output;
  if (inHexString[0] != '#') return output;
  const char* s = inHexString.c_str();
  uint8_t r, g, b, w;
  if (!parseHexByte(s + 1, r)) return output;
  if (!parseHexByte(s + 3, g)) return output;
  if (!parseHexByte(s + 5, b)) return output;
  if (!parseHexByte(s + 7, w)) return output;
  output.r = r;
  output.g = g;
  output.b = b;
  output.w = w;
  return output;
}

// Mirror of the production snprintf-based implementation in
// src/util/color.cpp. 10-byte stack buffer, no std::format, lowercase.
std::string colorToHexString(Color inColor) {
  char buf[10];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x", inColor.r, inColor.g, inColor.b, inColor.w);
  return std::string(buf, 9);
}

// Mirror of src/util/color.cpp::colorFromHue.
Color colorFromHue(uint16_t hueDeg) {
  hueDeg %= 360;
  const uint8_t region = hueDeg / 60;
  const uint8_t rem = (hueDeg % 60) * 255 / 60;
  const uint8_t down = 255 - rem;
  switch (region) {
    case 0:  return Color(255, rem, 0, 0);
    case 1:  return Color(down, 255, 0, 0);
    case 2:  return Color(0, 255, rem, 0);
    case 3:  return Color(0, down, 255, 0);
    case 4:  return Color(rem, 0, 255, 0);
    default: return Color(255, 0, down, 0);
  }
}

}  // namespace lamp

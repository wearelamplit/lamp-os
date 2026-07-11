// parseColorList is a file-static in config.cpp; mirrored here for native testability
// (config.cpp pulls Arduino.h). Keep the mirror in sync.

#include <unity.h>

#include <cstdint>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

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
  out = static_cast<uint8_t>((hi << 4) | lo);
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
  output.r = r; output.g = g; output.b = b; output.w = w;
  return output;
}

std::vector<Color> parseColorList(const std::string& csv) {
  std::vector<Color> out;
  std::istringstream stream(csv);
  std::string token;
  while (std::getline(stream, token, ',')) {
    size_t start = token.find_first_not_of(' ');
    size_t end   = token.find_last_not_of(' ');
    if (start == std::string::npos) continue;
    out.push_back(hexStringToColor(token.substr(start, end - start + 1)));
  }
  return out;
}

}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

void test_single_hex_yields_one_element() {
  auto v = lamp::parseColorList("#30078300");
  TEST_ASSERT_EQUAL_UINT(1, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x07, v[0].g);
  TEST_ASSERT_EQUAL_UINT8(0x83, v[0].b);
  TEST_ASSERT_EQUAL_UINT8(0x00, v[0].w);
}

void test_two_hex_yields_two_elements() {
  auto v = lamp::parseColorList("#30078300,#64149600");
  TEST_ASSERT_EQUAL_UINT(2, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x64, v[1].r);
}

void test_four_hex_yields_four_elements() {
  auto v = lamp::parseColorList("#962d0000,#097d0400,#003c7800,#78003200");
  TEST_ASSERT_EQUAL_UINT(4, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x96, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x09, v[1].r);
  TEST_ASSERT_EQUAL_UINT8(0x00, v[2].r);
  TEST_ASSERT_EQUAL_UINT8(0x78, v[3].r);
}

void test_five_hex_yields_five_elements() {
  auto v = lamp::parseColorList("#9b000000,#965a0000,#407a0000,#00647800,#7a004e00");
  TEST_ASSERT_EQUAL_UINT(5, v.size());
}

void test_spaces_around_tokens_are_trimmed() {
  auto v = lamp::parseColorList(" #30078300 , #64149600 ");
  TEST_ASSERT_EQUAL_UINT(2, v.size());
  TEST_ASSERT_EQUAL_UINT8(0x30, v[0].r);
  TEST_ASSERT_EQUAL_UINT8(0x64, v[1].r);
}

void test_empty_string_yields_empty_vector() {
  auto v = lamp::parseColorList("");
  TEST_ASSERT_EQUAL_UINT(0, v.size());
}

void test_whitespace_only_token_is_skipped() {
  auto v = lamp::parseColorList("#30078300, ");
  TEST_ASSERT_EQUAL_UINT(1, v.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_hex_yields_one_element);
  RUN_TEST(test_two_hex_yields_two_elements);
  RUN_TEST(test_four_hex_yields_four_elements);
  RUN_TEST(test_five_hex_yields_five_elements);
  RUN_TEST(test_spaces_around_tokens_are_trimmed);
  RUN_TEST(test_empty_string_yields_empty_vector);
  RUN_TEST(test_whitespace_only_token_is_skipped);
  return UNITY_END();
}

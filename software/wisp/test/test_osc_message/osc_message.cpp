// Golden datagrams are Straylight's own Straylight.OSC.Encode output
// (lib/straylight/osc/encode.ex): the primary palette's 16 RGB colours as 48
// floats, channel k = k / 47.0, once bare and once inside the #bundle the feed
// sends beside its param messages (f, i, s and "fs" arguments).

#include <unity.h>

#include <cstring>
#include <vector>

#include "osc/osc_message.hpp"
#include "osc/slip_decoder.hpp"

using wisp::SlipDecoder;

void setUp() {}
void tearDown() {}

namespace {

const char* kPaletteAddr = "/straylight/palette/primary/colours";

const char kBareHex[] =
    "2f73747261796c696768742f70616c657474652f7072696d6172792f636f6c6f75727300"
    "2c6666666666666666666666666666666666666666666666666666666666666666666666"
    "66666666666666666666666666000000000000003cae4c413d2e4c413d82b9313dae4c41"
    "3dd9df523e02b9313e1882b93e2e4c413e4415ca3e59df523e6fa8da3e82b9313e8d9df5"
    "3e9882b93ea3677d3eae4c413eb931053ec415ca3ecefa8e3ed9df523ee4c4163eefa8da"
    "3efa8d9e3f02b9313f082b933f0d9df53f1310573f1882b93f1df51b3f23677d3f28d9df"
    "3f2e4c413f33bea33f3931053f3ea3673f4415ca3f49882c3f4efa8e3f546cf03f59df52"
    "3f5f51b43f64c4163f6a36783f6fa8da3f751b3c3f7a8d9e3f800000";

const char kBundleHex[] =
    "2362756e646c65000000000000000001000000202f73747261796c696768742f6d61696e"
    "2f6c6576656c00002c6600003f000000000000282f73747261796c696768742f6d61696e"
    "2f737065656400002c6673003e8000003235202500000000000000282f73747261796c69"
    "6768742f70616c657474652f7072696d6172792f696400002c6900000000000700000030"
    "2f73747261796c696768742f70616c657474652f7072696d6172792f6e616d6500000000"
    "2c7300004f6365616e000000000001182f73747261796c696768742f70616c657474652f"
    "7072696d6172792f636f6c6f757273002c66666666666666666666666666666666666666"
    "666666666666666666666666666666666666666666666666666666666600000000000000"
    "3cae4c413d2e4c413d82b9313dae4c413dd9df523e02b9313e1882b93e2e4c413e4415ca"
    "3e59df523e6fa8da3e82b9313e8d9df53e9882b93ea3677d3eae4c413eb931053ec415ca"
    "3ecefa8e3ed9df523ee4c4163eefa8da3efa8d9e3f02b9313f082b933f0d9df53f131057"
    "3f1882b93f1df51b3f23677d3f28d9df3f2e4c413f33bea33f3931053f3ea3673f4415ca"
    "3f49882c3f4efa8e3f546cf03f59df523f5f51b43f64c4163f6a36783f6fa8da3f751b3c"
    "3f7a8d9e3f800000";

std::vector<uint8_t> unhex(const char* h) {
  std::vector<uint8_t> out;
  for (size_t i = 0; h[i] && h[i + 1]; i += 2) {
    auto nib = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
    out.push_back(static_cast<uint8_t>(nib(h[i]) << 4 | nib(h[i + 1])));
  }
  return out;
}

void assertGoldenPalette(const float* f, size_t n) {
  TEST_ASSERT_EQUAL_UINT(48, n);
  for (size_t k = 0; k < 48; ++k) {
    TEST_ASSERT_EQUAL_FLOAT(static_cast<float>(k / 47.0), f[k]);
  }
}

struct Seen {
  int count = 0;
  char addrs[8][48];
};

void record(const wisp::OscMessage& m, void* ctx) {
  auto* s = static_cast<Seen*>(ctx);
  if (s->count < 8) std::strncpy(s->addrs[s->count], m.address, 47);
  s->count++;
}

}  // namespace

void test_bare_straylight_palette(void) {
  const auto d = unhex(kBareHex);
  TEST_ASSERT_EQUAL_UINT(280, d.size());
  wisp::OscMessage m;
  TEST_ASSERT_TRUE(wisp::oscParseMessage(d.data(), d.size(), m));
  TEST_ASSERT_EQUAL_STRING(kPaletteAddr, m.address);
  TEST_ASSERT_EQUAL_UINT(48, std::strlen(m.tags));
  float f[150];
  size_t n = 0;
  TEST_ASSERT_TRUE(wisp::oscFindFloats(d.data(), d.size(), kPaletteAddr, f, 150, n));
  assertGoldenPalette(f, n);
}

void test_bundled_straylight_palette(void) {
  const auto d = unhex(kBundleHex);
  Seen seen;
  TEST_ASSERT_TRUE(wisp::oscForEachMessage(d.data(), d.size(), record, &seen));
  TEST_ASSERT_EQUAL_INT(5, seen.count);
  TEST_ASSERT_EQUAL_STRING("/straylight/main/level", seen.addrs[0]);
  TEST_ASSERT_EQUAL_STRING(kPaletteAddr, seen.addrs[4]);
  float f[150];
  size_t n = 0;
  TEST_ASSERT_TRUE(wisp::oscFindFloats(d.data(), d.size(), kPaletteAddr, f, 150, n));
  assertGoldenPalette(f, n);
}

void test_float_args_convert_int_and_skip_string(void) {
  const auto d = unhex(kBundleHex);
  float f[4];
  size_t n = 0;
  // "/straylight/main/speed" ,fs 0.25 "25 %"
  TEST_ASSERT_TRUE(wisp::oscFindFloats(d.data(), d.size(), "/straylight/main/speed", f, 4, n));
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_FLOAT(0.25f, f[0]);
  TEST_ASSERT_TRUE(wisp::oscFindFloats(d.data(), d.size(), "/straylight/palette/primary/id", f, 4, n));
  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_EQUAL_FLOAT(7.0f, f[0]);
}

void test_nested_bundle(void) {
  const auto inner = unhex(kBundleHex);
  std::vector<uint8_t> outer = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0,
                                0, 0, 0, 0, 0, 0, 0, 1};
  const uint32_t sz = static_cast<uint32_t>(inner.size());
  outer.push_back(sz >> 24); outer.push_back(sz >> 16);
  outer.push_back(sz >> 8);  outer.push_back(sz);
  outer.insert(outer.end(), inner.begin(), inner.end());
  float f[150];
  size_t n = 0;
  TEST_ASSERT_TRUE(wisp::oscFindFloats(outer.data(), outer.size(), kPaletteAddr, f, 150, n));
  assertGoldenPalette(f, n);
}

void test_rejects_truncation_and_overflow(void) {
  const auto d = unhex(kBareHex);
  float f[150];
  size_t n = 0;
  for (size_t cut = 4; cut < d.size(); cut += 4) {
    TEST_ASSERT_FALSE(wisp::oscFindFloats(d.data(), cut, kPaletteAddr, f, 150, n));
  }
  TEST_ASSERT_FALSE(wisp::oscFindFloats(d.data(), d.size(), kPaletteAddr, f, 47, n));
  // Bundle element size larger than what remains.
  auto b = unhex(kBundleHex);
  b[16] = 0x7f;
  TEST_ASSERT_FALSE(wisp::oscFindFloats(b.data(), b.size(), kPaletteAddr, f, 150, n));
}

void test_unknown_type_tag_rejected(void) {
  const uint8_t msg[] = {'/', 'x', 0, 0, ',', 'b', 0, 0, 0, 0, 0, 0};
  float f[4];
  size_t n = 0;
  TEST_ASSERT_FALSE(wisp::oscFindFloats(msg, sizeof msg, "/x", f, 4, n));
}

void test_address_prefix_found_in_bundle(void) {
  const auto b = unhex(kBundleHex);
  TEST_ASSERT_TRUE(wisp::oscHasAddressPrefix(b.data(), b.size(), "/straylight/"));
  TEST_ASSERT_FALSE(wisp::oscHasAddressPrefix(b.data(), b.size(), "/other/"));
  const uint8_t garbage[8] = {'x', 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_FALSE(wisp::oscHasAddressPrefix(garbage, sizeof garbage, "/"));
}

void test_classify_feed_packet(void) {
  using wisp::OscFeedPacket;
  const auto b = unhex(kBundleHex);
  float f[150];
  size_t n = 0;
  // The role's palette is in the bundle.
  TEST_ASSERT_EQUAL_INT((int)OscFeedPacket::Palette,
      (int)wisp::oscClassifyFeedPacket(b.data(), b.size(), kPaletteAddr,
                                       "/straylight/", f, 150, n));
  TEST_ASSERT_EQUAL_UINT(48, n);
  // Another role: the rest of the feed only.
  TEST_ASSERT_EQUAL_INT((int)OscFeedPacket::Feed,
      (int)wisp::oscClassifyFeedPacket(b.data(), b.size(),
                                       "/straylight/palette/fill/colours",
                                       "/straylight/", f, 150, n));
  // This role's palette with a type tag the reader rejects.
  std::vector<uint8_t> bad(kPaletteAddr, kPaletteAddr + std::strlen(kPaletteAddr));
  while (bad.size() % 4 != 0 || bad.size() <= std::strlen(kPaletteAddr)) {
    bad.push_back(0);
  }
  for (char c : {',', 'T', '\0', '\0'}) bad.push_back(static_cast<uint8_t>(c));
  TEST_ASSERT_EQUAL_INT((int)OscFeedPacket::Other,
      (int)wisp::oscClassifyFeedPacket(bad.data(), bad.size(), kPaletteAddr,
                                       "/straylight/", f, 150, n));
  TEST_ASSERT_EQUAL_UINT(0, n);
  // Nothing from the feed.
  const uint8_t garbage[8] = {'x', 0, 0, 0, 0, 0, 0, 0};
  TEST_ASSERT_EQUAL_INT((int)OscFeedPacket::Other,
      (int)wisp::oscClassifyFeedPacket(garbage, sizeof garbage, kPaletteAddr,
                                       "/straylight/", f, 150, n));
}

void test_slip_frame_with_escapes(void) {
  static SlipDecoder slip;
  const uint8_t payload[] = {0x01, 0xC0, 0x02, 0xDB, 0x03};
  const uint8_t wire[] = {'h', 'i', 0xC0, 0x01, 0xDB, 0xDC, 0x02,
                          0xDB, 0xDD, 0x03, 0xC0, 'o', 'k'};
  int frames = 0;
  std::vector<uint8_t> text;
  for (uint8_t b : wire) {
    switch (slip.feed(b, 0)) {
      case SlipDecoder::Result::Text: text.push_back(b); break;
      case SlipDecoder::Result::Frame:
        ++frames;
        TEST_ASSERT_EQUAL_UINT(sizeof payload, slip.frameLen());
        TEST_ASSERT_EQUAL_MEMORY(payload, slip.frame(), sizeof payload);
        break;
      case SlipDecoder::Result::Consumed: break;
    }
  }
  TEST_ASSERT_EQUAL_INT(1, frames);
  TEST_ASSERT_EQUAL_UINT(4, text.size());
  TEST_ASSERT_EQUAL_MEMORY("hiok", text.data(), 4);
}

void test_slip_back_to_back_and_oversize(void) {
  static SlipDecoder slip;
  int frames = 0;
  auto feedAll = [&](const std::vector<uint8_t>& v) {
    for (uint8_t b : v) {
      if (slip.feed(b, 0) == SlipDecoder::Result::Frame) ++frames;
    }
  };
  feedAll({0xC0, 'a', 0xC0, 0xC0, 'b', 0xC0});
  TEST_ASSERT_EQUAL_INT(2, frames);

  // END END is an empty frame: the decoder keeps collecting rather than
  // emitting nothing and dropping to text.
  feedAll({0xC0, 0xC0, 'c', 0xC0});
  TEST_ASSERT_EQUAL_INT(3, frames);
  TEST_ASSERT_EQUAL_UINT(1, slip.frameLen());

  std::vector<uint8_t> big(SlipDecoder::kMaxFrame + 10, 'x');
  big.insert(big.begin(), 0xC0);
  big.push_back(0xC0);
  feedAll(big);
  TEST_ASSERT_EQUAL_INT(3, frames);
  // Back on text after the oversize frame's closing END.
  TEST_ASSERT_TRUE(slip.feed('z', 0) == SlipDecoder::Result::Text);
}

void test_slip_idle_reset_returns_to_text(void) {
  static SlipDecoder slip;
  // A frame opened and cut off mid-stream.
  TEST_ASSERT_TRUE(slip.feed(0xC0, 1000) == SlipDecoder::Result::Consumed);
  TEST_ASSERT_TRUE(slip.feed('/', 1001) == SlipDecoder::Result::Consumed);
  // Inside the idle window the decoder is still collecting.
  TEST_ASSERT_TRUE(slip.feed('a', 1001 + SlipDecoder::kIdleResetMs - 1) ==
                   SlipDecoder::Result::Consumed);
  // Silent for the idle window: the next byte is console text again.
  const uint32_t later = 1001 + 2 * SlipDecoder::kIdleResetMs;
  TEST_ASSERT_TRUE(slip.feed('p', later) == SlipDecoder::Result::Text);
  // And a fresh frame still decodes.
  int frames = 0;
  for (uint8_t b : {0xC0, 0x01, 0x02, 0xC0}) {
    if (slip.feed(b, later + 1) == SlipDecoder::Result::Frame) ++frames;
  }
  TEST_ASSERT_EQUAL_INT(1, frames);
  TEST_ASSERT_EQUAL_UINT(2, slip.frameLen());
}

void test_slip_carries_straylight_bundle(void) {
  static SlipDecoder slip;
  const auto d = unhex(kBundleHex);
  std::vector<uint8_t> wire = {0xC0};
  for (uint8_t b : d) {
    if (b == 0xC0) { wire.push_back(0xDB); wire.push_back(0xDC); }
    else if (b == 0xDB) { wire.push_back(0xDB); wire.push_back(0xDD); }
    else wire.push_back(b);
  }
  wire.push_back(0xC0);
  bool got = false;
  for (uint8_t b : wire) {
    if (slip.feed(b, 0) == SlipDecoder::Result::Frame) {
      got = true;
      TEST_ASSERT_EQUAL_UINT(d.size(), slip.frameLen());
      TEST_ASSERT_EQUAL_MEMORY(d.data(), slip.frame(), d.size());
    }
  }
  TEST_ASSERT_TRUE(got);
}

// A frame cut off mid-way, then silence for a whole wrap: the next byte is
// console text, not the frame's continuation.
void test_slip_partial_frame_resets_a_wrap_later(void) {
  static SlipDecoder slip;
  const uint32_t cutMs = 1000;
  slip.feed(SlipDecoder::kEnd, cutMs);
  slip.feed('x', cutMs);
  for (uint64_t t = cutMs; t < cutMs + (1ull << 32); t += 1ull << 28) {
    slip.retire(static_cast<uint32_t>(t));
  }
  TEST_ASSERT_TRUE(slip.feed('a', cutMs + 10) == SlipDecoder::Result::Text);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_bare_straylight_palette);
  RUN_TEST(test_bundled_straylight_palette);
  RUN_TEST(test_float_args_convert_int_and_skip_string);
  RUN_TEST(test_nested_bundle);
  RUN_TEST(test_rejects_truncation_and_overflow);
  RUN_TEST(test_unknown_type_tag_rejected);
  RUN_TEST(test_address_prefix_found_in_bundle);
  RUN_TEST(test_classify_feed_packet);
  RUN_TEST(test_slip_frame_with_escapes);
  RUN_TEST(test_slip_back_to_back_and_oversize);
  RUN_TEST(test_slip_carries_straylight_bundle);
  RUN_TEST(test_slip_idle_reset_returns_to_text);
  RUN_TEST(test_slip_partial_frame_resets_a_wrap_later);
  return UNITY_END();
}

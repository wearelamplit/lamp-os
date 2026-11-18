// Golden datagrams are Straylight's own Straylight.OSC.Encode output
// (lib/straylight/osc/encode.ex): the primary palette's 16 RGB colours as 48
// floats, channel k = k / 47.0, once bare and once inside the #bundle the feed
// sends beside its param messages (f, i, s and "fs" arguments).

#include <unity.h>

#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "osc/osc_freshness.hpp"
#include "osc/osc_listener.hpp"
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

// One OSC message to `address` carrying `floats` f-tagged arguments, channel
// k = k / floats.
std::vector<uint8_t> floatsMessage(const char* address,
                                   const std::vector<float>& values) {
  std::vector<uint8_t> out(address, address + std::strlen(address));
  do out.push_back(0); while (out.size() % 4 != 0);
  out.push_back(',');
  for (size_t i = 0; i < values.size(); ++i) out.push_back('f');
  do out.push_back(0); while (out.size() % 4 != 0);
  for (float v : values) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    for (int shift = 24; shift >= 0; shift -= 8) {
      out.push_back(static_cast<uint8_t>(bits >> shift));
    }
  }
  return out;
}

std::vector<uint8_t> floatMessage(const char* address, size_t floats) {
  std::vector<float> values(floats);
  for (size_t i = 0; i < floats; ++i) {
    values[i] = static_cast<float>(i) / static_cast<float>(floats);
  }
  return floatsMessage(address, values);
}

// One OSC message to `address` with type tags `tags` (after the ','),
// each `i` taking the next of `ints` and each `f` the next of `floats`.
std::vector<uint8_t> typedMessage(const char* address, const char* tags,
                                  const std::vector<int32_t>& ints,
                                  const std::vector<float>& floats) {
  std::vector<uint8_t> out(address, address + std::strlen(address));
  do out.push_back(0); while (out.size() % 4 != 0);
  out.push_back(',');
  for (const char* t = tags; *t; ++t) out.push_back(static_cast<uint8_t>(*t));
  do out.push_back(0); while (out.size() % 4 != 0);
  size_t ii = 0, fi = 0;
  for (const char* t = tags; *t; ++t) {
    uint32_t bits = 0;
    if (*t == 'i') {
      bits = static_cast<uint32_t>(ints[ii++]);
    } else if (*t == 'f') {
      std::memcpy(&bits, &floats[fi++], sizeof bits);
    } else {
      continue;
    }
    for (int shift = 24; shift >= 0; shift -= 8) {
      out.push_back(static_cast<uint8_t>(bits >> shift));
    }
  }
  return out;
}

// A Limbic float-layout message: `layout` as an OSC string, then `floats`
// f-tagged (or `tags` after the 's', when given; an `s` there is an empty
// string).
std::vector<uint8_t> layoutMessage(const char* address, const char* layout,
                                   const std::vector<float>& floats,
                                   const char* tags = nullptr) {
  std::vector<uint8_t> out(address, address + std::strlen(address));
  do out.push_back(0); while (out.size() % 4 != 0);
  const std::string tagStr =
      std::string("s") + (tags ? std::string(tags) : std::string(floats.size(), 'f'));
  out.push_back(',');
  for (char t : tagStr) out.push_back(static_cast<uint8_t>(t));
  do out.push_back(0); while (out.size() % 4 != 0);
  for (const char* c = layout; *c; ++c) out.push_back(static_cast<uint8_t>(*c));
  do out.push_back(0); while (out.size() % 4 != 0);
  size_t fi = 0;
  for (char t : tagStr.substr(1)) {
    uint32_t bits = 0;
    if (t == 'f') {
      std::memcpy(&bits, &floats[fi++], sizeof bits);
    } else if (t == 'i') {
      bits = static_cast<uint32_t>(static_cast<int32_t>(floats[fi++]));
    } else if (t == 's') {
      for (int k = 0; k < 4; ++k) out.push_back(0);  // an empty string
      continue;
    } else {
      continue;
    }
    for (int shift = 24; shift >= 0; shift -= 8) {
      out.push_back(static_cast<uint8_t>(bits >> shift));
    }
  }
  return out;
}

std::vector<uint8_t> hexMessage(const char* address,
                                const std::vector<int32_t>& ints) {
  return typedMessage(address, std::string(ints.size(), 'i').c_str(), ints,
                      {});
}

wisp::OscColours findColours(const std::vector<uint8_t>& d,
                             const char* address, wisp::PaletteColor* out,
                             size_t& n) {
  const char* tags = nullptr;
  return wisp::oscFindColours(d.data(), d.size(), address, out,
                              wisp::kOscPaletteMaxColors, n, tags);
}

void assertColour(float r, float g, float b, float w,
                  const wisp::PaletteColor& c) {
  TEST_ASSERT_EQUAL_FLOAT(r, c.r);
  TEST_ASSERT_EQUAL_FLOAT(g, c.g);
  TEST_ASSERT_EQUAL_FLOAT(b, c.b);
  TEST_ASSERT_EQUAL_FLOAT(w, c.w);
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

// The OSC palette cap, kOscPaletteMaxColors, is 50 colours, wider than the
// Manual palette's. A 50-colour Straylight palette decodes whole at that
// capacity and a 51-colour one is refused whole.
void test_osc_cap_is_50_colours(void) {
  const auto d = floatMessage(kPaletteAddr, 50 * 3);
  std::vector<float> f(wisp::kOscPaletteMaxColors * 3);
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscFeedPacket::Palette,
      (int)wisp::oscClassifyFeedPacket(d.data(), d.size(), kPaletteAddr,
                                       "/straylight/", f.data(), f.size(), n));
  TEST_ASSERT_EQUAL_UINT(50 * 3, n);
  TEST_ASSERT_EQUAL_FLOAT(149.0f / 150.0f, f[149]);
  const auto over = floatMessage(kPaletteAddr, 51 * 3);
  TEST_ASSERT_EQUAL_INT((int)wisp::OscFeedPacket::Other,
      (int)wisp::oscClassifyFeedPacket(over.data(), over.size(), kPaletteAddr,
                                       "/straylight/", f.data(), f.size(), n));
}

// One NaN or infinity anywhere refuses the whole message.
void test_non_finite_float_refuses_whole_message(void) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  const std::vector<std::vector<float>> bad = {
      {nan, 1, 0},          {1, 0, 0, 0, nan, 0}, {inf, 0, 0},
      {1, 0, 0, 0, inf, 0}, {-inf, 1, 0},         {1, 0, 0, 0, 1, -inf},
  };
  for (const auto& values : bad) {
    const auto d = floatsMessage(kPaletteAddr, values);
    float f[150];
    size_t n = 99;
    TEST_ASSERT_FALSE(
        wisp::oscFindFloats(d.data(), d.size(), kPaletteAddr, f, 150, n));
    TEST_ASSERT_EQUAL_INT((int)wisp::OscFeedPacket::Other,
        (int)wisp::oscClassifyFeedPacket(d.data(), d.size(), kPaletteAddr,
                                         "/straylight/", f, 150, n));
  }
  const auto ok = floatsMessage(kPaletteAddr, {1, 0, 0, 0, 1, 0});
  float f[150];
  size_t n = 0;
  TEST_ASSERT_TRUE(
      wisp::oscFindFloats(ok.data(), ok.size(), kPaletteAddr, f, 150, n));
  TEST_ASSERT_EQUAL_UINT(6, n);
}

// Limbic hex: one 0xRRGGBB per int, each byte over 255, white 0.
void test_hex_ints_read_one_colour_each(void) {
  const char* addr = "/palette/live/3";
  const auto d = hexMessage(addr, {0xFF0000, 0x00FF00, 0x0000FF, 0x336699});
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(d, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(4, n);
  assertColour(1, 0, 0, 0, c[0]);
  assertColour(0, 1, 0, 0, c[1]);
  assertColour(0, 0, 1, 0, c[2]);
  assertColour(0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 0, c[3]);
}

// Only the low 24 bits carry the colour, whatever sits above them.
void test_hex_ignores_the_top_byte(void) {
  const char* addr = "/palette/live/1";
  const auto d = hexMessage(addr, {static_cast<int32_t>(0xFF102030u), -1});
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(d, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(2, n);
  assertColour(0x10 / 255.0f, 0x20 / 255.0f, 0x30 / 255.0f, 0, c[0]);
  assertColour(1, 1, 1, 0, c[1]);
}

// Floats, or floats mixed with ints, read as RGB in threes, clamped to 0..1.
void test_rgb_floats_read_in_threes(void) {
  const char* addr = "/palette/live/2";
  const auto d = typedMessage(addr, "fffiff", {1},
                              {0.25f, 0.5f, 0.75f, 2.0f, -1.0f});
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(d, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(2, n);
  assertColour(0.25f, 0.5f, 0.75f, 0, c[0]);
  assertColour(1, 1, 0, 0, c[1]);
}

// The address matches exactly: slot 1 never takes slot 10 or a longer path.
void test_colours_match_the_address_exactly(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  for (const char* other :
       {"/palette/live/10", "/palette/live/1/x", "/palette/live/", "/palette/live/01"}) {
    const auto d = hexMessage(other, {0xFF0000});
    TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Absent,
                          (int)findColours(d, "/palette/live/1", c, n));
  }
  const auto d = hexMessage("/palette/live/10", {0xFF0000});
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(d, "/palette/live/10", c, n));
}

// A message the decoder cannot read whole is refused and fills nothing.
void test_unreadable_colours_refused(void) {
  const char* addr = "/palette/live/4";
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  std::vector<std::vector<uint8_t>> bad = {
      typedMessage(addr, "fff", {}, {nan, 0, 0}),
      typedMessage(addr, "fffifff", {1}, {0, 0, 0, 0, 0, inf}),
      typedMessage(addr, "ifs", {1}, {0.5f}),
      typedMessage(addr, "ffff", {}, {0, 0, 0, 0}),
      typedMessage(addr, "", {}, {}),
      hexMessage(addr, std::vector<int32_t>(wisp::kOscPaletteMaxColors + 1,
                                            0x112233)),
      typedMessage(addr, "fT", {}, {0.5f}),
  };
  // Arguments that run past the type tags.
  auto trailing = hexMessage(addr, {0x112233});
  for (int i = 0; i < 4; ++i) trailing.push_back(0);
  bad.push_back(trailing);
  // Arguments that end before the tags do.
  auto shortArgs = hexMessage(addr, {0x112233, 0x445566});
  shortArgs.resize(shortArgs.size() - 4);
  bad.push_back(shortArgs);
  for (const auto& d : bad) {
    wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
    size_t n = 99;
    const char* tags = nullptr;
    TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Refused,
        (int)wisp::oscFindColours(d.data(), d.size(), addr, c,
                                  wisp::kOscPaletteMaxColors, n, tags));
    TEST_ASSERT_EQUAL_UINT(0, n);
    TEST_ASSERT_NOT_NULL(tags);
  }
}

// The OSC cap holds on the hex path too: 50 ints read, 51 refuse.
void test_hex_cap_is_50_colours(void) {
  const char* addr = "/palette/live/5";
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  const auto fifty =
      hexMessage(addr, std::vector<int32_t>(wisp::kOscPaletteMaxColors, 0x0A0B0C));
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(fifty, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(wisp::kOscPaletteMaxColors, n);
}

// In a bundle the first readable message on the address wins; a refused one
// before it does not stop the read.
void test_bundle_takes_first_readable_colours(void) {
  const char* addr = "/palette/live/6";
  const auto bad = typedMessage(addr, "ff", {}, {0.5f, 0.5f});
  const auto good = hexMessage(addr, {0x00FF00});
  std::vector<uint8_t> b = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0,
                            0, 0, 0, 0, 0, 0, 0, 1};
  for (const auto* m : {&bad, &good}) {
    const uint32_t sz = static_cast<uint32_t>(m->size());
    b.push_back(sz >> 24); b.push_back(sz >> 16);
    b.push_back(sz >> 8);  b.push_back(sz);
    b.insert(b.end(), m->begin(), m->end());
  }
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(b, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(1, n);
  assertColour(0, 1, 0, 0, c[0]);

  // A readable message, then a refused one and another readable one: the
  // first still wins.
  const auto other = hexMessage(addr, {0x0000FF, 0x0000FF});
  std::vector<uint8_t> b2 = {'#', 'b', 'u', 'n', 'd', 'l', 'e', 0,
                             0, 0, 0, 0, 0, 0, 0, 1};
  for (const auto* m : {&good, &bad, &other}) {
    const uint32_t sz = static_cast<uint32_t>(m->size());
    b2.push_back(sz >> 24); b2.push_back(sz >> 16);
    b2.push_back(sz >> 8);  b2.push_back(sz);
    b2.insert(b2.end(), m->begin(), m->end());
  }
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(b2, addr, c, n));
  TEST_ASSERT_EQUAL_UINT(1, n);
  assertColour(0, 1, 0, 0, c[0]);
}

namespace {

constexpr const char* kLayoutAddr = "/palette/live/7";

// Decodes `d` on kLayoutAddr; fails the test unless it reads.
size_t readLayoutOk(const std::vector<uint8_t>& d, wisp::PaletteColor* c) {
  size_t n = 0;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Read,
                        (int)findColours(d, kLayoutAddr, c, n));
  return n;
}

void assertRefused(const std::vector<uint8_t>& d) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  size_t n = 99;
  TEST_ASSERT_EQUAL_INT((int)wisp::OscColours::Refused,
                        (int)findColours(d, kLayoutAddr, c, n));
  TEST_ASSERT_EQUAL_UINT(0, n);
}

}  // namespace

// Straylight's own Maestro send: `rgbau`, 16 colours of five floats, amber
// and UV as hundredths over 100.
void test_layout_straylight_rgbau_reads_16_colours(void) {
  std::vector<float> f;
  for (int i = 0; i < 16; ++i) {
    const float r = i / 16.0f, g = 1.0f - i / 16.0f, b = 0.5f;
    f.insert(f.end(), {r, g, b, 0.0f, 0.3f});
  }
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(16, readLayoutOk(layoutMessage(kLayoutAddr, "rgbau", f), c));
  for (int i = 0; i < 16; ++i) {
    assertColour(i / 16.0f, 1.0f - i / 16.0f, 0.5f, 0, c[i]);
  }
}

// w lands on the lamps' white LED, r g b as sent, wherever w sits.
void test_layout_white_drives_white(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(2, readLayoutOk(
      layoutMessage(kLayoutAddr, "rgbw", {0.1f, 0.2f, 0.3f, 1.0f,
                                          0.0f, 0.0f, 0.0f, 0.4f}), c));
  assertColour(0.1f, 0.2f, 0.3f, 1.0f, c[0]);
  assertColour(0, 0, 0, 0.4f, c[1]);
  TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
      layoutMessage(kLayoutAddr, "wbgr", {0.6f, 0.3f, 0.2f, 0.1f}), c));
  assertColour(0.1f, 0.2f, 0.3f, 0.6f, c[0]);
}

// Amber, spelled `am` or `a`, mixes into red at full and green at
// kOscAmberGreen; `rgbam` is r g b amber, not r g b a m.
void test_layout_amber_mixes_into_red_and_green(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  const float a = 0.4f;
  for (const char* layout : {"rgbam", "rgba"}) {
    TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
        layoutMessage(kLayoutAddr, layout, {0.1f, 0.2f, 0.3f, a}), c));
    assertColour(0.1f + a, 0.2f + wisp::kOscAmberGreen * a, 0.3f, 0, c[0]);
  }
  TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
      layoutMessage(kLayoutAddr, "amrgb", {a, 0, 0, 0}), c));
  assertColour(a, wisp::kOscAmberGreen * a, 0, 0, c[0]);
}

// Each channel clamps to 0..1 on the way in, and again after mixing.
void test_layout_clamps_after_mixing(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
      layoutMessage(kLayoutAddr, "rgbwa", {0.8f, -2.0f, 3.0f, 0.5f, 0.6f}), c));
  assertColour(1.0f, wisp::kOscAmberGreen * 0.6f, 1.0f, 0.5f, c[0]);
}

// UV, spelled `uv` or `u`, drops; a UV-only palette is blank, so lamps hold.
void test_layout_uv_drops_and_uv_only_is_blank(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  for (const char* layout : {"rgbuv", "rgbu"}) {
    TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
        layoutMessage(kLayoutAddr, layout, {0.1f, 0.2f, 0.3f, 1.0f}), c));
    assertColour(0.1f, 0.2f, 0.3f, 0, c[0]);
  }
  TEST_ASSERT_EQUAL_UINT(3, readLayoutOk(
      layoutMessage(kLayoutAddr, "uv", {1.0f, 0.5f, 0.25f}), c));
  TEST_ASSERT_TRUE(wisp::isBlankPalette(c, 3));
}

// Any other letter consumes its float and drives nothing.
void test_layout_unknown_letter_consumes_its_float(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(2, readLayoutOk(
      layoutMessage(kLayoutAddr, "rxgb", {0.1f, 0.9f, 0.2f, 0.3f,
                                          0.4f, 0.9f, 0.5f, 0.6f}), c));
  assertColour(0.1f, 0.2f, 0.3f, 0, c[0]);
  assertColour(0.4f, 0.5f, 0.6f, 0, c[1]);
}

// Integer arguments read as numbers in a layout too.
void test_layout_takes_int_arguments(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
      layoutMessage(kLayoutAddr, "rgb", {1, 0.5f, 0}, "ifi"), c));
  assertColour(1, 0.5f, 0, 0, c[0]);
}

// 16 channels read; 17 refuse.
void test_layout_channel_cap_is_16(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  std::vector<float> sixteen(16, 0.0f);
  sixteen[2] = 0.7f;
  TEST_ASSERT_EQUAL_UINT(1, readLayoutOk(
      layoutMessage(kLayoutAddr, "xxbxxxxxxxxxxxxx", sixteen), c));
  assertColour(0, 0, 0.7f, 0, c[0]);
  assertRefused(layoutMessage(kLayoutAddr, "xxbxxxxxxxxxxxxxx",
                              std::vector<float>(17, 0.5f)));
}

void test_layout_unreadable_refused(void) {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  // 7 floats under a 3-wide layout.
  assertRefused(layoutMessage(kLayoutAddr, "rgb", std::vector<float>(7, 0.5f)));
  assertRefused(layoutMessage(kLayoutAddr, "rgbw", {0.5f, nan, 0, 0}));
  assertRefused(layoutMessage(kLayoutAddr, "rgb", {0.5f, 0, inf}));
  assertRefused(layoutMessage(kLayoutAddr, "", {0.5f, 0.5f, 0.5f}));
  // A layout with no floats, a second string, an unknown tag.
  assertRefused(layoutMessage(kLayoutAddr, "rgb", {}));
  assertRefused(layoutMessage(kLayoutAddr, "rgb", {0.5f, 0.5f}, "ffs"));
  assertRefused(layoutMessage(kLayoutAddr, "rgb", {0.5f, 0.5f}, "fsf"));
  assertRefused(layoutMessage(kLayoutAddr, "rgb", {0.5f, 0.5f}, "fTf"));
  // More than 50 colours.
  assertRefused(layoutMessage(kLayoutAddr, "w",
                              std::vector<float>(wisp::kOscPaletteMaxColors + 1, 1.0f)));
  // Arguments that run past the tags.
  auto trailing = layoutMessage(kLayoutAddr, "rgb", {0.5f, 0.5f, 0.5f});
  for (int i = 0; i < 4; ++i) trailing.push_back(0);
  assertRefused(trailing);
}

// The 50-colour cap reads a full layout palette whole.
void test_layout_cap_is_50_colours(void) {
  wisp::PaletteColor c[wisp::kOscPaletteMaxColors];
  TEST_ASSERT_EQUAL_UINT(wisp::kOscPaletteMaxColors, readLayoutOk(
      layoutMessage(kLayoutAddr, "w",
                    std::vector<float>(wisp::kOscPaletteMaxColors, 1.0f)), c));
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
  RUN_TEST(test_osc_cap_is_50_colours);
  RUN_TEST(test_non_finite_float_refuses_whole_message);
  RUN_TEST(test_hex_ints_read_one_colour_each);
  RUN_TEST(test_hex_ignores_the_top_byte);
  RUN_TEST(test_rgb_floats_read_in_threes);
  RUN_TEST(test_colours_match_the_address_exactly);
  RUN_TEST(test_unreadable_colours_refused);
  RUN_TEST(test_hex_cap_is_50_colours);
  RUN_TEST(test_bundle_takes_first_readable_colours);
  RUN_TEST(test_layout_straylight_rgbau_reads_16_colours);
  RUN_TEST(test_layout_white_drives_white);
  RUN_TEST(test_layout_amber_mixes_into_red_and_green);
  RUN_TEST(test_layout_clamps_after_mixing);
  RUN_TEST(test_layout_uv_drops_and_uv_only_is_blank);
  RUN_TEST(test_layout_unknown_letter_consumes_its_float);
  RUN_TEST(test_layout_takes_int_arguments);
  RUN_TEST(test_layout_channel_cap_is_16);
  RUN_TEST(test_layout_unreadable_refused);
  RUN_TEST(test_layout_cap_is_50_colours);
  RUN_TEST(test_slip_frame_with_escapes);
  RUN_TEST(test_slip_back_to_back_and_oversize);
  RUN_TEST(test_slip_carries_straylight_bundle);
  RUN_TEST(test_slip_idle_reset_returns_to_text);
  RUN_TEST(test_slip_partial_frame_resets_a_wrap_later);
  return UNITY_END();
}

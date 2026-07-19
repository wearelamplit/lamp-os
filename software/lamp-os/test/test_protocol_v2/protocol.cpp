// Native-host round-trip tests for protocol additions:
// MSG_WISP_HELLO, MSG_OVERRIDE_COLORS, MSG_RESTORE_COLORS,
// MSG_OVERRIDE_BRIGHTNESS, MSG_RESTORE_BRIGHTNESS.
//
// The point is to pin the wire format so a refactor of the header
// can't silently shift byte offsets or drop a field. We include the
// production header directly — it's self-contained and provides a
// no-op portMUX shim when neither ARDUINO nor ESP_PLATFORM is defined.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "components/network/protocol/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

// v0x05 lock-in pin: introduces TLV trailers on HELLO + WISP_HELLO so
// future fields land as new TLV types (parsers skip unknowns by length)
// rather than bumping PROTOCOL_VERSION again. This is designed to be
// the LAST additive bump: anything that fits into a TLV doesn't touch
// this constant.
//
// If a future change wants to bump 0x05, it has to update THIS line —
// which forces a thoughtful answer to "is this actually a behavioral
// contract change (HELLO interval, MSG_FW_*
// layout, semantic redefinition) and not just a new field that could
// have been a TLV?" since inspect() rejects mismatched-version frames
// and a bump splits the mesh.
// Pin both ends of the supported version range. Bumping EMIT or
// RX_MAX is a real deploy-affecting change — see the upgrade-workflow
// doc block above PROTOCOL_VERSION_EMIT in lamp_protocol.hpp.
static_assert(lp::PROTOCOL_VERSION_EMIT == 0x05,
              "EMIT lock-in. Advance only after the fleet's RX_MAX has "
              "already moved up — the transitional release must accept "
              "the new version before any release starts emitting it.");
static_assert(lp::PROTOCOL_VERSION_RX_MAX == 0x05,
              "RX_MAX lock-in. Range [RX_MIN, RX_MAX] = {0x04, 0x05} today; "
              "v0x04 legacy support remains via per-peer wireVersion on "
              "OTA OFFER/CHUNK/DONE/ACCEPT/REQ/RESULT (see lamp_protocol).");
static_assert(lp::PROTOCOL_VERSION_RX_MIN == 0x04,
              "RX_MIN lock-in. Dropping this floor evicts v0x04 lamps from "
              "the mesh — only do so once the whole fleet has rolled past it.");

static const uint8_t kSrcMac[6]    = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kTargetMac[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

// --- MSG_WISP_HELLO ---

void test_wisp_hello_roundtrip() {
  uint8_t buf[lp::MAX_PACKET_SIZE];
  const char palette[] = "palette7";        // exactly 8 bytes, no terminator on wire
  const char channel[] = "standard-stable"; // 15 bytes — buildWispHello zero-pads
  const size_t n = lp::buildWispHello(
      buf, sizeof(buf), /*seq=*/0x1234, kSrcMac,
      /*wispVersion=*/0x01020304,
      /*flags=*/lp::WISP_HELLO_FLAG_PAINT_MODE | lp::WISP_HELLO_FLAG_AURORA_CONNECTED,
      palette, 8,
      channel, 15,
      /*carriedFwVersion=*/0xCAFEF00D);
  // v0x05 appends a 1-byte tlv_count=0 after the fixed prefix; total = 46.
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_HELLO_FIXED_SIZE + 1, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_WISP_HELLO, lp::inspect(buf, n));

  lp::ParsedWispHello out;
  TEST_ASSERT_TRUE(lp::parseWispHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x1234, out.seq);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrcMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, out.wispVersion);
  TEST_ASSERT_EQUAL_UINT8(lp::WISP_HELLO_FLAG_PAINT_MODE | lp::WISP_HELLO_FLAG_AURORA_CONNECTED,
                          out.flags);
  TEST_ASSERT_EQUAL_STRING("palette7", out.paletteIdPrefix);
  TEST_ASSERT_EQUAL_STRING("standard-stable", out.carriedFwChannel);
  TEST_ASSERT_EQUAL_UINT32(0xCAFEF00Du, out.carriedFwVersion);
}

void test_wisp_hello_too_short_rejected() {
  // Need room for the v0x05 tlv_count byte (FIXED_SIZE + 1).
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE + 1];
  std::memset(buf, 0, sizeof(buf));
  // Valid frame builds to FIXED_SIZE + 1 (the trailing tlv_count=0).
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_HELLO_FIXED_SIZE + 1,
      lp::buildWispHello(buf, sizeof(buf), 1, kSrcMac, 0, 0, "", 0, "", 0, 0));
  lp::ParsedWispHello out;
  // Parser still rejects anything shorter than the fixed prefix.
  TEST_ASSERT_FALSE(lp::parseWispHello(buf, lp::WISP_HELLO_FIXED_SIZE - 1, out));
}

// --- MSG_OVERRIDE_COLORS ---

void test_override_colors_roundtrip_min_and_max() {
  uint8_t buf[lp::OVERRIDE_COLORS_MAX_SIZE];

  // N=1 (minimum)
  const uint8_t one[4] = {0xAA, 0xBB, 0xCC, 0xDD};
  size_t n = lp::buildOverrideColors(buf, sizeof(buf), 7, kSrcMac, kTargetMac,
                                     lp::OverrideSurface::Base,
                                     lp::OverrideSource::Wisp,
                                     /*fadeMs=*/500,
                                     one, 1);
  TEST_ASSERT_EQUAL_UINT32(lp::OVERRIDE_COLORS_FIXED_SIZE + 4, n);
  lp::ParsedOverrideColors out;
  TEST_ASSERT_TRUE(lp::parseOverrideColors(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(7, out.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::OverrideSurface::Base),
                          static_cast<uint8_t>(out.surface));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::OverrideSource::Wisp),
                          static_cast<uint8_t>(out.sourceKind));
  TEST_ASSERT_EQUAL_UINT16(500, out.fadeDurationMs);
  TEST_ASSERT_EQUAL_UINT8(1, out.numColors);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(one, out.colors[0], 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrcMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTargetMac, out.targetMac, 6);

  // N=8 (maximum)
  uint8_t eight[8 * 4];
  for (size_t i = 0; i < sizeof(eight); ++i) eight[i] = static_cast<uint8_t>(i ^ 0x5A);
  n = lp::buildOverrideColors(buf, sizeof(buf), 8, kSrcMac, kTargetMac,
                              lp::OverrideSurface::BaseAndShade,
                              lp::OverrideSource::Any,
                              /*fadeMs=*/0xFFFF,
                              eight, 8);
  TEST_ASSERT_EQUAL_UINT32(lp::OVERRIDE_COLORS_FIXED_SIZE + 32, n);
  TEST_ASSERT_TRUE(lp::parseOverrideColors(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(8, out.numColors);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::OverrideSurface::BaseAndShade),
                          static_cast<uint8_t>(out.surface));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, out.fadeDurationMs);
  for (uint8_t i = 0; i < 8; ++i) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&eight[i * 4], out.colors[i], 4);
  }
}

void test_override_colors_zero_numcolors_rejected_by_builder() {
  uint8_t buf[lp::OVERRIDE_COLORS_MAX_SIZE];
  const uint8_t dummy[4] = {0};
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildOverrideColors(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                              lp::OverrideSurface::Base,
                              lp::OverrideSource::Wisp, 0, dummy, 0));
}

void test_override_colors_over_max_rejected_by_builder() {
  uint8_t buf[lp::OVERRIDE_COLORS_MAX_SIZE + 16];
  uint8_t many[9 * 4] = {0};
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildOverrideColors(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                              lp::OverrideSurface::Base,
                              lp::OverrideSource::Wisp, 0, many, 9));
}

void test_override_colors_unknown_surface_byte_rejected_by_parser() {
  // Hand-craft a frame with an illegal surface byte (0x42).
  uint8_t buf[lp::OVERRIDE_COLORS_FIXED_SIZE + 4];
  const uint8_t one[4] = {1, 2, 3, 4};
  const size_t n = lp::buildOverrideColors(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                                           lp::OverrideSurface::Base,
                                           lp::OverrideSource::Wisp, 100, one, 1);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  buf[18] = 0x42;  // corrupt surface byte
  lp::ParsedOverrideColors out;
  TEST_ASSERT_FALSE(lp::parseOverrideColors(buf, n, out));
}

void test_override_colors_reserved_source_byte_rejected_by_parser() {
  uint8_t buf[lp::OVERRIDE_COLORS_FIXED_SIZE + 4];
  const uint8_t one[4] = {1, 2, 3, 4};
  const size_t n = lp::buildOverrideColors(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                                           lp::OverrideSurface::Base,
                                           lp::OverrideSource::Wisp, 100, one, 1);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  buf[19] = 0x05;  // reserved range 0x03..0x0F
  lp::ParsedOverrideColors out;
  TEST_ASSERT_FALSE(lp::parseOverrideColors(buf, n, out));
}

void test_override_colors_fade_uint32_roundtrip() {
  uint8_t buf[lp::OVERRIDE_COLORS_FIXED_SIZE + 8];
  const uint8_t two[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
  const size_t n = lp::buildOverrideColors(buf, sizeof(buf), 5, kSrcMac, kTargetMac,
                                           lp::OverrideSurface::BaseAndShade,
                                           lp::OverrideSource::Wisp,
                                           200000u,
                                           two, 2);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  lp::ParsedOverrideColors out;
  TEST_ASSERT_TRUE(lp::parseOverrideColors(buf, n, out));
  TEST_ASSERT_EQUAL_UINT32(200000u, out.fadeDurationMs);
  TEST_ASSERT_EQUAL_UINT8(2, out.numColors);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(two,      out.colors[0], 4);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(&two[4],  out.colors[1], 4);
}

void test_override_colors_length_mismatch_rejected_by_parser() {
  uint8_t buf[lp::OVERRIDE_COLORS_FIXED_SIZE + 8];
  const uint8_t two[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const size_t n = lp::buildOverrideColors(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                                           lp::OverrideSurface::Base,
                                           lp::OverrideSource::Wisp, 100, two, 2);
  // Truncate the frame so length != fixed + numColors*4
  lp::ParsedOverrideColors out;
  TEST_ASSERT_FALSE(lp::parseOverrideColors(buf, n - 1, out));
  // Extend the frame by one byte (still over) — also rejected.
  TEST_ASSERT_FALSE(lp::parseOverrideColors(buf, n + 1, out));
}

// --- MSG_RESTORE_COLORS ---

void test_restore_colors_roundtrip() {
  uint8_t buf[lp::RESTORE_FIXED_SIZE];
  const size_t n = lp::buildRestoreColors(buf, sizeof(buf), 0xBEEF, kSrcMac, kTargetMac,
                                          lp::OverrideSurface::Shade,
                                          lp::OverrideSource::Wisp,
                                          /*fadeMs=*/1000);
  TEST_ASSERT_EQUAL_UINT32(lp::RESTORE_FIXED_SIZE, n);
  lp::ParsedRestoreColors out;
  TEST_ASSERT_TRUE(lp::parseRestoreColors(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::OverrideSurface::Shade),
                          static_cast<uint8_t>(out.surface));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::OverrideSource::Wisp),
                          static_cast<uint8_t>(out.sourceKind));
  TEST_ASSERT_EQUAL_UINT16(1000, out.fadeDurationMs);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kSrcMac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kTargetMac, out.targetMac, 6);
}

// --- MSG_OVERRIDE_BRIGHTNESS ---

void test_override_brightness_roundtrip() {
  uint8_t buf[lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  const size_t n = lp::buildOverrideBrightness(buf, sizeof(buf), 42,
                                               kSrcMac, kTargetMac,
                                               lp::OverrideSurface::Base,
                                               lp::OverrideSource::Wisp,
                                               /*fadeMs=*/2500,
                                               /*brightness=*/75);
  TEST_ASSERT_EQUAL_UINT32(lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE, n);
  lp::ParsedOverrideBrightness out;
  TEST_ASSERT_TRUE(lp::parseOverrideBrightness(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(42, out.seq);
  TEST_ASSERT_EQUAL_UINT8(75, out.brightness);
  TEST_ASSERT_EQUAL_UINT16(2500, out.fadeDurationMs);
}

void test_override_brightness_zero_accepted_by_builder() {
  uint8_t buf[lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  const size_t n = lp::buildOverrideBrightness(buf, sizeof(buf), 1, kSrcMac,
                                               kTargetMac,
                                               lp::OverrideSurface::Base,
                                               lp::OverrideSource::Wisp, 0,
                                               /*brightness=*/0);
  TEST_ASSERT_EQUAL_UINT32(lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE, n);
  lp::ParsedOverrideBrightness out;
  TEST_ASSERT_TRUE(lp::parseOverrideBrightness(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0, out.brightness);
}

void test_override_brightness_over_100_rejected_by_builder() {
  uint8_t buf[lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildOverrideBrightness(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                                  lp::OverrideSurface::Base,
                                  lp::OverrideSource::Wisp, 0,
                                  /*brightness=*/101));
}

void test_override_brightness_out_of_range_rejected_by_parser() {
  uint8_t buf[lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  const size_t n = lp::buildOverrideBrightness(buf, sizeof(buf), 1,
                                               kSrcMac, kTargetMac,
                                               lp::OverrideSurface::Base,
                                               lp::OverrideSource::Wisp, 0,
                                               /*brightness=*/50);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  buf[22] = 200;  // corrupt brightness past 100
  lp::ParsedOverrideBrightness out;
  TEST_ASSERT_FALSE(lp::parseOverrideBrightness(buf, n, out));
}

// --- MSG_RESTORE_BRIGHTNESS ---

void test_restore_brightness_roundtrip() {
  uint8_t buf[lp::RESTORE_FIXED_SIZE];
  const size_t n = lp::buildRestoreBrightness(buf, sizeof(buf), 9, kSrcMac, kTargetMac,
                                              lp::OverrideSurface::Shade,
                                              lp::OverrideSource::None,
                                              /*fadeMs=*/300);
  TEST_ASSERT_EQUAL_UINT32(lp::RESTORE_FIXED_SIZE, n);
  lp::ParsedRestoreBrightness out;
  TEST_ASSERT_TRUE(lp::parseRestoreBrightness(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(9, out.seq);
  TEST_ASSERT_EQUAL_UINT16(300, out.fadeDurationMs);
}

// MSG_WISP_PALETTE — round-trip a representative palette + boundary
// cases. Wisp truncates oversized palettes on emit (logged), so the
// builder REJECTS over-the-cap input rather than silently truncating;
// the parser mirrors the same guard.

static void test_wisp_palette_roundtrip() {
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0xE4, 0xB3, 0x23, 0xB4, 0x95, 0x20};
  // 7 colors, mixed values, including 0 and 255 to catch byte-shift bugs.
  const uint8_t rgb[] = {
      0,   0,   0,
      255, 255, 255,
      199,   0,  16,
      49, 155, 255,
      0,   97, 255,
      1,  205, 103,
      36,  99,  39,
  };
  const size_t count = sizeof(rgb) / 3;
  const uint8_t w[] = {0, 255, 128, 0, 1, 254, 77};
  static_assert(sizeof(w) == sizeof(rgb) / 3, "w plane mismatched to rgb");

  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 0x1234,
                                        mac, rgb, w, count);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_FIXED_PREFIX + count * 4, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(0x1234, out.seq);
  TEST_ASSERT_EQUAL_MEMORY(mac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL(count, out.count);
  TEST_ASSERT_EQUAL_MEMORY(rgb, out.rgb, count * 3);
  TEST_ASSERT_NOT_NULL(out.w);
  TEST_ASSERT_EQUAL_MEMORY(w, out.w, count);
}

static void test_wisp_palette_legacy_frame_parses_without_w_plane() {
  // Frames from senders that predate the W plane are count*3 only; the
  // parser must accept them and surface w == nullptr.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0xE4, 0xB3, 0x23, 0xB4, 0x95, 0x20};
  const uint8_t rgb[] = {10, 20, 30, 40, 50, 60};
  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 5, mac,
                                        rgb, /*w*/ nullptr, 2);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_FIXED_PREFIX + 2 * 3, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(2, out.count);
  TEST_ASSERT_EQUAL_MEMORY(rgb, out.rgb, sizeof(rgb));
  TEST_ASSERT_NULL(out.w);
}

static void test_wisp_palette_empty_roundtrip() {
  // Zero-color palette: header + count(=0), no body. Should round-trip
  // cleanly so a Manual-mode wisp with no palette still emits.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 7, mac,
                                        /*rgb*/ nullptr, /*w*/ nullptr,
                                        /*count*/ 0);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_FIXED_PREFIX, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_NULL(out.rgb);
  TEST_ASSERT_NULL(out.w);
}

static void test_wisp_palette_at_cap_roundtrip() {
  // Worst-case (50 colors): pin both the builder size accounting and the
  // parser length acceptance at the cap so a future cap bump is loud.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t rgb[lp::kMaxWispPaletteColors * 3];
  for (size_t i = 0; i < sizeof(rgb); ++i) rgb[i] = static_cast<uint8_t>(i);
  uint8_t w[lp::kMaxWispPaletteColors];
  for (size_t i = 0; i < sizeof(w); ++i) w[i] = static_cast<uint8_t>(255 - i);

  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 0xFFFF, mac,
                                        rgb, w, lp::kMaxWispPaletteColors);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_MAX_SIZE, n);
  TEST_ASSERT_TRUE(n <= 250);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(lp::kMaxWispPaletteColors, out.count);
  TEST_ASSERT_EQUAL_MEMORY(rgb, out.rgb, sizeof(rgb));
  TEST_ASSERT_NOT_NULL(out.w);
  TEST_ASSERT_EQUAL_MEMORY(w, out.w, sizeof(w));
}

static void test_wisp_palette_over_cap_rejected_by_builder() {
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE + 64];  // extra room so the failure
                                                // is the count check, not
                                                // the buffer size.
  const uint8_t mac[6] = {0};
  uint8_t rgb[(lp::kMaxWispPaletteColors + 1) * 3] = {0};
  TEST_ASSERT_EQUAL(0,
                    lp::buildWispPalette(buf, sizeof(buf), 0, mac, rgb,
                                         /*w*/ nullptr,
                                         lp::kMaxWispPaletteColors + 1));
}

static void test_wisp_palette_short_frame_rejected_by_parser() {
  // Frame claims 4 colors but the buffer is truncated mid-color. Parser
  // must reject — silently accepting would expose garbage data into the
  // BLE-served JSON.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t rgb[12] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
  const size_t n =
      lp::buildWispPalette(buf, sizeof(buf), 0, mac, rgb, /*w*/ nullptr, 4);
  TEST_ASSERT_TRUE(n > 0);
  // Trim one byte off the end → tail color is incomplete.
  lp::ParsedWispPalette out;
  TEST_ASSERT_FALSE(lp::parseWispPalette(buf, n - 1, out));
}

// Cross-version safety: inspect() accepts any version in [EMIT, RX_MAX]
// and rejects anything outside that range BEFORE any field read.
// Without this, a wider parser running against a narrower frame
// underflows.
void test_inspect_rejects_version_above_rx_max() {
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = lp::MAGIC_0;
  buf[1] = lp::MAGIC_1;
  buf[2] = lp::PROTOCOL_VERSION_RX_MAX + 1;  // newer than we can parse
  buf[3] = lp::MSG_WISP_HELLO;
  TEST_ASSERT_EQUAL_UINT8(0, lp::inspect(buf, sizeof(buf)));
}

void test_inspect_rejects_version_below_rx_min() {
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = lp::MAGIC_0;
  buf[1] = lp::MAGIC_1;
  buf[2] = lp::PROTOCOL_VERSION_RX_MIN - 1;  // older than we still parse
  buf[3] = lp::MSG_WISP_HELLO;
  TEST_ASSERT_EQUAL_UINT8(0, lp::inspect(buf, sizeof(buf)));
}

// Both ends of the accepted range work for inspect.
void test_inspect_accepts_rx_min_version() {
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = lp::MAGIC_0;
  buf[1] = lp::MAGIC_1;
  buf[2] = lp::PROTOCOL_VERSION_RX_MIN;
  buf[3] = lp::MSG_WISP_HELLO;
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_WISP_HELLO, lp::inspect(buf, sizeof(buf)));
}

void test_inspect_accepts_rx_max_version() {
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = lp::MAGIC_0;
  buf[1] = lp::MAGIC_1;
  buf[2] = lp::PROTOCOL_VERSION_RX_MAX;
  buf[3] = lp::MSG_WISP_HELLO;
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_WISP_HELLO, lp::inspect(buf, sizeof(buf)));
}

// --- MSG_HELLO TLV trailer (v0x05+) ---
//
// The TLV trailer is the forward-compat hinge: future fields land as
// new TLV types and parsers skip unknowns by length. These tests pin
// the contract so a future regression that re-introduces strict-size
// behavior fails loudly.

static const uint8_t kHelloShade[4] = {0x10, 0x20, 0x30, 0x40};
static const uint8_t kHelloBase[4]  = {0x50, 0x60, 0x70, 0x80};

void test_hello_idle_state_emits_compact_tlv_count_zero() {
  // The idle case (kOtaStateIdle) omits the OTA_STATE TLV entirely
  // and emits tlv_count=0. Anything else would bloat every HELLO with
  // 3 redundant bytes since most lamps are Idle most of the time.
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 0x1234, kSrcMac,
                                  kHelloShade, kHelloBase, 0xCAFE,
                                  "flora", 5, lp::kOtaStateIdle);
  // 24 fixed + 1 nameLen + 5 name + 1 tlv_count = 31 bytes total.
  TEST_ASSERT_EQUAL_UINT32(lp::HELLO_FIXED_SIZE + 1 + 5 + 1, n);
  // tlv_count byte sits right after the name.
  TEST_ASSERT_EQUAL_UINT8(0, buf[lp::HELLO_FIXED_SIZE + 1 + 5]);
  // Parse round-trip yields the same idle state.
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateIdle, out.otaState);
  TEST_ASSERT_EQUAL_STRING("flora", out.name);
}

void test_hello_sending_state_round_trip() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 7, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "jacko", 5, lp::kOtaStateSending);
  // Non-idle adds 3 bytes (type + len + value).
  TEST_ASSERT_EQUAL_UINT32(lp::HELLO_FIXED_SIZE + 1 + 5 + 1 + 3, n);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateSending, out.otaState);
}

void test_hello_receiving_state_round_trip() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 9, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "anonymou", 8, lp::kOtaStateReceiving);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateReceiving, out.otaState);
}

void test_hello_fw_channel_round_trip() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 11, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "jacko", 5, lp::kOtaStateIdle,
                                  "standard-beta");
  // Idle (no OTA_STATE TLV) + FW_CHANNEL TLV: tlv_count(1) + type(1) + len(1)
  // + value(16).
  TEST_ASSERT_EQUAL_UINT32(
      lp::HELLO_FIXED_SIZE + 1 + 5 + 1 + (2 + lp::HELLO_FW_CHANNEL_LEN), n);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_STRING("standard-beta", out.fwChannel);
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateIdle, out.otaState);
}

void test_hello_fw_channel_and_ota_state_both_emit() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 12, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "snafu1", 6, lp::kOtaStateSending,
                                  "snafu-stable");
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_STRING("snafu-stable", out.fwChannel);
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateSending, out.otaState);
}

void test_hello_absent_fw_channel_is_empty() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  // No fwChannel arg → no TLV emitted → parsed channel stays empty (the
  // "older peer" case the distributor treats as unknown).
  const size_t n = lp::buildHello(buf, sizeof(buf), 13, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "old", 3, lp::kOtaStateIdle);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_STRING("", out.fwChannel);
}

void test_hello_max_chunk_round_trip() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 14, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "jacko", 5, lp::kOtaStateIdle,
                                  /*fwChannel=*/nullptr, /*fsDigest=*/nullptr,
                                  /*maxChunk=*/768);
  // Idle (no OTA_STATE TLV) + FW_MAX_CHUNK TLV: tlv_count(1) + type(1) +
  // len(1) + value(2).
  TEST_ASSERT_EQUAL_UINT32(lp::HELLO_FIXED_SIZE + 1 + 5 + 1 + 4, n);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(768, out.maxChunk);
}

void test_hello_absent_max_chunk_is_zero() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  // No maxChunk arg → no TLV emitted → parses to 0, the "older peer /
  // never-receives-OTA peer" default the distributor treats as baseline.
  const size_t n = lp::buildHello(buf, sizeof(buf), 15, kSrcMac,
                                  kHelloShade, kHelloBase, 0xBEEF,
                                  "old", 3, lp::kOtaStateIdle);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0, out.maxChunk);
}

void test_hello_max_chunk_with_channel_and_fs_state_all_emit() {
  // Every known TLV type at once: the byte-budget worst case (name at max
  // length + OTA_STATE + FW_CHANNEL + FS_STATE + FW_MAX_CHUNK) must still
  // fit HELLO_MAX_SIZE (128).
  static const uint8_t kFsDigest[lp::HELLO_FS_DIGEST_LEN] = {
      1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(
      buf, sizeof(buf), 16, kSrcMac, kHelloShade, kHelloBase, 0xBEEF,
      "thirtytwocharlongnamepadded1234", 32, lp::kOtaStateSending,
      "standard-beta", kFsDigest, /*maxChunk=*/768);
  TEST_ASSERT_TRUE(n <= lp::HELLO_MAX_SIZE);
  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateSending, out.otaState);
  TEST_ASSERT_EQUAL_STRING("standard-beta", out.fwChannel);
  TEST_ASSERT_TRUE(out.hasFsDigest);
  TEST_ASSERT_EQUAL_UINT16(768, out.maxChunk);
}

// Forward-compat: an unknown TLV type must be skipped (by length),
// the known OTA_STATE TLV that follows must still parse cleanly, and
// no fields beyond what we know about should be affected.
void test_hello_unknown_tlv_is_skipped() {
  // Hand-assemble a HELLO with two trailing TLVs:
  //   1) unknown type 0xEE, value len 4 (gibberish)
  //   2) OTA_STATE = kOtaStateReceiving
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 1, kSrcMac,
                                  kHelloShade, kHelloBase, 0,
                                  "x", 1, lp::kOtaStateIdle);
  // Overwrite the trailer (tlv_count=0 at offset 26): rewrite it as
  // tlv_count=2 then two TLVs.
  const size_t trailerOff = lp::HELLO_FIXED_SIZE + 1 + 1;  // 24 + 1 nameLen + 1 name
  TEST_ASSERT_EQUAL_UINT32(trailerOff + 1, n);  // sanity: idle build ends at trailerOff+1
  uint8_t* p = buf + trailerOff;
  *p++ = 2;            // tlv_count
  *p++ = 0xEE;         // unknown type
  *p++ = 4;            // value len
  *p++ = 0xAA; *p++ = 0xBB; *p++ = 0xCC; *p++ = 0xDD;
  *p++ = lp::HELLO_TLV_OTA_STATE;
  *p++ = 1;
  *p++ = lp::kOtaStateReceiving;
  const size_t totalLen = static_cast<size_t>(p - buf);

  lp::ParsedHello out;
  TEST_ASSERT_TRUE(lp::parseHello(buf, totalLen, out));
  // Unknown TLV skipped; OTA_STATE found and applied.
  TEST_ASSERT_EQUAL_UINT8(lp::kOtaStateReceiving, out.otaState);
}

// A malformed trailer (TLV claims more bytes than the frame holds)
// must be rejected, not crash.
void test_hello_tlv_with_oversized_length_is_rejected() {
  uint8_t buf[lp::HELLO_MAX_SIZE];
  const size_t n = lp::buildHello(buf, sizeof(buf), 1, kSrcMac,
                                  kHelloShade, kHelloBase, 0,
                                  "x", 1, lp::kOtaStateIdle);
  const size_t trailerOff = lp::HELLO_FIXED_SIZE + 1 + 1;
  TEST_ASSERT_EQUAL_UINT32(trailerOff + 1, n);
  uint8_t* p = buf + trailerOff;
  *p++ = 1;            // tlv_count=1
  *p++ = 0xEE;
  *p++ = 200;          // claimed len > remaining frame bytes
  const size_t totalLen = static_cast<size_t>(p - buf);
  lp::ParsedHello out;
  TEST_ASSERT_FALSE(lp::parseHello(buf, totalLen, out));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_wisp_hello_roundtrip);
  RUN_TEST(test_wisp_hello_too_short_rejected);
  RUN_TEST(test_inspect_rejects_version_above_rx_max);
  RUN_TEST(test_inspect_rejects_version_below_rx_min);
  RUN_TEST(test_inspect_accepts_rx_min_version);
  RUN_TEST(test_inspect_accepts_rx_max_version);
  RUN_TEST(test_hello_idle_state_emits_compact_tlv_count_zero);
  RUN_TEST(test_hello_sending_state_round_trip);
  RUN_TEST(test_hello_receiving_state_round_trip);
  RUN_TEST(test_hello_fw_channel_round_trip);
  RUN_TEST(test_hello_fw_channel_and_ota_state_both_emit);
  RUN_TEST(test_hello_absent_fw_channel_is_empty);
  RUN_TEST(test_hello_max_chunk_round_trip);
  RUN_TEST(test_hello_absent_max_chunk_is_zero);
  RUN_TEST(test_hello_max_chunk_with_channel_and_fs_state_all_emit);
  RUN_TEST(test_hello_unknown_tlv_is_skipped);
  RUN_TEST(test_hello_tlv_with_oversized_length_is_rejected);

  RUN_TEST(test_override_colors_roundtrip_min_and_max);
  RUN_TEST(test_override_colors_fade_uint32_roundtrip);
  RUN_TEST(test_override_colors_zero_numcolors_rejected_by_builder);
  RUN_TEST(test_override_colors_over_max_rejected_by_builder);
  RUN_TEST(test_override_colors_unknown_surface_byte_rejected_by_parser);
  RUN_TEST(test_override_colors_reserved_source_byte_rejected_by_parser);
  RUN_TEST(test_override_colors_length_mismatch_rejected_by_parser);

  RUN_TEST(test_restore_colors_roundtrip);

  RUN_TEST(test_override_brightness_roundtrip);
  RUN_TEST(test_override_brightness_zero_accepted_by_builder);
  RUN_TEST(test_override_brightness_over_100_rejected_by_builder);
  RUN_TEST(test_override_brightness_out_of_range_rejected_by_parser);

  RUN_TEST(test_restore_brightness_roundtrip);

  RUN_TEST(test_wisp_palette_roundtrip);
  RUN_TEST(test_wisp_palette_legacy_frame_parses_without_w_plane);
  RUN_TEST(test_wisp_palette_empty_roundtrip);
  RUN_TEST(test_wisp_palette_at_cap_roundtrip);
  RUN_TEST(test_wisp_palette_over_cap_rejected_by_builder);
  RUN_TEST(test_wisp_palette_short_frame_rejected_by_parser);

  return UNITY_END();
}

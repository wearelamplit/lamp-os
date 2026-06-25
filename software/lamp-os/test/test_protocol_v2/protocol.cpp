// Native-host round-trip tests for the Phase C protocol additions:
// MSG_WISP_HELLO, MSG_OVERRIDE_COLORS, MSG_RESTORE_COLORS,
// MSG_OVERRIDE_BRIGHTNESS, MSG_RESTORE_BRIGHTNESS, MSG_EVENT.
//
// The point is to pin the wire format so a refactor of the header
// can't silently shift byte offsets or drop a field. We include the
// production header directly — it's self-contained and provides a
// no-op portMUX shim when neither ARDUINO nor ESP_PLATFORM is defined.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "components/network/lamp_protocol.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lp = lamp_protocol;

// v0x04 lock-in pin: widens FW_CHANNEL_LEN 8→16 so the channel slot can
// carry `{lampType}-{channel}` (e.g. "standard-stable") and the existing
// silent-drop on channel-mismatch enforces per-variant OTA gating. MSG_FW_OFFER
// grows 48→56, MSG_WISP_HELLO grows 37→45. If a future change wants to bump
// 0x04, it has to update THIS line — which forces a thoughtful answer to
// "have we re-flashed all peers + the wisp?" since inspect() rejects
// mismatched-version frames.
static_assert(lp::PROTOCOL_VERSION == 0x04,
              "PROTOCOL_VERSION lock-in for v0x04 (typed-channel OTA gating). "
              "Bumping this constant requires re-flashing all lamps + wisp "
              "before redeploy — inspect() rejects mismatches.");

// v0x03 lock-in pin: DedupRing capacity grew 32 → 64 because at 20-50 lamps
// each gossiping (sourceMac, seq) the 32-slot ring wrapped fast enough that
// a late-arriving gossip copy could re-fire a receiver. Pinning here forces
// any future shrink to come with an explicit "we re-validated this against
// fleet size N" answer. The data-behavior tests for the new boundary live
// in test_dedup_ring (against an inline mirror that's also pinned to 64).
// why: pins the DedupRing capacity lock-in per validated plan §"Layer 2".
static_assert(lp::DedupRing::CAPACITY == 64,
              "DedupRing::CAPACITY lock-in for v0x03 (32→64 for 20-50 lamp "
              "fleet headroom). Shrinking risks re-firing late gossip copies.");

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
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_HELLO_FIXED_SIZE, n);
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
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  // Valid frame
  TEST_ASSERT_EQUAL_UINT32(lp::WISP_HELLO_FIXED_SIZE,
      lp::buildWispHello(buf, sizeof(buf), 1, kSrcMac, 0, 0, "", 0, "", 0, 0));
  lp::ParsedWispHello out;
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

void test_override_brightness_too_low_rejected_by_builder() {
  uint8_t buf[lp::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildOverrideBrightness(buf, sizeof(buf), 1, kSrcMac, kTargetMac,
                                  lp::OverrideSurface::Base,
                                  lp::OverrideSource::Wisp, 0,
                                  /*brightness=*/lp::kBrightnessOverrideMin - 1));
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

// --- MSG_EVENT ---

void test_event_roundtrip_no_stagger_no_payload() {
  uint8_t buf[lp::EVENT_MAX_SIZE];
  const size_t n = lp::buildEvent(buf, sizeof(buf), /*seq=*/0x55AA, kSrcMac,
                                  /*eventKind=*/static_cast<uint8_t>(lp::EventKind::ExpressionTriggered),
                                  /*staggerMacs=*/nullptr,
                                  /*staggerDelays=*/nullptr,
                                  /*numStaggerEntries=*/0,
                                  /*payload=*/nullptr, /*payloadLen=*/0);
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_FIXED_SIZE, n);
  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT16(0x55AA, out.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::EventKind::ExpressionTriggered),
                          out.eventKindRaw);
  TEST_ASSERT_EQUAL_UINT8(0, out.numStaggerEntries);
  TEST_ASSERT_EQUAL_UINT16(0, out.payloadLen);
}

void test_event_roundtrip_full_stagger_and_payload() {
  uint8_t buf[lp::EVENT_MAX_SIZE];

  // 12 stagger entries
  uint8_t staggerMacs[lp::kMaxStaggerEntries * 6];
  uint16_t staggerDelays[lp::kMaxStaggerEntries];
  for (size_t i = 0; i < lp::kMaxStaggerEntries; ++i) {
    for (size_t b = 0; b < 6; ++b) {
      staggerMacs[i * 6 + b] = static_cast<uint8_t>((i << 4) | b);
    }
    staggerDelays[i] = static_cast<uint16_t>(100 * i + 7);
  }

  // Payload: small JSON-ish blob
  const char payload[] = "{\"expression\":\"hello world\"}";
  const uint16_t payloadLen = static_cast<uint16_t>(sizeof(payload) - 1);

  const size_t n = lp::buildEvent(buf, sizeof(buf), 1, kSrcMac,
                                  /*eventKind=*/0xA5,  // user-defined
                                  staggerMacs, staggerDelays,
                                  lp::kMaxStaggerEntries,
                                  reinterpret_cast<const uint8_t*>(payload),
                                  payloadLen);
  TEST_ASSERT_EQUAL_UINT32(
      lp::EVENT_FIXED_SIZE + lp::kMaxStaggerEntries * lp::EVENT_STAGGER_ENTRY + payloadLen,
      n);

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(0xA5, out.eventKindRaw);
  TEST_ASSERT_EQUAL_UINT8(lp::kMaxStaggerEntries, out.numStaggerEntries);
  for (size_t i = 0; i < lp::kMaxStaggerEntries; ++i) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(&staggerMacs[i * 6], out.staggerEntries[i].mac, 6);
    TEST_ASSERT_EQUAL_UINT16(staggerDelays[i], out.staggerEntries[i].delayMs);
  }
  TEST_ASSERT_EQUAL_UINT16(payloadLen, out.payloadLen);
  TEST_ASSERT_NOT_NULL(out.payload);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out.payload, payloadLen);
}

void test_event_too_many_stagger_entries_rejected_by_builder() {
  uint8_t buf[lp::EVENT_MAX_SIZE];
  uint8_t macs[6] = {0};
  uint16_t delays[1] = {0};
  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildEvent(buf, sizeof(buf), 1, kSrcMac, 0x01,
                     macs, delays,
                     /*numStaggerEntries=*/lp::kMaxStaggerEntries + 1,
                     nullptr, 0));
}

void test_event_too_many_stagger_entries_rejected_by_parser() {
  // Hand-craft a valid header but with numStaggerEntries=13 (over cap).
  uint8_t buf[lp::EVENT_MAX_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = 'L';
  buf[1] = 'M';
  buf[2] = lp::PROTOCOL_VERSION;
  buf[3] = lp::MSG_EVENT;
  buf[4] = 0x01;
  buf[5] = 0x00;
  std::memcpy(&buf[6], kSrcMac, 6);
  buf[12] = 0x01;                                          // eventKind
  buf[13] = static_cast<uint8_t>(lp::kMaxStaggerEntries + 1);  // over cap
  lp::ParsedEvent out;
  TEST_ASSERT_FALSE(lp::parseEvent(buf, lp::EVENT_FIXED_SIZE +
                                        (lp::kMaxStaggerEntries + 1) * lp::EVENT_STAGGER_ENTRY,
                                    out));
}

// MSG_EVENT payload budget is dynamic in the actual stagger-entry count.
// The wire room reserved for stagger entries on a small mesh is a few bytes
// instead of the worst-case 96 (12 entries × 8); the payload budget grows
// to fill what's left in the 250 B frame. Without this, the cascade dropped
// any invocation > 138 B even on a 1-peer mesh — which broke glitchy in
// the field (151 B real payload).
void test_event_dynamic_payload_budget_zero_stagger() {
  // No stagger entries → budget is 250 - EVENT_FIXED_SIZE = 234 B.
  TEST_ASSERT_EQUAL_UINT32(250 - lp::EVENT_FIXED_SIZE,
                           lp::maxEventPayloadFor(0));
}

void test_event_dynamic_payload_budget_one_stagger() {
  // 1 stagger entry (= 8 B) → 226 B payload budget.
  TEST_ASSERT_EQUAL_UINT32(250 - lp::EVENT_FIXED_SIZE - lp::EVENT_STAGGER_ENTRY,
                           lp::maxEventPayloadFor(1));
}

void test_event_dynamic_payload_budget_full_stagger_matches_legacy_constant() {
  // 12 stagger entries (the worst case) reproduces the historical
  // EVENT_MAX_PAYLOAD = 138. Keeps a hard upper bound the rest of the
  // codebase can still rely on for buffer sizing.
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_MAX_PAYLOAD,
                           lp::maxEventPayloadFor(lp::kMaxStaggerEntries));
}

void test_event_dynamic_payload_budget_clamps_over_kmax() {
  // numStagger over the cap clamps to kMaxStaggerEntries' budget. Defensive:
  // callers should never exceed the cap, but a stale arg shouldn't grant a
  // larger-than-frame payload window.
  TEST_ASSERT_EQUAL_UINT32(lp::EVENT_MAX_PAYLOAD,
                           lp::maxEventPayloadFor(lp::kMaxStaggerEntries + 5));
}

void test_event_builder_accepts_226_byte_payload_with_one_stagger() {
  // The bug glitchy hit in the field: a 151 B JSON payload on a 1-peer mesh
  // got dropped because the static EVENT_MAX_PAYLOAD (138) was tighter than
  // the actual frame budget. After the dynamic-budget change, a 226 B payload
  // with 1 stagger entry must succeed and roundtrip cleanly (just at the cap).
  uint8_t buf[lp::EVENT_MAX_SIZE];
  uint8_t macs[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint16_t delays[1] = {250};
  std::vector<uint8_t> payload(226, 0x5A);

  const size_t n = lp::buildEvent(buf, sizeof(buf), 1, kSrcMac,
                                  /*eventKind=*/0xA5,
                                  macs, delays, /*numStaggerEntries=*/1,
                                  payload.data(),
                                  static_cast<uint16_t>(payload.size()));
  TEST_ASSERT_EQUAL_UINT32(
      lp::EVENT_FIXED_SIZE + lp::EVENT_STAGGER_ENTRY + payload.size(), n);
  TEST_ASSERT_EQUAL_UINT32(250, n);  // exactly the ESP-NOW frame ceiling

  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));
  TEST_ASSERT_EQUAL_UINT8(1, out.numStaggerEntries);
  TEST_ASSERT_EQUAL_UINT16(payload.size(), out.payloadLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload.data(), out.payload, payload.size());
}

void test_event_builder_rejects_payload_over_dynamic_budget() {
  // 1 stagger entry leaves room for exactly 226 B of payload. 227 B must
  // be rejected by the builder (still cleanly, not by overflowing the buf).
  uint8_t buf[lp::EVENT_MAX_SIZE];
  uint8_t macs[6] = {0};
  uint16_t delays[1] = {0};
  std::vector<uint8_t> tooBig(227, 0x42);

  TEST_ASSERT_EQUAL_UINT32(0,
      lp::buildEvent(buf, sizeof(buf), 1, kSrcMac, 0x01,
                     macs, delays, /*numStaggerEntries=*/1,
                     tooBig.data(),
                     static_cast<uint16_t>(tooBig.size())));
}

// v0x03 lock-in: high bit on numStaggerEntries (data[13]) is reserved for
// a future "scope flag" on the stagger semantics (e.g., room-scope vs.
// fleet-scope cascades). Today's receivers must REJECT any frame that
// sets it, so a future receiver can use the bit unambiguously: a v0x03
// peer dropping the frame is loud, diagnosable forward-compat behavior,
// not a silent reinterpretation. Mirrors the kReservedMsgTypeHighBit
// pattern on data[3].
// why: forward-compat reservation per validated plan §"Layer 3".
void test_event_parse_rejects_frame_with_stagger_count_high_bit_set() {
  uint8_t buf[lp::EVENT_MAX_SIZE];
  const size_t n = lp::buildEvent(buf, sizeof(buf), /*seq=*/1, kSrcMac,
                                  /*eventKind=*/static_cast<uint8_t>(lp::EventKind::ExpressionTriggered),
                                  /*staggerMacs=*/nullptr,
                                  /*staggerDelays=*/nullptr,
                                  /*numStaggerEntries=*/0,
                                  /*payload=*/nullptr, /*payloadLen=*/0);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);

  // Sanity: pristine frame parses cleanly.
  lp::ParsedEvent out;
  TEST_ASSERT_TRUE(lp::parseEvent(buf, n, out));

  // Flip the reserved high bit on data[13] (numStaggerEntries field).
  // Even though the low bits still encode 0 (a legitimate count), the
  // explicit reservation must reject the frame. Without the guard, the
  // pre-existing `numStaggerEntries > kMaxStaggerEntries` check would
  // ALSO catch 0x80 (= 128) as out-of-range — but that's the wrong
  // failure path. We want the reservation to be the EXPLICIT contract,
  // so a future code-shape that masks first (e.g., `data[13] & 0x7F`)
  // doesn't silently accept the frame.
  buf[13] |= lp::kStaggerCountReservedHighBit;
  TEST_ASSERT_FALSE(lp::parseEvent(buf, n, out));
}

// Mirror the kReservedMsgTypeHighBit constant pattern: any future revival
// of the bit needs an explicit code update. Pinning the value here means
// the test compiles only if the constant exists with the documented value.
// why: pins the reserved-bit constant per validated plan §"Layer 3".
static_assert(lp::kStaggerCountReservedHighBit == 0x80,
              "kStaggerCountReservedHighBit lock-in: reserves the high bit "
              "on numStaggerEntries (data[13]) for future protocol use.");

void test_inspect_no_longer_masks_high_bit() {
  // C.3 retired FLAG_LOCAL_ONLY. inspect() now returns data[3] verbatim,
  // so a frame with the high bit set on the msgType byte (a hypothetical
  // future use of kReservedMsgTypeHighBit) presents as an unknown type
  // rather than silently mapping back to MSG_CONTROL_OP. This is the
  // forward-compat guarantee: any reuse of the bit MUST come with an
  // explicit parser update.
  uint8_t buf[lp::CONTROL_MAX_SIZE];
  const size_t n = lp::buildControlOp(buf, sizeof(buf), 1, kTargetMac, kSrcMac,
                                      nullptr, 0);
  TEST_ASSERT_GREATER_THAN_UINT32(0, n);
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_CONTROL_OP, lp::inspect(buf, n));

  // Manually flip the high bit; inspect() must now return the high-bit
  // variant verbatim (i.e. not equal to MSG_CONTROL_OP).
  buf[3] = lp::MSG_CONTROL_OP | lp::kReservedMsgTypeHighBit;
  TEST_ASSERT_EQUAL_UINT8(lp::MSG_CONTROL_OP | lp::kReservedMsgTypeHighBit,
                          lp::inspect(buf, n));
  TEST_ASSERT_NOT_EQUAL(lp::MSG_CONTROL_OP, lp::inspect(buf, n));
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

  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 0x1234,
                                        mac, rgb, count);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_FIXED_PREFIX + count * 3, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(0x1234, out.seq);
  TEST_ASSERT_EQUAL_MEMORY(mac, out.sourceMac, 6);
  TEST_ASSERT_EQUAL(count, out.count);
  TEST_ASSERT_EQUAL_MEMORY(rgb, out.rgb, count * 3);
}

static void test_wisp_palette_empty_roundtrip() {
  // Zero-color palette: header + count(=0), no body. Should round-trip
  // cleanly so a Manual-mode wisp with no palette still emits.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 7, mac,
                                        /*rgb*/ nullptr, /*count*/ 0);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_FIXED_PREFIX, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(0, out.count);
  TEST_ASSERT_NULL(out.rgb);
}

static void test_wisp_palette_at_cap_roundtrip() {
  // Worst-case (50 colors): pin both the builder size accounting and the
  // parser length acceptance at the cap so a future cap bump is loud.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t rgb[lp::kMaxWispPaletteColors * 3];
  for (size_t i = 0; i < sizeof(rgb); ++i) rgb[i] = static_cast<uint8_t>(i);

  const size_t n = lp::buildWispPalette(buf, sizeof(buf), /*seq*/ 0xFFFF, mac,
                                        rgb, lp::kMaxWispPaletteColors);
  TEST_ASSERT_EQUAL(lp::WISP_PALETTE_MAX_SIZE, n);

  lp::ParsedWispPalette out;
  TEST_ASSERT_TRUE(lp::parseWispPalette(buf, n, out));
  TEST_ASSERT_EQUAL(lp::kMaxWispPaletteColors, out.count);
  TEST_ASSERT_EQUAL_MEMORY(rgb, out.rgb, sizeof(rgb));
}

static void test_wisp_palette_over_cap_rejected_by_builder() {
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE + 64];  // extra room so the failure
                                                // is the count check, not
                                                // the buffer size.
  const uint8_t mac[6] = {0};
  uint8_t rgb[(lp::kMaxWispPaletteColors + 1) * 3] = {0};
  TEST_ASSERT_EQUAL(0,
                    lp::buildWispPalette(buf, sizeof(buf), 0, mac, rgb,
                                         lp::kMaxWispPaletteColors + 1));
}

static void test_wisp_palette_short_frame_rejected_by_parser() {
  // Frame claims 4 colors but the buffer is truncated mid-color. Parser
  // must reject — silently accepting would expose garbage data into the
  // BLE-served JSON.
  uint8_t buf[lp::WISP_PALETTE_MAX_SIZE];
  const uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t rgb[12] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120};
  const size_t n = lp::buildWispPalette(buf, sizeof(buf), 0, mac, rgb, 4);
  TEST_ASSERT_TRUE(n > 0);
  // Trim one byte off the end → tail color is incomplete.
  lp::ParsedWispPalette out;
  TEST_ASSERT_FALSE(lp::parseWispPalette(buf, n - 1, out));
}

// Cross-version safety: v0x04 lamps must silently drop v0x03 frames at
// inspect() before any field read. Without this, a wider v0x04 parser
// running against a narrower v0x03 frame underflows.
void test_inspect_rejects_wrong_protocol_version() {
  uint8_t buf[lp::WISP_HELLO_FIXED_SIZE];
  std::memset(buf, 0, sizeof(buf));
  buf[0] = lp::MAGIC_0;
  buf[1] = lp::MAGIC_1;
  buf[2] = 0x03;  // pre-v0x04 frame
  buf[3] = lp::MSG_WISP_HELLO;
  // inspect() rejects on version mismatch BEFORE any field read.
  TEST_ASSERT_EQUAL_UINT8(0, lp::inspect(buf, sizeof(buf)));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_wisp_hello_roundtrip);
  RUN_TEST(test_wisp_hello_too_short_rejected);
  RUN_TEST(test_inspect_rejects_wrong_protocol_version);

  RUN_TEST(test_override_colors_roundtrip_min_and_max);
  RUN_TEST(test_override_colors_zero_numcolors_rejected_by_builder);
  RUN_TEST(test_override_colors_over_max_rejected_by_builder);
  RUN_TEST(test_override_colors_unknown_surface_byte_rejected_by_parser);
  RUN_TEST(test_override_colors_reserved_source_byte_rejected_by_parser);
  RUN_TEST(test_override_colors_length_mismatch_rejected_by_parser);

  RUN_TEST(test_restore_colors_roundtrip);

  RUN_TEST(test_override_brightness_roundtrip);
  RUN_TEST(test_override_brightness_too_low_rejected_by_builder);
  RUN_TEST(test_override_brightness_over_100_rejected_by_builder);
  RUN_TEST(test_override_brightness_out_of_range_rejected_by_parser);

  RUN_TEST(test_restore_brightness_roundtrip);

  RUN_TEST(test_event_roundtrip_no_stagger_no_payload);
  RUN_TEST(test_event_roundtrip_full_stagger_and_payload);
  RUN_TEST(test_event_too_many_stagger_entries_rejected_by_builder);
  RUN_TEST(test_event_too_many_stagger_entries_rejected_by_parser);

  RUN_TEST(test_event_dynamic_payload_budget_zero_stagger);
  RUN_TEST(test_event_dynamic_payload_budget_one_stagger);
  RUN_TEST(test_event_dynamic_payload_budget_full_stagger_matches_legacy_constant);
  RUN_TEST(test_event_dynamic_payload_budget_clamps_over_kmax);
  RUN_TEST(test_event_builder_accepts_226_byte_payload_with_one_stagger);
  RUN_TEST(test_event_builder_rejects_payload_over_dynamic_budget);

  RUN_TEST(test_event_parse_rejects_frame_with_stagger_count_high_bit_set);

  RUN_TEST(test_inspect_no_longer_masks_high_bit);

  RUN_TEST(test_wisp_palette_roundtrip);
  RUN_TEST(test_wisp_palette_empty_roundtrip);
  RUN_TEST(test_wisp_palette_at_cap_roundtrip);
  RUN_TEST(test_wisp_palette_over_cap_rejected_by_builder);
  RUN_TEST(test_wisp_palette_short_frame_rejected_by_parser);

  return UNITY_END();
}

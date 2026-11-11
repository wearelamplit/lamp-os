// Compositor::applyWispStateSlot, the drain's STATE-slot step: this lamp's
// RGBW entry lands as the wisp target with W intact, black included, and an
// absent entry reports unpainted, and the drift fade record drives a timed
// fade: repeats keep it, lost frames don't step it, partial frames leave it
// running, a full frame without it ends it. Links against a stub Compositor
// constructor.

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/network/mesh/pending_slots.cpp"
#include "core/compositor.hpp"
#include "util/fade.cpp"

namespace lp = lamp_protocol;
using lamp::Color;
using lamp::Compositor;
using lamp::PendingWispState;

namespace lamp {
Compositor::Compositor()
    : wispShadePresence_(1.0f, 0.0f), wispBasePresence_(1.0f, 0.0f) {}
}  // namespace lamp

void setUp(void) {}
void tearDown(void) {}

namespace {
const uint8_t kSrc[6]   = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};
const uint8_t kMine[6]  = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
const uint8_t kOther[6] = {0x20, 0x21, 0x22, 0x23, 0x24, 0x25};

PendingWispState gSlot;

// kOther at index 0, kMine at index 1, each with its own RGB and W.
void fillTwo(const uint8_t mineRgb[6], const uint8_t w[4], bool withW) {
  uint8_t entries[2 * lp::WISP_STATE_ENTRY_SIZE];
  std::memcpy(entries, kOther, 6);
  const uint8_t otherRgb[6] = {7, 8, 9, 10, 11, 12};
  std::memcpy(entries + 6, otherRgb, 6);
  std::memcpy(entries + 12, kMine, 6);
  std::memcpy(entries + 18, mineRgb, 6);
  uint8_t buf[lp::WISP_STATE_MAX_SIZE];
  const size_t n = lp::buildWispState(buf, sizeof(buf), 1, kSrc, 100, 60000, 0,
                                      entries, withW ? w : nullptr, 2);
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, ws));
  lamp::fillPendingWispState(gSlot, ws, {});
}

void assertColor(Color want, Color got) {
  TEST_ASSERT_EQUAL_UINT8(want.r, got.r);
  TEST_ASSERT_EQUAL_UINT8(want.g, got.g);
  TEST_ASSERT_EQUAL_UINT8(want.b, got.b);
  TEST_ASSERT_EQUAL_UINT8(want.w, got.w);
}
}  // namespace

void test_drain_applies_own_rgbw() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertColor(Color(40, 50, 60, 0xC8), c.wispStateBaseColor());
  assertColor(Color(70, 80, 90, 0x37), c.wispStateShadeColor());
}

// Full wisp presence replaces the own scene with the layers' eased target.
void assertLayers(const Compositor& c, Color wantBase, Color wantShade) {
  const Color own(200, 100, 50, 0);
  lamp::LayerStack base = c.wispLayer(true);
  lamp::LayerStack shade = c.wispLayer(false);
  for (int i = 0; i < 600; ++i) {
    base.tick(1.0f, 0);
    shade.tick(1.0f, 0);
  }
  assertColor(wantBase, base.composite(own));
  assertColor(wantShade, shade.composite(own));
}

void test_drain_feeds_render_layers_rgbw() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertLayers(c, Color(40, 50, 60, 0xC8), Color(70, 80, 90, 0x37));
}

void test_drain_w_only_repaint_reaches_layers() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w1[4] = {0, 0, 0xC8, 0x37};
  const uint8_t w2[4] = {0, 0, 0x21, 0x9A};
  Compositor c;
  fillTwo(rgb, w1, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w2, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertLayers(c, Color(40, 50, 60, 0x21), Color(70, 80, 90, 0x9A));
}

void test_drain_applies_warm_white_only() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  assertColor(Color(0, 0, 0, 0xC8), c.wispStateBaseColor());
  assertColor(Color(0, 0, 0, 0x37), c.wispStateShadeColor());
}

void test_drain_applies_black() {
  const uint8_t rgb[6] = {0, 0, 0, 0, 0, 0};
  const uint8_t w[4] = {9, 9, 0, 0};
  Compositor c;
  // Painted first, so black must overwrite a live colour.
  const uint8_t seed[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t seedW[4] = {0, 0, 7, 7};
  fillTwo(seed, seedW, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertColor(Color(0, 0, 0, 0), c.wispStateBaseColor());
  assertColor(Color(0, 0, 0, 0), c.wispStateShadeColor());
}

void test_drain_without_w_plane_applies_w_zero() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0x11, 0x22, 0xC8, 0x37};
  Compositor c;
  fillTwo(rgb, w, true);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  fillTwo(rgb, w, false);
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 2000));
  assertColor(Color(40, 50, 60, 0), c.wispStateBaseColor());
  assertColor(Color(70, 80, 90, 0), c.wispStateShadeColor());
}

// A wisp that paints this lamp then goes silent ages out after
// kWispStateFreshMs. A whole millis() wrap later, with no STATE since, its
// paint must not read fresh again.
void test_silent_wisp_stays_aged_out_a_wrap_later() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  const uint32_t paintedMs = 1000;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, paintedMs));
  for (uint64_t t = paintedMs; t < paintedMs + (1ull << 32); t += 1ull << 28) {
    c.retireWispState(static_cast<uint32_t>(t));
  }
  TEST_ASSERT_FALSE(c.wispStateFresh(paintedMs + 10));
}

// Away from the wrap the latch changes nothing: still fresh inside the
// window, and a STATE after an age-out paints again.
void test_wisp_state_retire_keeps_a_fresh_paint() {
  const uint8_t rgb[6] = {40, 50, 60, 70, 80, 90};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 1000));
  c.retireWispState(1000 + Compositor::kWispStateFreshMs - 1);
  TEST_ASSERT_TRUE(c.wispStateFresh(1000 + Compositor::kWispStateFreshMs - 1));
  c.retireWispState(1000 + Compositor::kWispStateFreshMs);
  TEST_ASSERT_FALSE(c.wispStateFresh(1000 + Compositor::kWispStateFreshMs));
  TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, 90000));
  TEST_ASSERT_TRUE(c.wispStateFresh(90001));
}

void test_drain_absent_entry_reports_unpainted() {
  const uint8_t kStranger[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01};
  const uint8_t rgb[6] = {1, 2, 3, 4, 5, 6};
  const uint8_t w[4] = {0, 0, 0, 0};
  fillTwo(rgb, w, true);
  Compositor c;
  TEST_ASSERT_FALSE(c.applyWispStateSlot(gSlot, kStranger, 1000));
}

// Drift fade fixture: every channel moves, both directions, away from 0/255.
const Color kStartBase(20, 210, 60, 15);
const Color kStartShade(160, 40, 200, 70);
const Color kToBase(190, 30, 140, 95);
const Color kToShade(50, 180, 80, 20);
const Color kOtherToBase(100, 120, 10, 230);
const Color kOtherToShade(5, 90, 250, 140);

enum class Trailer { None, MineFull, MinePartial, OtherFull, OtherPartial };

void putColor(uint8_t* p, Color c) {
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.w;
}

// A STATE with kOther at 0 and kMine at 1, kMine's plain colours `base` /
// `shade`, and the drift fade trailer `form`: a record for kMine (or kOther)
// heading to `to*` with `leftMs` left, marked partial or not.
const uint8_t* gFrameSrc = kSrc;

void fillFrame(Color base, Color shade, Trailer form, Color toBase = Color(),
               Color toShade = Color(), uint32_t leftMs = 0) {
  uint8_t entries[2 * lp::WISP_STATE_ENTRY_SIZE];
  std::memcpy(entries, kOther, 6);
  const uint8_t otherRgb[6] = {7, 8, 9, 10, 11, 12};
  std::memcpy(entries + 6, otherRgb, 6);
  std::memcpy(entries + 12, kMine, 6);
  const uint8_t mineRgb[6] = {base.r, base.g, base.b, shade.r, shade.g, shade.b};
  std::memcpy(entries + 18, mineRgb, 6);
  const uint8_t w[4] = {1, 2, base.w, shade.w};
  static uint8_t buf[lp::ESPNOW_V2_FRAME_MAX];
  size_t n = lp::buildWispState(buf, sizeof(buf), 1, gFrameSrc, 100, 60000, 0,
                                entries, w, 2);
  if (form != Trailer::None) {
    uint8_t rec[lp::WISP_STATE_DRIFT_FADE_RECORD_SIZE];
    const bool mine = form == Trailer::MineFull || form == Trailer::MinePartial;
    rec[0] = mine ? 1 : 0;
    putColor(rec + 1, toBase);
    putColor(rec + 5, toShade);
    const uint32_t ds = leftMs / lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS;
    rec[9] = static_cast<uint8_t>(ds & 0xFF);
    rec[10] = static_cast<uint8_t>(ds >> 8);
    const bool partial =
        form == Trailer::MinePartial || form == Trailer::OtherPartial;
    n = lp::appendWispStateDriftFade(buf, sizeof(buf), n, rec, 1, partial);
  }
  lp::ParsedWispState ws;
  TEST_ASSERT_TRUE(lp::parseWispState(buf, n, ws));
  lamp::fillPendingWispState(gSlot, ws, lamp::decodeOwnWispDriftFade(ws, kMine));
}

constexpr uint32_t kFrameMs = 10;

// A compositor driven frame by frame, a STATE applied when `apply` runs.
struct Rig {
  Compositor c;
  uint32_t t = 0;
  void apply() { TEST_ASSERT_TRUE(c.applyWispStateSlot(gSlot, kMine, t)); }
  void runTo(uint32_t ms) {
    while (t < ms) {
      t += kFrameMs;
      c.tickWisp(t, true, true);
    }
  }
  Color base() const { return c.wispLayer(true).wisp().value(); }
  Color shade() const { return c.wispLayer(false).wisp().value(); }
};

int maxStep(Color a, Color b) {
  const int d[4] = {a.r - b.r, a.g - b.g, a.b - b.b, a.w - b.w};
  int m = 0;
  for (int v : d) m = (v < 0 ? -v : v) > m ? (v < 0 ? -v : v) : m;
  return m;
}

bool strictlyBetween(Color a, Color b, Color got) {
  auto in = [](uint8_t x, uint8_t y, uint8_t g) {
    const uint8_t lo = x < y ? x : y;
    const uint8_t hi = x < y ? y : x;
    return g > lo && g < hi;
  };
  return in(a.r, b.r, got.r) && in(a.g, b.g, got.g) && in(a.b, b.b, got.b) &&
         in(a.w, b.w, got.w);
}

// The wisp's plain colour `elapsed` into a `fadeMs` step, as it packs it.
Color wispPlain(Color from, Color to, uint32_t elapsed, uint32_t fadeMs) {
  return lamp::mixColorWeight(from, to, static_cast<float>(elapsed) / fadeMs);
}

// A record repeated 2 s later with the time left it then carries changes
// nothing: the colour 3 s in equals a run that saw only the first record.
void test_drift_record_repeat_does_not_restart() {
  Rig once;
  Rig twice;
  for (Rig* r : {&once, &twice}) {
    fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
    r->apply();
    r->runTo(2000);
  }
  fillFrame(wispPlain(kStartBase, kToBase, 2000, 20000),
            wispPlain(kStartShade, kToShade, 2000, 20000), Trailer::MineFull,
            kToBase, kToShade, 18000);
  twice.apply();
  TEST_ASSERT_TRUE(twice.c.wispFadeSync() == lamp::WispFadeSync::Kept);
  once.runTo(3000);
  twice.runTo(3000);
  TEST_ASSERT_TRUE(strictlyBetween(kStartBase, kToBase, twice.base()));
  assertColor(once.base(), twice.base());
  assertColor(once.shade(), twice.shade());
}

// Frames at 2 s and 4 s lost: the fade runs on its own, never stepping, and
// lands on the target.
void test_drift_record_rides_out_lost_frames() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(kFrameMs);
  Color prevBase = r.base();
  Color prevShade = r.shade();
  while (r.t < 21000) {
    if (r.t == 6000) {
      fillFrame(wispPlain(kStartBase, kToBase, 6000, 20000),
                wispPlain(kStartShade, kToShade, 6000, 20000), Trailer::MineFull,
                kToBase, kToShade, 14000);
      r.apply();
    }
    r.runTo(r.t + kFrameMs);
    TEST_ASSERT_TRUE(maxStep(prevBase, r.base()) <= 1);
    TEST_ASSERT_TRUE(maxStep(prevShade, r.shade()) <= 1);
    prevBase = r.base();
    prevShade = r.shade();
  }
  assertColor(kToBase, r.base());
  assertColor(kToShade, r.shade());
}

// A lamp that joins mid-fade starts from the colour the wisp shows for it and
// lands on the target when the record says.
void test_drift_record_join_starts_at_shown_colour() {
  const Color midBase = wispPlain(kStartBase, kToBase, 12000, 20000);
  const Color midShade = wispPlain(kStartShade, kToShade, 12000, 20000);
  Rig r;
  fillFrame(midBase, midShade, Trailer::MineFull, kToBase, kToShade, 8000);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Started);
  r.runTo(kFrameMs);
  TEST_ASSERT_TRUE(maxStep(midBase, r.base()) <= 1);
  TEST_ASSERT_TRUE(maxStep(midShade, r.shade()) <= 1);
  r.runTo(7990);
  TEST_ASSERT_TRUE(strictlyBetween(midBase, kToBase, r.base()) ||
                   maxStep(kToBase, r.base()) <= 1);
  r.runTo(8000);
  assertColor(kToBase, r.base());
  assertColor(kToShade, r.shade());
}

// A record reaching a lamp already showing the wisp's colour fades on from
// what it shows.
void test_drift_record_while_painted_continues_from_shown() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::None);
  r.apply();
  r.runTo(1000);
  const Color shown = r.base();
  fillFrame(kOtherToBase, kOtherToShade, Trailer::MineFull, kToBase, kToShade,
            10000);
  r.apply();
  r.runTo(r.t + kFrameMs);
  TEST_ASSERT_TRUE(maxStep(shown, r.base()) <= 1);
}

// The record's end within the tolerance keeps the fade; past it retimes.
void test_drift_record_retimes_past_tolerance() {
  const uint32_t tol = lamp::LayerStack::kDriftRetimeToleranceMs;
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(2000);
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade,
            18000 + tol);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Kept);
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade,
            18000 - tol);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Kept);
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade,
            18000 - tol - lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Retimed);
  r.runTo(2000 + 18000 - tol - lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS);
  assertColor(kToBase, r.base());
}

void test_drift_record_retimes_a_later_end() {
  const uint32_t tol = lamp::LayerStack::kDriftRetimeToleranceMs;
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(2000);
  const uint32_t later = 18000 + tol + lp::WISP_STATE_DRIFT_FADE_LEFT_UNIT_MS;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, later);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Retimed);
  r.runTo(20000);
  TEST_ASSERT_TRUE(strictlyBetween(kStartBase, kToBase, r.base()));
  r.runTo(2000 + later);
  assertColor(kToBase, r.base());
}

// A new target mid-fade starts a fade from the colour shown, without a jump.
void test_drift_record_new_target_starts_from_shown() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(10000);
  const Color shown = r.base();
  fillFrame(kToBase, kToShade, Trailer::MineFull, kOtherToBase, kOtherToShade,
            20000);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Started);
  r.runTo(r.t + kFrameMs);
  TEST_ASSERT_TRUE(maxStep(shown, r.base()) <= 1);
  r.runTo(30000);
  assertColor(kOtherToBase, r.base());
}

// A partial frame without this lamp's record leaves its fade running, whatever
// plain colour it carries; the colour matches a run that never saw it.
void test_drift_partial_frame_keeps_running_fade() {
  Rig plain;
  Rig partial;
  for (Rig* r : {&plain, &partial}) {
    fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
    r->apply();
    r->runTo(4000);
  }
  fillFrame(kOtherToBase, kOtherToShade, Trailer::OtherPartial, kOtherToBase,
            kOtherToShade, 5000);
  partial.apply();
  plain.runTo(6000);
  partial.runTo(6000);
  assertColor(plain.base(), partial.base());
  assertColor(plain.shade(), partial.shade());
  TEST_ASSERT_TRUE(partial.c.wispLayer(true).wispFadeRunning(partial.t));
}

// With no fade running, a partial frame without a record eases to the plain
// colour as before.
void test_drift_partial_frame_without_fade_eases_to_plain() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::OtherPartial, kOtherToBase,
            kOtherToShade, 5000);
  r.apply();
  r.runTo(1000);
  assertColor(kStartBase, r.base());
  assertColor(kStartShade, r.shade());
}

// A full frame without this lamp's record, or a frame with no trailer, ends
// its fade with the short ease toward the plain colour.
void endsFadeOnPlain(Trailer form) {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(4000);
  fillFrame(kOtherToBase, kOtherToShade, form, kOtherToBase, kOtherToShade, 5000);
  r.apply();
  TEST_ASSERT_FALSE(r.c.wispLayer(true).wispTimed());
  r.runTo(4500);
  assertColor(kOtherToBase, r.base());
  assertColor(kOtherToShade, r.shade());
}

void test_drift_full_frame_without_record_ends_fade() {
  endsFadeOnPlain(Trailer::OtherFull);
}

void test_drift_no_trailer_ends_fade() { endsFadeOnPlain(Trailer::None); }

// A plain colour unchanged from the last frame still ends a fade.
void test_drift_no_trailer_same_plain_ends_fade() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(2000);
  fillFrame(kStartBase, kStartShade, Trailer::None);
  r.apply();
  TEST_ASSERT_FALSE(r.c.wispLayer(true).wispTimed());
}

// The app indicator's colour holds the fade's target across its frames and
// follows the plain colour once the fade ends.
void test_drift_heading_steady_across_fade() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  assertColor(kToBase, r.c.wispStateHeadingBaseColor());
  assertColor(kToShade, r.c.wispStateHeadingShadeColor());
  for (uint32_t at = 2000; at <= 6000; at += 2000) {
    r.runTo(at);
    fillFrame(wispPlain(kStartBase, kToBase, at, 20000),
              wispPlain(kStartShade, kToShade, at, 20000),
              at == 4000 ? Trailer::OtherPartial : Trailer::MineFull, kToBase,
              kToShade, 20000 - at);
    r.apply();
    assertColor(kToBase, r.c.wispStateHeadingBaseColor());
    assertColor(kToShade, r.c.wispStateHeadingShadeColor());
  }
  fillFrame(kOtherToBase, kOtherToShade, Trailer::None);
  r.apply();
  assertColor(kOtherToBase, r.c.wispStateHeadingBaseColor());
  assertColor(kOtherToShade, r.c.wispStateHeadingShadeColor());
}

// A lamp dropped from the wisp's paint and taken back, or handed to another
// wisp, takes the plain colour from a partial frame instead of an old fade.
void takesPlainAfter(void (*interrupt)(Rig&)) {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 600000);
  r.apply();
  r.runTo(4000);
  interrupt(r);
  fillFrame(kOtherToBase, kOtherToShade, Trailer::OtherPartial, kOtherToBase,
            kOtherToShade, 5000);
  r.apply();
  gFrameSrc = kSrc;
  TEST_ASSERT_FALSE(r.c.wispLayer(true).wispTimed());
  TEST_ASSERT_FALSE(r.c.wispLayer(false).wispTimed());
  r.runTo(r.t + 1000);
  assertColor(kOtherToBase, r.base());
  assertColor(kOtherToShade, r.shade());
  assertColor(kOtherToBase, r.c.wispStateHeadingBaseColor());
}

void test_drift_partial_after_absence_takes_plain() {
  takesPlainAfter([](Rig& r) { r.c.clearWispState(r.t); });
}

void test_drift_partial_after_silence_takes_plain() {
  takesPlainAfter([](Rig& r) { r.runTo(r.t + Compositor::kWispStateFreshMs); });
}

// The notify edge: a heading change on either surface, none across a fade's
// repeated records.
void test_drift_heading_change_flags_notify() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispHeadingChanged());
  r.runTo(2000);
  fillFrame(wispPlain(kStartBase, kToBase, 2000, 20000),
            wispPlain(kStartShade, kToShade, 2000, 20000), Trailer::MineFull,
            kToBase, kToShade, 18000);
  r.apply();
  TEST_ASSERT_FALSE(r.c.wispHeadingChanged());
  // Same base target, new shade target.
  fillFrame(kToBase, kToShade, Trailer::MineFull, kToBase, kOtherToShade, 18000);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispHeadingChanged());
}

void test_drift_partial_from_new_wisp_takes_plain() {
  takesPlainAfter([](Rig&) {
    static const uint8_t kOtherWisp[6] = {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5};
    gFrameSrc = kOtherWisp;
  });
}

// A record with a new target starts a new fade even when its end matches the
// running one's.
void test_drift_record_new_target_same_end_starts() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(5000);
  fillFrame(kToBase, kToShade, Trailer::MineFull, kOtherToBase, kOtherToShade,
            15000);
  r.apply();
  TEST_ASSERT_TRUE(r.c.wispFadeSync() == lamp::WispFadeSync::Started);
  r.runTo(20000);
  assertColor(kOtherToBase, r.base());
  assertColor(kOtherToShade, r.shade());
}

// A finished fade holds its target however long the layer then sits.
void test_drift_finished_fade_holds_across_wrap() {
  Rig r;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  r.runTo(20000);
  for (uint64_t t = 20000; t < (1ull << 32); t += 1ull << 26) {
    r.c.tickWisp(static_cast<uint32_t>(t), true, true);
    assertColor(kToBase, r.base());
  }
  // One wrap on, the clock reads mid-fade again.
  r.c.tickWisp(10000, true, true);
  assertColor(kToBase, r.base());
}

// A fade that spans the millis() wrap runs to its end.
void test_drift_fade_across_millis_wrap() {
  Rig r;
  r.t = 0xFFFFFFFFu - 4999u;
  r.t -= r.t % kFrameMs;
  const uint32_t start = r.t;
  fillFrame(kStartBase, kStartShade, Trailer::MineFull, kToBase, kToShade, 20000);
  r.apply();
  for (uint32_t i = 0; i < 1000; ++i) {
    r.t += kFrameMs;
    r.c.tickWisp(r.t, true, true);
  }
  TEST_ASSERT_TRUE(strictlyBetween(kStartBase, kToBase, r.base()));
  while (r.t - start < 20000) {
    r.t += kFrameMs;
    r.c.tickWisp(r.t, true, true);
  }
  assertColor(kToBase, r.base());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_drain_applies_own_rgbw);
  RUN_TEST(test_drain_feeds_render_layers_rgbw);
  RUN_TEST(test_drain_w_only_repaint_reaches_layers);
  RUN_TEST(test_drain_applies_warm_white_only);
  RUN_TEST(test_drain_applies_black);
  RUN_TEST(test_drain_without_w_plane_applies_w_zero);
  RUN_TEST(test_drain_absent_entry_reports_unpainted);
  RUN_TEST(test_silent_wisp_stays_aged_out_a_wrap_later);
  RUN_TEST(test_wisp_state_retire_keeps_a_fresh_paint);
  RUN_TEST(test_drift_record_repeat_does_not_restart);
  RUN_TEST(test_drift_record_rides_out_lost_frames);
  RUN_TEST(test_drift_record_join_starts_at_shown_colour);
  RUN_TEST(test_drift_record_while_painted_continues_from_shown);
  RUN_TEST(test_drift_record_retimes_past_tolerance);
  RUN_TEST(test_drift_record_retimes_a_later_end);
  RUN_TEST(test_drift_record_new_target_starts_from_shown);
  RUN_TEST(test_drift_partial_frame_keeps_running_fade);
  RUN_TEST(test_drift_partial_frame_without_fade_eases_to_plain);
  RUN_TEST(test_drift_full_frame_without_record_ends_fade);
  RUN_TEST(test_drift_no_trailer_ends_fade);
  RUN_TEST(test_drift_no_trailer_same_plain_ends_fade);
  RUN_TEST(test_drift_heading_steady_across_fade);
  RUN_TEST(test_drift_fade_across_millis_wrap);
  RUN_TEST(test_drift_partial_after_absence_takes_plain);
  RUN_TEST(test_drift_partial_after_silence_takes_plain);
  RUN_TEST(test_drift_heading_change_flags_notify);
  RUN_TEST(test_drift_partial_from_new_wisp_takes_plain);
  RUN_TEST(test_drift_record_new_target_same_end_starts);
  RUN_TEST(test_drift_finished_fade_holds_across_wrap);
  return UNITY_END();
}

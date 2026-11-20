// Native pin for snafu at home with social off: the dots refuse a peer palette
// borrow and drift an active one back to the own palette, and the greeting
// takes no peer colour info. BehaviorContext::ignoresPeers is the seam; the
// rest is the shipped DotsBehavior and Greeting.

#include <unity.h>

#include <algorithm>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
// Carries Expression's typeinfo, which GCC's -fsanitize=vptr checks reference.
#include "../../src/expressions/expression.cpp"
#include "../../src/lamps/snafu/dots_behavior.cpp"
#include "../../src/lamps/snafu/greeting.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/components/network/mesh/lamp_roster.cpp"
#include "../../src/core/peer_view.cpp"
#include "../replaced_new/heap_tally.hpp"

static bool g_ignore = false;
static float g_wisp = 0.0f;

lamp::Config config;
lamp::ExpressionManager expressionManager;

namespace lamp {
OverrideAggregate overrides;
PersonalityEngine personalityEngine;
GreetingTuning PersonalityEngine::greetingFor(const std::string&) const {
  GreetingTuning t;
  t.holdFrames = 100;
  return t;
}
bool Compositor::wispActive() const { return false; }
// The dots read only the shade's wisp presence off the compositor; this one
// holds it at g_wisp.
Compositor::Compositor() {
  context_.compositor = this;
  wispShadePresence_ = EasedScalar(kDefaultEaseRate, g_wisp);
}
bool BehaviorContext::ignoresPeers() const { return g_ignore; }
void BehaviorContext::forEachArrival(uint32_t, const std::function<bool(const PeerView&)>&) {}
void ExpressionManager::sendInvocationTo(const uint8_t*, const ExpressionInvocation&) {}
void ExpressionManager::broadcastInvocation(const ExpressionInvocation&, const uint8_t*) {}
void ExpressionManager::onExpressionFired(Expression*) {}
bool MeshLink::sendColorQuery(const uint8_t*) { return true; }
}  // namespace lamp

using namespace lamp;
using lamp::snafu::DotsBehavior;
using lamp::snafu::Greeting;

static Adafruit_NeoPixel neo0;

namespace {

const Color kOwn(0x40, 0, 0, 0);
const Color kPeer(0, 0x80, 0, 0);
const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};

ShadeSettings ownConfig() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 4, {kOwn}}};
  return cfg;
}

FrameBuffer makeFb() {
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 4}});
  return fb;
}

bool showsPeer(const FrameBuffer& fb) {
  for (const Color& c : fb.buffer) if (c.g > 0) return true;
  return false;
}

void drawFrames(DotsBehavior& d, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) d.draw();
}

// Largest per-channel step between two frames.
int largestStep(const std::vector<Color>& a, const std::vector<Color>& b) {
  int most = 0;
  for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
    for (int d : {a[i].r - b[i].r, a[i].g - b[i].g, a[i].b - b[i].b, a[i].w - b[i].w}) {
      most = std::max(most, d < 0 ? -d : d);
    }
  }
  return most;
}

// A 16 px, three-colour lamp wearing a three-colour peer palette for a
// 2000-frame borrow: `passes` control+draw passes, then home mode starts
// ignoring peers. Returns the step from the last frame before to the first
// after, and the largest step between frames over the 200 passes before it.
struct Continuity {
  int atHomeEntry = 0;
  int duringBorrow = 0;
  int duringDrift = 0;  // the drift-back and the ambient re-scatter after it
  // Once the drift has landed: the own scene, wisp-mixed, with no melt left.
  bool landedOnOwnScene = false;
};

const std::vector<Color> kOwnPalette = {Color(0x80, 0, 0, 0), Color(0, 0, 0x80, 0),
                                        Color(0x40, 0x40, 0, 0)};

// The landed buffer holds exactly the own gradient's pixels (in the scatter's
// order), each mixed with the wisp's paint at its presence.
bool isOwnSceneUnderWisp(const std::vector<Color>& buffer, float wisp, Color wispPaint,
                         const std::vector<Color>& palette = kOwnPalette) {
  std::vector<Color> want = buildGradientWithStops(16, palette);
  for (Color& c : want) c = mixColorWeight(c, wispPaint, wisp);
  std::vector<Color> got = buffer;
  auto key = [](const Color& c) { return (c.r << 24) | (c.g << 16) | (c.b << 8) | c.w; };
  auto byKey = [&](const Color& a, const Color& b) { return key(a) < key(b); };
  std::sort(want.begin(), want.end(), byKey);
  std::sort(got.begin(), got.end(), byKey);
  return want.size() == got.size() && std::equal(want.begin(), want.end(), got.begin());
}

// With `wisp` > 0 the wisp holds that share of the shade: the buffer holds the
// wisp's paint before each draw, as compositeWisp() leaves it.
Continuity homeEntryStep(uint32_t passes, float wisp = 0.0f) {
  ShadeSettings cfg;
  cfg.segments = {{"a", 16, kOwnPalette}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 16}});
  g_wisp = wisp;
  Compositor compositor;
  BehaviorContext ctx;
  ctx.compositor = &compositor;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  const Color wispPaint(0x10, 0x10, 0x60, 0x20);
  auto paintWisp = [&]() {
    if (wisp > 0.0f) fb.buffer.assign(fb.buffer.size(), wispPaint);
  };
  dots.control();  // the lamp has been running before the greeting
  paintWisp();
  dots.draw();
  dots.borrowColors({Color(0, 0x80, 0, 0), Color(0, 0, 0, 0x80), Color(0x20, 0, 0x60, 0)},
                    {Color(0, 0x60, 0x20, 0), Color(0x60, 0, 0x20, 0), Color(0, 0, 0, 0x40)},
                    2000);
  Continuity c;
  std::vector<Color> last;
  for (uint32_t i = 0; i < passes; ++i) {
    dots.control();
    paintWisp();
    dots.draw();
    if (!last.empty() && i + 200 >= passes) {
      c.duringBorrow = std::max(c.duringBorrow, largestStep(last, fb.buffer));
    }
    last = fb.buffer;
  }
  g_ignore = true;
  dots.control();
  paintWisp();
  dots.draw();
  c.atHomeEntry = largestStep(last, fb.buffer);
  // Through the drift and into the next ambient re-scatter after it.
  for (uint32_t i = 0;
       i < DotsBehavior::kBorrowSceneFrames + DotsBehavior::kAmbientHoldFrames + 60; ++i) {
    last = fb.buffer;
    dots.control();
    paintWisp();
    dots.draw();
    c.duringDrift = std::max(c.duringDrift, largestStep(last, fb.buffer));
    if (i == DotsBehavior::kBorrowSceneFrames + 5) {
      c.landedOnOwnScene = isOwnSceneUnderWisp(fb.buffer, wisp, wispPaint);
    }
  }
  g_wisp = 0.0f;
  return c;
}

}  // namespace

void setUp() { g_ignore = false; }
void tearDown() {}

void test_borrow_shows_peer_palette_outside_home_mode() {
  ShadeSettings cfg = ownConfig();
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.borrowColors({kPeer}, {kPeer}, 1000);
  dots.draw();
  TEST_ASSERT_TRUE(showsPeer(fb));
}

void test_borrow_refused_while_ignoring_peers() {
  ShadeSettings cfg = ownConfig();
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  g_ignore = true;
  dots.borrowColors({kPeer}, {kPeer}, 1000);
  dots.draw();
  TEST_ASSERT_FALSE(showsPeer(fb));
}

// An active borrow drifts back to the own palette once home mode starts
// ignoring peers: still on show the first frame, gone after the crossfade.
void test_active_borrow_drifts_home_when_ignoring_starts() {
  ShadeSettings cfg = ownConfig();
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.borrowColors({kPeer}, {kPeer}, 100000);
  dots.draw();

  g_ignore = true;
  dots.control();
  dots.draw();
  TEST_ASSERT_TRUE(showsPeer(fb));
  drawFrames(dots, DotsBehavior::kBorrowSceneFrames);
  TEST_ASSERT_FALSE(showsPeer(fb));

  // Home mode ending does not bring the borrow back.
  g_ignore = false;
  dots.control();
  dots.draw();
  TEST_ASSERT_FALSE(showsPeer(fb));
}

// The greeting's colour info feeds the post-greeting borrow; while ignoring
// peers it takes none, so nothing is borrowed even once home mode ends.
static bool greetingBorrowsAfterColourInfo(bool ignoreDuringInfo) {
  ShadeSettings cfg = ownConfig();
  FrameBuffer shadeFb = makeFb();
  FrameBuffer dotsFb = makeFb();
  LampRoster roster;
  BehaviorContext ctx;
  ctx.lampRoster = &roster;
  DotsBehavior dots(&dotsFb, cfg);
  dots.setBehaviorContext(&ctx);
  Greeting greeting(&shadeFb);
  greeting.setBehaviorContext(&ctx);
  greeting.setDotsBehavior(&dots);

  PeerView peer;
  peer.mac = kPeerMac;
  peer.hasMac = true;
  peer.baseColor = kPeer;
  greeting.triggerGreeting(peer);

  g_ignore = ignoreDuringInfo;
  greeting.onColorInfo(kPeerMac, std::vector<Color>{kPeer}, std::vector<Color>{kPeer});
  g_ignore = false;

  greeting.animationState = STOPPED;  // the greeting's pass ends
  greeting.control();
  dots.draw();
  return showsPeer(dotsFb);
}

void test_greeting_takes_colour_info_outside_home_mode() {
  TEST_ASSERT_TRUE(greetingBorrowsAfterColourInfo(false));
}

void test_greeting_takes_no_colour_info_while_ignoring_peers() {
  TEST_ASSERT_FALSE(greetingBorrowsAfterColourInfo(true));
}

// Home mode starting mid-crossfade (a borrow re-scatter at pass 480, 420
// frames long) drifts back from the scene on show: no jump at entry.
void test_drift_back_is_continuous_mid_crossfade() {
  const Continuity c = homeEntryStep(680);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringBorrow);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.atHomeEntry);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringDrift);
  TEST_ASSERT_TRUE(c.landedOnOwnScene);
}

// Past the borrow's midpoint (pass 1000) the dots melt toward the own
// palette; home mode starting then drifts back from the melted scene.
void test_drift_back_is_continuous_mid_melt() {
  const Continuity c = homeEntryStep(1300);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringBorrow);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.atHomeEntry);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringDrift);
  TEST_ASSERT_TRUE(c.landedOnOwnScene);
}

// With the wisp holding half the shade the melt paints above the wisp mix;
// the drift-back keeps it there, easing out, so entry is still continuous.
void test_drift_back_is_continuous_mid_melt_under_wisp() {
  const Continuity c = homeEntryStep(1300, 0.5f);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringBorrow);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.atHomeEntry);
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, c.duringDrift);
  TEST_ASSERT_TRUE(c.landedOnOwnScene);
}

// The drift-back crossfades at the borrow's pace; once it lands, snafu's own
// ambient re-scatter crossfades at its usual length again.
void test_ambient_crossfade_length_restored_after_drift() {
  ShadeSettings cfg = ownConfig();
  cfg.segments = {{"a", 4, {kOwn, Color(0, 0, 0x40, 0)}}};
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.control();
  dots.draw();
  TEST_ASSERT_EQUAL_UINT32(DotsBehavior::kSceneFrames, dots.frames);
  dots.borrowColors({kPeer}, {kPeer}, 2000);
  for (int i = 0; i < 50; ++i) { dots.control(); dots.draw(); }

  g_ignore = true;
  for (uint32_t i = 0;
       i < DotsBehavior::kBorrowSceneFrames + DotsBehavior::kAmbientHoldFrames + 10; ++i) {
    dots.control();
    dots.draw();
  }
  TEST_ASSERT_EQUAL_UINT32(DotsBehavior::kSceneFrames, dots.frames);
}

// A borrow starting mid-drift (home mode ended, a greeting followed) ends the
// drift: the borrow keeps its own crossfade length, and the melt the drift was
// easing out does not paint over the new borrow.
void test_borrow_mid_drift_keeps_its_crossfade_length() {
  ShadeSettings cfg = ownConfig();
  FrameBuffer fb = makeFb();
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.control();
  dots.draw();
  dots.borrowColors({kPeer}, {kPeer}, 2000);
  for (int i = 0; i < 1900; ++i) { dots.control(); dots.draw(); }  // deep in the melt

  g_ignore = true;
  for (int i = 0; i < 20; ++i) { dots.control(); dots.draw(); }
  g_ignore = false;
  dots.borrowColors({kPeer}, {kPeer}, 2000);
  for (int i = 0; i < 5; ++i) { dots.control(); dots.draw(); }
  TEST_ASSERT_EQUAL_UINT32(DotsBehavior::kBorrowSceneFrames, dots.frames);
  // A solid borrow jitters each dot a few levels; the cut-off melt would pull
  // them most of the way to the own red.
  for (const Color& c : fb.buffer) TEST_ASSERT_LESS_OR_EQUAL_UINT8(8, c.r);
}

// The own palette edited during the borrow, then home mode: the drift-back
// heads for the edited palette from its first frame, with no snap on its
// second.
void test_drift_after_palette_edit_mid_borrow_is_continuous() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 16, kOwnPalette}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 16}});
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.control();
  dots.draw();
  dots.borrowColors({kPeer, Color(0, 0, 0, 0x80)}, {kPeer, Color(0, 0, 0, 0x80)}, 2000);
  for (int i = 0; i < 100; ++i) { dots.control(); dots.draw(); }
  cfg.segments[0].colors = {Color(0, 0, 0x80, 0), Color(0x80, 0x80, 0, 0)};
  for (int i = 0; i < 100; ++i) { dots.control(); dots.draw(); }

  g_ignore = true;
  std::vector<Color> last = fb.buffer;
  int most = 0;
  for (uint32_t i = 0; i < DotsBehavior::kBorrowSceneFrames; ++i) {
    dots.control();
    dots.draw();
    most = std::max(most, largestStep(last, fb.buffer));
    last = fb.buffer;
  }
  TEST_ASSERT_LESS_OR_EQUAL_INT(5, most);
}

// A palette edit during the drift recolours in place, as snafu does for any
// edit, and ends the drift: the ambient crossfade is back to its own length.
void test_palette_edit_mid_drift_ends_the_drift() {
  ShadeSettings cfg;
  cfg.segments = {{"a", 16, kOwnPalette}};
  FrameBuffer fb;
  fb.begin({}, std::vector<StripSegment>{{&neo0, "a", 0, 16}});
  BehaviorContext ctx;
  DotsBehavior dots(&fb, cfg);
  dots.setBehaviorContext(&ctx);
  dots.control();
  dots.draw();
  dots.borrowColors({kPeer, Color(0, 0, 0, 0x80)}, {kPeer, Color(0, 0, 0, 0x80)}, 2000);
  for (int i = 0; i < 1500; ++i) { dots.control(); dots.draw(); }  // deep in the melt

  g_ignore = true;
  for (int i = 0; i < 50; ++i) { dots.control(); dots.draw(); }
  cfg.segments[0].colors = {Color(0, 0, 0x80, 0), Color(0x80, 0x80, 0, 0)};
  dots.control();
  dots.draw();
  dots.control();
  TEST_ASSERT_EQUAL_UINT32(DotsBehavior::kSceneFrames, dots.frames);
  // No melt overlay left: the buffer is the edited palette's gradient.
  dots.draw();
  TEST_ASSERT_TRUE(isOwnSceneUnderWisp(fb.buffer, 0.0f, Color(), cfg.segments[0].colors));
}

// Once the drift-back lands the dots hold no peer stops and no melt target,
// whether or not a melt was under way when home mode started.
void test_landed_drift_holds_nothing() {
  for (int passes : {100, 1900}) {
    ShadeSettings cfg = ownConfig();
    FrameBuffer fb = makeFb();
    BehaviorContext ctx;
    DotsBehavior dots(&fb, cfg);
    dots.setBehaviorContext(&ctx);
    dots.control();
    dots.draw();
    const size_t before = heap_tally::g_liveBytes;
    dots.borrowColors({kPeer, Color(0, 0, 0, 0x80)}, {kPeer, Color(0, 0, 0, 0x80)}, 2000);
    for (int i = 0; i < passes; ++i) { dots.control(); dots.draw(); }
    g_ignore = true;
    for (uint32_t i = 0; i < DotsBehavior::kBorrowSceneFrames + 5; ++i) {
      dots.control();
      dots.draw();
    }
    g_ignore = false;
    TEST_ASSERT_EQUAL_size_t(before, heap_tally::g_liveBytes);
  }
}

// A finished greeting frees the frames it drew from and the peer's palette.
void test_finished_greeting_holds_nothing() {
  FrameBuffer shadeFb = makeFb();
  LampRoster roster;
  BehaviorContext ctx;
  ctx.lampRoster = &roster;
  Greeting greeting(&shadeFb);
  greeting.setBehaviorContext(&ctx);
  PeerView p;
  p.mac = kPeerMac;
  p.hasMac = true;
  p.baseColor = kPeer;
  greeting.control();
  const size_t before = heap_tally::g_liveBytes;
  greeting.triggerGreeting(p);
  greeting.onColorInfo(kPeerMac, std::vector<Color>{kPeer, Color(0, 0, 0, 0x80)},
                       std::vector<Color>{kPeer, Color(0, 0, 0, 0x80)});
  for (int i = 0; i < 10; ++i) greeting.draw();
  greeting.animationState = STOPPED;
  greeting.control();
  TEST_ASSERT_EQUAL_size_t(before, heap_tally::g_liveBytes);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_borrow_shows_peer_palette_outside_home_mode);
  RUN_TEST(test_borrow_refused_while_ignoring_peers);
  RUN_TEST(test_active_borrow_drifts_home_when_ignoring_starts);
  RUN_TEST(test_drift_back_is_continuous_mid_crossfade);
  RUN_TEST(test_drift_back_is_continuous_mid_melt);
  RUN_TEST(test_drift_back_is_continuous_mid_melt_under_wisp);
  RUN_TEST(test_ambient_crossfade_length_restored_after_drift);
  RUN_TEST(test_borrow_mid_drift_keeps_its_crossfade_length);
  RUN_TEST(test_drift_after_palette_edit_mid_borrow_is_continuous);
  RUN_TEST(test_palette_edit_mid_drift_ends_the_drift);
  RUN_TEST(test_landed_drift_holds_nothing);
  RUN_TEST(test_finished_greeting_holds_nothing);
  RUN_TEST(test_greeting_takes_colour_info_outside_home_mode);
  RUN_TEST(test_greeting_takes_no_colour_info_while_ignoring_peers);
  return UNITY_END();
}

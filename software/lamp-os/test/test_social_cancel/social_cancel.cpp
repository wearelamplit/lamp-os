// Native pin for SocialBehavior::cancelGreeting, which the compositor calls on
// a SocialBehavior home mode skips: a greeting in progress ends (STOPPED,
// frame 0) and the app is told once; with none playing it does nothing.
// Links the shipped social.cpp; its personality and mesh dependencies are
// link stubs.

#include <unity.h>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/behaviors/social.cpp"
#include "../../src/components/network/mesh/lamp_roster.cpp"
#include "../../src/core/peer_view.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/core/fondness_engine.cpp"
#include "../../src/util/heap_guard.cpp"
#include "../../src/behaviors/social_reunion.cpp"
#include "../../src/behaviors/copycat.cpp"
#include "../../src/behaviors/roll_cooldown.cpp"


static bool g_ignore = false;

namespace lamp {
OverrideAggregate overrides;
PersonalityEngine personalityEngine;
GreetingTuning PersonalityEngine::greetingFor(const uint8_t*) const { return {}; }
bool MeshLink::sendColorQuery(const uint8_t*) { return true; }
const std::vector<NearbyCopy>& PersonalityEngine::nearbyPeers() const {
  static const std::vector<NearbyCopy> none;
  return none;
}
bool ExpressionManager::triggerInvocation(const ExpressionInvocation&, const uint8_t*, bool,
                                          TransientSource) {
  return false;
}
bool BehaviorContext::ignoresPeers() const { return g_ignore; }
}  // namespace lamp


using namespace lamp;

namespace {
const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};

PeerView peer() {
  PeerView p;
  p.mac = kPeerMac;
  p.hasMac = true;
  return p;
}
}  // namespace

void setUp() { g_ignore = false; }
void tearDown() {}

void test_cancel_ends_greeting_and_tells_app_once() {
  SocialBehavior social(nullptr, 1200);
  int notifies = 0;
  social.setOnGreetingChangeCallback([&]() { ++notifies; });
  social.triggerGreeting(peer());
  social.frame = 40;
  TEST_ASSERT_TRUE(social.greetingState().active);
  notifies = 0;

  social.cancelGreeting();
  TEST_ASSERT_EQUAL_INT(STOPPED, social.animationState);
  TEST_ASSERT_EQUAL_UINT32(0, social.frame);
  TEST_ASSERT_FALSE(social.greetingState().active);
  TEST_ASSERT_EQUAL_INT(1, notifies);

  // The compositor calls it every skipped tick, and control() resumes after
  // home mode: neither tells the app again or restarts the greeting.
  social.cancelGreeting();
  social.control();
  TEST_ASSERT_EQUAL_INT(1, notifies);
  TEST_ASSERT_EQUAL_INT(STOPPED, social.animationState);
}

void test_cancel_without_greeting_is_noop() {
  SocialBehavior social(nullptr, 1200);
  int notifies = 0;
  social.setOnGreetingChangeCallback([&]() { ++notifies; });
  social.cancelGreeting();
  TEST_ASSERT_EQUAL_INT(0, notifies);
}

// A greeting the app asked for is marked so home mode lets it play; the mark
// clears when it ends or is cancelled.
void test_app_greeting_is_marked_until_it_ends() {
  SocialBehavior social(nullptr, 1200);
  TEST_ASSERT_FALSE(social.playsAppGreeting());
  social.triggerGreeting(peer());
  TEST_ASSERT_TRUE(social.playsAppGreeting());

  social.animationState = STOPPED;  // its pass ends
  social.control();
  TEST_ASSERT_FALSE(social.playsAppGreeting());

  social.triggerGreeting(peer());
  social.cancelGreeting();
  TEST_ASSERT_FALSE(social.playsAppGreeting());
}

// The app's greeting plays through the Home Mode preview; when it ends, the
// next control() picks no peer while home mode still ignores them.
static bool greetsPeerAfterAppGreeting(bool ignoring) {
  lampRoster = LampRoster();
  BehaviorContext ctx;
  SocialBehavior social(nullptr, 1200);
  social.setBehaviorContext(&ctx);
  set_mock_millis(100000);
  social.triggerGreeting(peer());

  set_mock_millis(131000);
  lampRoster.addOrUpdateFromBle("near", "AA:BB:CC:DD:EE:F0", Color(), Color(), -50);
  social.animationState = STOPPED;  // the app greeting's pass ends
  g_ignore = ignoring;
  social.control();
  return social.animationState != STOPPED;
}

void test_peer_greeted_after_app_greeting_outside_home_mode() {
  TEST_ASSERT_TRUE(greetsPeerAfterAppGreeting(false));
}

void test_no_peer_picked_after_app_greeting_while_ignoring() {
  TEST_ASSERT_FALSE(greetsPeerAfterAppGreeting(true));
}

// While home mode ignores peers the greeting takes no peer colour info, the
// app's own greeting in the preview included, matching snafu: it shows the
// peer's base colour without its palette.
static bool greetingShowsPeerPalette(bool ignoring) {
  FrameBuffer fb;
  fb.pixelCount = 4;
  fb.buffer.assign(4, Color());
  BehaviorContext ctx;
  SocialBehavior social(&fb, 1200);
  social.setBehaviorContext(&ctx);
  PeerView p = peer();
  p.baseColor = Color(0, 0x80, 0, 0);
  social.triggerGreeting(p);
  social.easeInFrames = 0;  // the stub tuning is all zero: hold for 100 frames
  social.holdFrames = 100;
  social.frames = 100;

  g_ignore = ignoring;
  social.onColorInfo(kPeerMac, {Color(0xF0, 0, 0, 0)}, {});
  bool red = false;
  for (int i = 0; i < 40; ++i) {
    fb.buffer.assign(4, Color());
    social.frame = 1;
    social.draw();
    for (const Color& c : fb.buffer) red = red || c.r > 0;
  }
  return red;
}

void test_greeting_takes_peer_palette_outside_home_mode() {
  TEST_ASSERT_TRUE(greetingShowsPeerPalette(false));
}

void test_greeting_takes_no_peer_palette_while_ignoring() {
  TEST_ASSERT_FALSE(greetingShowsPeerPalette(true));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_cancel_ends_greeting_and_tells_app_once);
  RUN_TEST(test_cancel_without_greeting_is_noop);
  RUN_TEST(test_app_greeting_is_marked_until_it_ends);
  RUN_TEST(test_peer_greeted_after_app_greeting_outside_home_mode);
  RUN_TEST(test_no_peer_picked_after_app_greeting_while_ignoring);
  RUN_TEST(test_greeting_takes_peer_palette_outside_home_mode);
  RUN_TEST(test_greeting_takes_no_peer_palette_while_ignoring);
  return UNITY_END();
}

// Native pin for the reunion greeting and the colour hint, against the shipped
// SocialBehavior, DispositionStore, PersonalityEngine and ExpressionManager: a
// Smitten peer unseen for ReunionTuning::reunionMin is greeted with its hold
// doubled, once; every other greeting is today's; the end of a Fond-or-better
// greeting rolls a colour hint once per window, which plays faint breaths
// and leaves; home mode and the engine's master switch block both. The
// compositor and mesh are link stubs.

#include <unity.h>

#include <algorithm>
#include <cstdlib>

#include "core/animated_behavior.cpp"
#include "behaviors/social.cpp"
#include "behaviors/social_reunion.cpp"
#include "behaviors/copycat.cpp"
#include "behaviors/roll_cooldown.cpp"
#include "components/network/mesh/lamp_roster.cpp"
#include "config/disposition_store.cpp"
#include "core/frame_buffer.cpp"
#include "core/peer_view.cpp"
#include "core/personality_engine.cpp"
#include "expressions/breathing/breathing_expression.cpp"
#include "expressions/cascade_scheduler.cpp"
#include "expressions/expression.cpp"
#include "expressions/expression_invocation.cpp"
#include "expressions/expression_manager.cpp"
#include "expressions/expression_registry.cpp"
#include "expressions/recent_plays.cpp"
#include "util/color.cpp"
#include "util/fade.cpp"
#include "util/gradient.cpp"
#include "util/heap_guard.cpp"

lamp::Config config;

namespace lamp {
OverrideAggregate overrides;
TxCredit EspNowLink::s_txCredit;

std::vector<AnimatedBehavior*> s_registered;

Compositor::Compositor() { context_.compositor = this; }
void Compositor::addBehavior(AnimatedBehavior* b) {
  b->setBehaviorContext(&context_);
  s_registered.push_back(b);
}
void Compositor::removeBehavior(AnimatedBehavior* b) {
  s_registered.erase(std::remove(s_registered.begin(), s_registered.end(), b),
                     s_registered.end());
}
void Compositor::setHomeModePolicy(bool socialDisabled, std::vector<std::string> disabledIds) {
  homeSocialDisabled_ = socialDisabled;
  homeDisabledExprIds_ = std::move(disabledIds);
}

bool MeshLink::sendColorQuery(const uint8_t*) { return true; }
bool MeshLink::sendCommand(const uint8_t*, const uint8_t*, size_t) { return true; }
bool MeshLink::sendEvent(const uint8_t*, size_t) { return true; }
bool MeshLink::isOtaInProgress() const { return false; }
void MeshLink::getMyMac(uint8_t* mac) const { std::memset(mac, 0, 6); }

bool BehaviorContext::ignoresPeers() const {
  return compositor && compositor->blocksRemote(TransientSource::Remote);
}
uint8_t Config::getDisposition(const uint8_t mac[6]) const {
  return dispositions_.peer(mac, uptimeMs()).disp;
}
uint8_t Config::getDisposition(const std::string&) const { return 3; }
}  // namespace lamp

using namespace lamp;

namespace {

constexpr uint64_t kMinMs = 60000;
constexpr uint64_t kStartMs = 100000;
const char* kBle = "AA:BB:CC:DD:EE:F0";
const Color kScene(0, 0, 0x40, 0);
const Color kPeerBase(0, 0x80, 0, 0);
const Color kPeerStop0(0xF0, 0, 0, 0);

void setClock(uint64_t ms) { set_mock_millis(static_cast<uint32_t>(ms)); }

std::array<uint8_t, 6> peerMac() {
  uint8_t ble[6];
  parseBdAddr(kBle, ble);
  std::array<uint8_t, 6> mac;
  meshMacFromBleAddr(ble, mac.data());
  return mac;
}

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  ExpressionManager manager;
  SocialBehavior social{&shade, 1200};
  uint64_t now = kStartMs;

  explicit Rig(SocialMode mode = SocialMode::Ambivert) {
    for (FrameBuffer* fb : {&shade, &base}) {
      fb->pixelCount = 8;
      fb->buffer.assign(8, kScene);
    }
    s_registered.clear();
    lampRoster = LampRoster();
    config = Config();
    config.lamp.socialMode = mode;
    personalityEngine.begin(&config);
    manager.registry().add(BreathingExpression::classDescriptor());
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    social.setConfig(&config);
    social.setBehaviorContext(&compositor.behaviorContext());
    // Social on at home, so a greeting still plays there.
    compositor.setHomeModePolicy(/*socialDisabled=*/false, {});
    setClock(now);
  }

  void advance(uint64_t ms) {
    now += ms;
    setClock(now);
  }
  // The peer arrives in the near view and the next control() greets it.
  bool greet() {
    lampRoster = LampRoster();
    lampRoster.addOrUpdateFromBle("friend", kBle, kPeerBase, Color(), -50);
    social.control();
    return social.animationState != STOPPED;
  }
  // The greeting playing ends; control() runs its stop edge.
  void endGreeting() {
    social.animationState = STOPPED;
    social.control();
  }
  PeerRecord row() { return config.dispositions().peer(peerMac().data(), now); }
};

void assertPlays(const GreetingTuning& t, const SocialBehavior& s) {
  TEST_ASSERT_EQUAL_UINT32(t.totalFrames, s.frames);
  TEST_ASSERT_EQUAL_UINT32(t.easeInFrames, s.easeInFrames);
  TEST_ASSERT_EQUAL_UINT32(t.holdFrames, s.holdFrames);
  TEST_ASSERT_EQUAL_UINT32(t.fadeOutFrames, s.fadeOutFrames);
  TEST_ASSERT_EQUAL_UINT8(t.pulseBackStrength, s.pulseBackStrength);
  TEST_ASSERT_EQUAL_UINT8(t.pulseBackCount, s.pulseBackCount);
  TEST_ASSERT_EQUAL_UINT16(t.breathCycleFrames, s.breathCycleFrames);
  TEST_ASSERT_EQUAL(t.snub, s.snub);
  TEST_ASSERT_EQUAL(t.easeInCurve, s.easeInCurve);
  TEST_ASSERT_EQUAL(t.easeOutCurve, s.easeOutCurve);
  TEST_ASSERT_EQUAL(t.breathCurve, s.breathCurve);
}

// Only the manager's transients register with the stub compositor.
size_t hintsLive() { return s_registered.size(); }

}  // namespace

void setUp() {}
void tearDown() {}

// A Smitten peer back after reunionMin on-minutes: the hold doubles and the
// in-hold breaths with it, every other waveform field as today; the greeting
// restarts its unseen clock, so the next one is today's.
void test_reunion_doubles_smitten_hold_exactly_once() {
  for (SocialMode mode : {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert}) {
    Rig r(mode);
    config.dispositions().set(peerMac().data(), 5, r.now);
    const ReunionTuning& t = r.social.reunionTuning();
    r.advance(t.reunionMin * kMinMs);
    TEST_ASSERT_TRUE(r.greet());

    const GreetingTuning today = greetingTuningFor(mode, 5);
    // Breaths double with the hold only while the profile breathes through all of it.
    TEST_ASSERT_EQUAL_UINT8(kPulseCountContinuous, today.pulseBackCount);
    TEST_ASSERT_TRUE(today.pulseBackStrength > 0);
    GreetingTuning doubled = today;
    doubled.holdFrames = today.holdFrames * 2;
    doubled.totalFrames = today.easeInFrames + doubled.holdFrames + today.fadeOutFrames;
    assertPlays(doubled, r.social);
    TEST_ASSERT_EQUAL_UINT16(0, r.row().unseenMin);

    // Seen again a cooldown later, the peer gets today's greeting.
    r.advance(20 * kMinMs);
    r.endGreeting();
    r.advance(20 * kMinMs);
    TEST_ASSERT_TRUE(r.greet());
    assertPlays(today, r.social);
  }
}

// A reunion outlasts the base cooldown, which runs from greeting start; its
// cooldown runs from its end instead.
void test_reunion_cooldown_runs_from_its_end() {
  Rig r;
  config.dispositions().set(peerMac().data(), 5, r.now);
  r.advance(r.social.reunionTuning().reunionMin * kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  const uint64_t startCooldown = r.social.nextAcknowledgeTimeMs;
  r.advance(45000);
  r.endGreeting();
  TEST_ASSERT_TRUE(r.social.nextAcknowledgeTimeMs > startCooldown);
  TEST_ASSERT_TRUE(r.social.nextAcknowledgeTimeMs >= r.now + 30000);

  // An app greeting that cuts a reunion short keeps its own cooldown.
  r.advance(r.social.reunionTuning().reunionMin * kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  TEST_ASSERT_TRUE(r.social.holdFrames > greetingTuningFor(SocialMode::Ambivert, 5).holdFrames);
  PeerView p;
  const auto mac = peerMac();
  p.mac = mac.data();
  p.hasMac = true;
  r.social.triggerGreeting(p);
  const uint64_t appCooldown = r.social.nextAcknowledgeTimeMs;
  r.advance(45000);
  r.endGreeting();
  TEST_ASSERT_EQUAL_UINT64(appCooldown, r.social.nextAcknowledgeTimeMs);
}

// Fond, Neutral and Salty peers long unseen, and a Smitten one seen within the
// threshold, are greeted exactly as greetingTuningFor says today.
void test_other_greetings_are_todays() {
  struct Case {
    uint8_t disp;
    uint64_t unseenMs;
  };
  const uint16_t T = ReunionTuning{}.reunionMin;
  const Case cases[] = {
      {4, 3u * T * kMinMs}, {3, 3u * T * kMinMs}, {1, 3u * T * kMinMs}, {5, (T - 1) * kMinMs}};
  for (SocialMode mode : {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert}) {
    for (const Case& c : cases) {
      Rig r(mode);
      config.dispositions().set(peerMac().data(), c.disp, r.now);
      r.advance(c.unseenMs);
      TEST_ASSERT_TRUE(r.greet());
      assertPlays(greetingTuningFor(mode, c.disp), r.social);
      TEST_ASSERT_EQUAL_UINT64(r.now + (mode == SocialMode::Introvert ? 60000 : 30000),
                               r.social.nextAcknowledgeTimeMs);
    }
  }
}

// With the engine's master switch off nothing tracks: no reunion, no unseen
// clock restart, no hint.
void test_engine_off_blocks_reunion_and_hint() {
  Rig r;
  config.dispositions().set(peerMac().data(), 5, r.now);
  config.dispositions().setEngineEnabled(false, r.now);
  r.advance(ReunionTuning{}.reunionMin * kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  assertPlays(greetingTuningFor(SocialMode::Ambivert, 5), r.social);
  r.endGreeting();
  TEST_ASSERT_EQUAL_size_t(0, hintsLive());

  // Switched back on, the next greeting's end does borrow the colour.
  config.dispositions().setEngineEnabled(true, r.now);
  r.advance(6 * kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  r.endGreeting();
  TEST_ASSERT_EQUAL_size_t(1, hintsLive());
}

// Home mode opts out entirely: the greeting plays as today, the unseen clock
// keeps running and the greeting's end borrows no colour.
void test_home_mode_blocks_reunion_and_hint() {
  Rig r;
  config.dispositions().set(peerMac().data(), 5, r.now);
  r.advance(ReunionTuning{}.reunionMin * kMinMs);
  r.compositor.homeMode = true;
  TEST_ASSERT_TRUE(r.greet());
  assertPlays(greetingTuningFor(SocialMode::Ambivert, 5), r.social);
  TEST_ASSERT_EQUAL_UINT16(ReunionTuning{}.reunionMin, r.row().unseenMin);
  r.endGreeting();
  TEST_ASSERT_EQUAL_size_t(0, hintsLive());

  // Near and greeted at home, the peer is still not marked seen.
  for (int m = 0; m < 3; ++m) {
    r.advance(kMinMs);
    lampRoster.addOrUpdateFromBle("friend", kBle, kPeerBase, Color(), -50);
    r.social.control();
  }
  TEST_ASSERT_TRUE(r.row().unseenMin > ReunionTuning{}.reunionMin);

  // Home mode ends and the peer arrives again: the reunion still waits.
  r.compositor.homeMode = false;
  r.advance(20 * kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  TEST_ASSERT_EQUAL_UINT32(greetingTuningFor(SocialMode::Ambivert, 5).holdFrames * 2,
                           r.social.holdFrames);
  r.endGreeting();
  TEST_ASSERT_EQUAL_size_t(1, hintsLive());
}

// A Smitten peer greeted and then near for hours is seen all along: gone just
// long enough to prune and back, it gets today's greeting. One away past the
// threshold that returns while the greeting cooldown holds is not seen until
// greeted, so it still gets its reunion.
void test_greeted_near_peer_is_seen_and_arrival_waits() {
  Rig r(SocialMode::Extrovert);
  config.dispositions().set(peerMac().data(), 5, r.now);
  TEST_ASSERT_TRUE(r.greet());
  r.endGreeting();
  const uint16_t T = r.social.reunionTuning().reunionMin;
  for (int m = 0; m < T + 60; ++m) {
    r.advance(kMinMs);
    lampRoster.addOrUpdateFromBle("friend", kBle, kPeerBase, Color(), -50);
    r.social.control();
    TEST_ASSERT_EQUAL(STOPPED, r.social.animationState);
  }
  r.advance(LAMP_PRUNE_TIME_MS + kMinMs);
  TEST_ASSERT_TRUE(r.greet());
  assertPlays(greetingTuningFor(SocialMode::Extrovert, 5), r.social);

  r.endGreeting();
  r.advance(T * kMinMs);
  r.social.nextAcknowledgeTimeMs = r.now + 3 * kMinMs;
  lampRoster = LampRoster();
  for (int m = 0; m < 3; ++m) {
    lampRoster.addOrUpdateFromBle("friend", kBle, kPeerBase, Color(), -50);
    r.social.control();
    TEST_ASSERT_EQUAL(STOPPED, r.social.animationState);
    r.advance(kMinMs);
  }
  TEST_ASSERT_TRUE(r.greet());
  TEST_ASSERT_EQUAL_UINT32(greetingTuningFor(SocialMode::Extrovert, 5).holdFrames * 2,
                           r.social.holdFrames);
}

// The hint roll: Fond and up only, one roll per window whatever its outcome,
// at a chance the crowd halves at crowdHalf lamps.
void test_hint_roll_is_rate_limited_and_crowd_damped() {
  const ReunionTuning t;
  const ContagionTuning c;
  const uint64_t w = t.hintCooldownMs;
  {
    ColourHint h;
    TEST_ASSERT_FALSE(h.roll(3, 0, 1, 0, t, c));
    TEST_ASSERT_TRUE(h.roll(4, 0, 100, 1000, t, c));
    TEST_ASSERT_FALSE(h.roll(5, 0, 1, 1000 + w - 1, t, c));
    TEST_ASSERT_TRUE(h.roll(5, 0, 1, 1000 + w, t, c));
  }
  {
    ColourHint h;
    TEST_ASSERT_FALSE(h.roll(4, c.crowdHalf, 51, 1000, t, c));  // a miss
    TEST_ASSERT_FALSE(h.roll(4, c.crowdHalf, 1, 1000 + w - 1, t, c));
    TEST_ASSERT_TRUE(h.roll(4, c.crowdHalf, 50, 1000 + w, t, c));
  }
}

// The end of a Fond greeting plays shade-only breaths in the friend's
// COLOR_INFO stop 0 (its roster base without one), capped at hintOpacity; it
// stops by itself and is reaped, leaving the scene. A second greeting's end
// inside the window plays none.
void test_hint_plays_faint_breaths_and_leaves() {
  for (bool withStops : {true, false}) {
    Rig r(SocialMode::Extrovert);
    config.dispositions().set(peerMac().data(), 4, r.now);
    // The native esp_random stub seeds a fixed stream whose first draw is 2;
    // crowd damping this weak keeps the chance far above it.
    ContagionTuning sure;
    sure.crowdHalf = kMaxCrowdHalf;
    r.social.setContagionTuning(&sure);
    TEST_ASSERT_TRUE(r.greet());
    if (withStops) r.social.onColorInfo(peerMac().data(), {kPeerStop0}, {});
    r.advance(30000);
    r.endGreeting();
    TEST_ASSERT_EQUAL_size_t(1, hintsLive());

    Expression* e = static_cast<Expression*>(s_registered.front());
    const Color target = withStops ? kPeerStop0 : kPeerBase;
    const uint8_t cap = r.social.reunionTuning().hintOpacity;
    int peakR = 0;
    int peakG = 0;
    int lastToward = 0;
    int prevToward = 0;
    int maxStep = 0;
    uint32_t frames = 0;
    while (!e->isAnimationComplete() && frames < 5000) {
      r.shade.buffer.assign(8, kScene);
      r.base.buffer.assign(8, kScene);
      e->control();
      e->draw();
      lastToward = 0;
      for (const Color& px : r.shade.buffer) {
        peakR = std::max(peakR, std::abs(px.r - kScene.r));
        peakG = std::max(peakG, std::abs(px.g - kScene.g));
        lastToward = std::max(lastToward, std::abs(withStops ? px.r - kScene.r : px.g - kScene.g));
      }
      maxStep = std::max(maxStep, std::abs(lastToward - prevToward));
      prevToward = lastToward;
      for (const Color& px : r.base.buffer) TEST_ASSERT_TRUE(px == kScene);
      r.advance(16);
      ++frames;
    }
    TEST_ASSERT_TRUE(e->isAnimationComplete());
    // The breath moves only toward the friend's colour, at most hintOpacity of
    // the way: stop 0 is red, the roster base green.
    const int toward = withStops ? peakR : peakG;
    const int away = withStops ? peakG : peakR;
    const int span = withStops ? target.r - kScene.r : target.g - kScene.g;
    TEST_ASSERT_EQUAL_INT(0, away);
    TEST_ASSERT_TRUE(toward > span * cap / 200);
    TEST_ASSERT_TRUE(toward <= span * cap / 100 + 1);
    // It fades in and out, never snapping between frames, and its last frame
    // is back on the scene.
    TEST_ASSERT_TRUE(maxStep <= 3);
    TEST_ASSERT_TRUE(lastToward <= 1);
    r.manager.gcTransients();
    TEST_ASSERT_EQUAL_size_t(0, hintsLive());

    r.advance(10 * kMinMs - 60000);
    TEST_ASSERT_TRUE(r.greet());
    r.advance(30000);
    r.endGreeting();
    TEST_ASSERT_EQUAL_size_t(0, hintsLive());
  }
}

// A Neutral greeting's end and an app greeting's end borrow nothing.
void test_hint_only_after_a_fond_organic_greeting() {
  ContagionTuning sure;
  sure.crowdHalf = kMaxCrowdHalf;
  {
    Rig r;
    r.social.setContagionTuning(&sure);
    config.dispositions().set(peerMac().data(), 3, r.now);
    TEST_ASSERT_TRUE(r.greet());
    r.endGreeting();
    TEST_ASSERT_EQUAL_size_t(0, hintsLive());
  }
  {
    Rig r;
    r.social.setContagionTuning(&sure);
    config.dispositions().set(peerMac().data(), 5, r.now);
    PeerView p;
    const auto mac = peerMac();
    p.mac = mac.data();
    p.hasMac = true;
    r.social.triggerGreeting(p);
    r.endGreeting();
    TEST_ASSERT_EQUAL_size_t(0, hintsLive());
  }
}

void test_reunion_tuning_setter_range_checks() {
  ReunionTuning t;
  TEST_ASSERT_TRUE(setReunionTuning(t, "reunionMin", 2));
  TEST_ASSERT_EQUAL_UINT16(2, t.reunionMin);
  TEST_ASSERT_FALSE(setReunionTuning(t, "reunionMin", 70000));
  TEST_ASSERT_FALSE(setReunionTuning(t, "reunionX", 0));
  TEST_ASSERT_FALSE(setReunionTuning(t, "reunionX", kMaxReunionX + 1));
  TEST_ASSERT_FALSE(setReunionTuning(t, "hintOpacity", 5));
  TEST_ASSERT_FALSE(setReunionTuning(t, "hintCooldownMs", kMaxHintCooldownMs + 1));
  TEST_ASSERT_FALSE(setReunionTuning(t, "bogus", 1));
  TEST_ASSERT_EQUAL_UINT8(2, t.reunionX);
  TEST_ASSERT_EQUAL_UINT8(30, t.hintOpacity);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reunion_doubles_smitten_hold_exactly_once);
  RUN_TEST(test_reunion_cooldown_runs_from_its_end);
  RUN_TEST(test_other_greetings_are_todays);
  RUN_TEST(test_engine_off_blocks_reunion_and_hint);
  RUN_TEST(test_home_mode_blocks_reunion_and_hint);
  RUN_TEST(test_hint_roll_is_rate_limited_and_crowd_damped);
  RUN_TEST(test_hint_plays_faint_breaths_and_leaves);
  RUN_TEST(test_greeted_near_peer_is_seen_and_arrival_waits);
  RUN_TEST(test_hint_only_after_a_fond_organic_greeting);
  RUN_TEST(test_reunion_tuning_setter_range_checks);
  return UNITY_END();
}

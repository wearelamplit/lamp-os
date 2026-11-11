// Native pin for yawn contagion: YawnContagion's rolls on their own, then the
// shipped SocialEchoObserver and ExpressionManager on stub surfaces, the
// compositor and mesh as link stubs, a probe registered under the yawn id.

#include <unity.h>

#include <string>

#include "core/animated_behavior.cpp"
#include "expressions/expression.cpp"
#include "expressions/expression_manager.cpp"
#include "expressions/recent_plays.cpp"
#include "expressions/cascade_scheduler.cpp"
#include "expressions/expression_invocation.cpp"
#include "expressions/expression_registry.cpp"
#include "util/heap_guard.cpp"
#include "components/network/mesh/lamp_roster.cpp"
#include "core/peer_view.cpp"
#include "core/frame_buffer.cpp"
#include "util/color.cpp"
#include "behaviors/social_echo.cpp"
#include "behaviors/copycat.cpp"
#include "behaviors/roll_cooldown.cpp"
#include "behaviors/yawn_contagion.cpp"
#include "config/disposition_store.cpp"

namespace lamp {
OverrideAggregate overrides;
TxCredit EspNowLink::s_txCredit;

std::vector<AnimatedBehavior*> s_registered;
std::vector<std::string> s_events;
uint8_t s_disposition = 5;

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

bool MeshLink::sendCommand(const uint8_t*, const uint8_t*, size_t) { return true; }
bool MeshLink::sendEvent(const uint8_t* payload, size_t len) {
  s_events.emplace_back(reinterpret_cast<const char*>(payload), len);
  return true;
}
bool s_ota = false;
bool MeshLink::isOtaInProgress() const { return s_ota; }
void MeshLink::getMyMac(uint8_t* mac) const { std::memset(mac, 0, 6); }
uint8_t Config::getDisposition(const uint8_t*) const { return s_disposition; }
}  // namespace lamp

using namespace lamp;

namespace {

class Probe : public Expression {
 public:
  explicit Probe(FrameBuffer* fb) : Expression(fb, 600) {}
  const ExpressionDescriptor& descriptor() const override;
  void configureFromParameters(const ExpressionParams&) override {}
  void draw() override { nextFrame(); }

 protected:
  void onTrigger() override {}
};

// The real yawn's descriptor data, so one fire spans both surfaces as one
// transient, built as a probe that paints nothing.
constexpr ExpressionDescriptor kYawnProbe = withMake(kYawnDescriptorData, &makeExpr<Probe>);
const ExpressionDescriptor& Probe::descriptor() const { return kYawnProbe; }

const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};
const uint8_t kOtherPeerMac[6] = {1, 2, 3, 4, 5, 7};
bool s_tired = false;

ExpressionInvocation yawnAt(uint8_t hops) {
  ExpressionInvocation inv;
  inv.type = kYawnDescriptorData.id;
  inv.target = TARGET_SHADE;
  inv.hops = hops;
  return inv;
}

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  MeshLink mesh;
  ExpressionManager manager;
  Config config;
  SocialEchoObserver echo{config, manager, mesh};
  Rig() {
    shade.pixelCount = 4;
    shade.buffer.assign(4, Color());
    base.pixelCount = 4;
    base.buffer.assign(4, Color());
    s_registered.clear();
    s_events.clear();
    s_disposition = 5;
    s_tired = false;
    s_ota = false;
    manager.registry().add(kYawnProbe);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    manager.setMeshLink(&mesh);
    config.lamp.socialMode = SocialMode::Extrovert;
    echo.setTired([] { return s_tired; });
    // Every warm yawn-back, at any hop short of the max, and every tired roll fires.
    echo.yawnTuning().backPct = {0, 0, 0, 100, 100};
    echo.tuning().chainHopPct = 100;
    echo.yawnTuning().tiredYawnPct = 100;
  }

  void hear(const ExpressionInvocation& inv, const uint8_t* mac = kPeerMac) {
    echo.onPeerExpression(mac, inv);
    advance(kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  }
  void advance(uint32_t ms) {
    set_mock_millis(millis() + ms);
    manager.retireStale(millis());
    echo.tick(millis());
  }
  // Ticks across one tired interval, one second at a time.
  void tiredInterval() {
    for (uint32_t t = 0; t < echo.yawnTuning().tiredYawnEveryMs; t += 1000) advance(1000);
  }
};

bool eventHasHops(const std::string& payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return true;
  return doc["hp"].is<unsigned>();
}

}  // namespace

void setUp() { set_mock_millis(10000); }
void tearDown() {}

// A tired lamp rolls once per tiredYawnEveryMs, the first a full interval in,
// and fires at tiredYawnPct; a rested one never rolls.
void test_tired_yawns_at_the_tuned_rate() {
  YawnContagion y;
  FastRng rng(0x5EED1234u);
  const uint32_t every = y.tuning.tiredYawnEveryMs;
  const uint32_t intervals = 1000;
  uint32_t rolls = 0;
  uint32_t fires = 0;
  uint32_t now = 5000;
  for (uint32_t t = 0; t <= every * intervals; t += 1000, now += 1000) {
    const TiredRoll r =
        y.tiredRoll(true, now, static_cast<uint8_t>(rng.range(1, 100)), false);
    if (r != TiredRoll::NotDue) ++rolls;
    if (r == TiredRoll::Fire) ++fires;
  }
  TEST_ASSERT_EQUAL_UINT32(intervals, rolls);
  const uint32_t expected = intervals * y.tuning.tiredYawnPct / 100;
  TEST_ASSERT_UINT32_WITHIN(intervals / 20, expected, fires);

  for (uint32_t t = 0; t <= every * 10; t += 1000, now += 1000) {
    TEST_ASSERT_TRUE(y.tiredRoll(false, now, 1, false) == TiredRoll::NotDue);
  }
}

// A tired yawn inside the own floor spends its roll without firing.
void test_tired_roll_holds_the_own_floor() {
  YawnContagion y;
  y.tuning.tiredYawnPct = 100;
  uint32_t now = 1000;
  y.tiredRoll(true, now, 1, false);
  now += y.tuning.tiredYawnEveryMs;
  TEST_ASSERT_TRUE(y.tiredRoll(true, now, 1, true) == TiredRoll::SelfFloor);
  TEST_ASSERT_TRUE(y.tiredRoll(true, now, 1, false) == TiredRoll::NotDue);
  now += y.tuning.tiredYawnEveryMs;
  TEST_ASSERT_TRUE(y.tiredRoll(true, now, 1, false) == TiredRoll::Fire);
}

// Through the observer: a tired lamp yawns as an origin, announced at hop 0.
void test_tired_lamp_fires_and_announces_an_origin() {
  Rig r;
  r.tiredInterval();
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  s_tired = true;
  r.tiredInterval();
  r.advance(1000);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(1, s_events.size());
  TEST_ASSERT_FALSE(eventHasHops(s_events[0]));
}

// Each disposition's chance is its backPct times the mode factor; a Fond peer
// catches more yawns than a Neutral one, and a Salty one none.
void test_fond_peer_catches_more_than_neutral() {
  const YawnTuning y;
  const ContagionTuning c;
  for (uint8_t d = 1; d <= 5; ++d) {
    TEST_ASSERT_EQUAL_UINT8(y.backPct[d - 1] * kMirrorModeFactorX10[1] / 10,
                            yawnBackPct(d, SocialMode::Ambivert, 0, 0, 0, y, c));
  }
  uint32_t caught[6] = {};
  for (uint8_t d = 1; d <= 5; ++d) {
    YawnContagion yc;
    yc.tuning.yawnRollCooldownMs = 0;
    FastRng rng(0xC0FFEEu);
    for (uint32_t i = 0; i < 2000; ++i) {
      const CopyDecision dec = yc.catchYawn(kPeerMac, d, SocialMode::Ambivert, 0, 0, 1000 + i,
                                            static_cast<uint8_t>(rng.range(1, 100)), 0, 0,
                                            false, c);
      if (dec.outcome == CopyOutcome::Fire) ++caught[d];
    }
  }
  TEST_ASSERT_EQUAL_UINT32(0, caught[1]);
  TEST_ASSERT_TRUE(caught[4] > caught[3]);
  TEST_ASSERT_TRUE(caught[5] > caught[4]);
}

// Familiarity raises the chance familiarBoostPct per meeting, ten at most.
void test_familiarity_boost_caps_at_ten_meetings() {
  const YawnTuning y;
  ContagionTuning c;
  c.familiarBoostPct = kMaxFamiliarBoostPct;
  const uint8_t base = yawnBackPct(5, SocialMode::Ambivert, 0, 0, 0, y, c);
  const uint8_t ten = yawnBackPct(5, SocialMode::Ambivert, 0, 10, 0, y, c);
  TEST_ASSERT_EQUAL_UINT8(base * (100 + 10 * c.familiarBoostPct) / 100, ten);
  TEST_ASSERT_EQUAL_UINT8(ten, yawnBackPct(5, SocialMode::Ambivert, 0, 40, 0, y, c));
}

// A peer yawning every second for ten minutes gets one roll per window, every
// roll forced to miss; another peer rolls inside the first one's window.
void test_cooldown_caps_a_peer_at_one_roll_per_window() {
  YawnContagion y;
  const ContagionTuning c;
  const uint32_t window = y.tuning.yawnRollCooldownMs;
  uint32_t rolls = 0;
  for (uint32_t t = 0; t < 2 * window; t += 1000) {
    const CopyDecision d =
        y.catchYawn(kPeerMac, 5, SocialMode::Ambivert, 0, 0, 1000 + t, 100, 0, 0, false, c);
    if (d.outcome == CopyOutcome::Skip || d.outcome == CopyOutcome::Fire) ++rolls;
    if (t == 1000) {
      const CopyDecision other =
          y.catchYawn(kOtherPeerMac, 5, SocialMode::Ambivert, 0, 0, 1000 + t, 100, 0, 0, false, c);
      TEST_ASSERT_TRUE(other.outcome == CopyOutcome::Skip);
    }
  }
  TEST_ASSERT_EQUAL_UINT32(2, rolls);
}

// Every yawn-back chance scales by crowdHalf / (crowdHalf + nearby).
void test_crowd_damps_the_yawn_back() {
  const YawnTuning y;
  const ContagionTuning c;
  const uint8_t alone = yawnBackPct(5, SocialMode::Ambivert, 0, 0, 0, y, c);
  for (size_t nearby : {static_cast<size_t>(8), static_cast<size_t>(24)}) {
    TEST_ASSERT_EQUAL_UINT8(alone * c.crowdHalf / (c.crowdHalf + nearby),
                            yawnBackPct(5, SocialMode::Ambivert, 0, 0, nearby, y, c));
  }
}

// Each hop past an origin's yawn-back rolls at chainHopPct of the one before,
// and none past chainMaxHops.
void test_hops_damp_the_yawn_back() {
  const YawnTuning y;
  const ContagionTuning c;
  const uint8_t origin = yawnBackPct(5, SocialMode::Ambivert, 0, 0, 0, y, c);
  TEST_ASSERT_EQUAL_UINT8(origin * c.chainHopPct / 100,
                          yawnBackPct(5, SocialMode::Ambivert, 1, 0, 0, y, c));
  TEST_ASSERT_EQUAL_UINT8(0, yawnBackPct(5, SocialMode::Ambivert, c.chainMaxHops, 0, 0, y, c));
}

// Home mode stops tired yawns and yawn-backs alike.
void test_home_mode_blocks_yawns() {
  Rig r;
  r.compositor.homeMode = true;
  s_tired = true;
  r.tiredInterval();
  r.tiredInterval();
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(0, s_events.size());

  r.compositor.homeMode = false;
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// A lamp that just yawned does not yawn back at the wave coming back, until
// the own floor has passed.
void test_no_yawn_back_inside_the_own_floor() {
  Rig r;
  s_tired = true;
  r.tiredInterval();
  r.advance(1000);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  s_tired = false;
  r.hear(yawnAt(1), kPeerMac);
  r.hear(yawnAt(1), kOtherPeerMac);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());

  r.advance(r.echo.yawnTuning().yawnSelfFloorMs);
  r.hear(yawnAt(0), kPeerMac);
  TEST_ASSERT_EQUAL_size_t(2, s_registered.size());
}

// A yawn takes only its own roll: with every yawn-back chance at 0, a Smitten
// peer's yawn is never copied by the mirror.
void test_yawn_never_takes_the_mirror_roll() {
  Rig r;
  r.echo.yawnTuning().backPct = {0, 0, 0, 0, 0};
  r.echo.tuning().copyRollCooldownMs = 0;
  for (uint8_t i = 0; i < 20; ++i) {
    const uint8_t mac[6] = {9, 9, 9, 9, 9, i};
    r.hear(yawnAt(0), mac);
  }
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

// With the engine off a yawn is an ordinary expression: no tired yawn, and a
// peer's yawn takes the mirror roll.
void test_engine_off_is_the_plain_mirror() {
  Rig r;
  r.config.dispositions().setEngineEnabled(false, 0);
  r.echo.yawnTuning().backPct = {0, 0, 0, 0, 0};
  r.echo.tuning().copyRollCooldownMs = 0;
  s_tired = true;
  r.tiredInterval();
  r.tiredInterval();
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  s_tired = false;
  for (uint8_t i = 0; i < 20; ++i) {
    const uint8_t mac[6] = {9, 9, 9, 9, 9, i};
    r.echo.onPeerExpression(mac, yawnAt(0));
  }
  r.advance(kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  TEST_ASSERT_TRUE(s_registered.size() > 0);
}

// A yawn-back short of chainMaxHops announces itself one hop on.
void test_yawn_back_announces_one_hop_on() {
  Rig r;
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(1, s_events.size());
  TEST_ASSERT_TRUE(eventHasHops(s_events[0]));
}

// Each personality scales the yawn-back by the mirror mode factor.
void test_mode_factor_scales_the_yawn_back() {
  const YawnTuning y;
  const ContagionTuning c;
  const SocialMode modes[3] = {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert};
  for (uint8_t m = 0; m < 3; ++m) {
    TEST_ASSERT_EQUAL_UINT8(y.backPct[3] * kMirrorModeFactorX10[m] / 10,
                            yawnBackPct(4, modes[m], 0, 0, 0, y, c));
  }
  TEST_ASSERT_TRUE(yawnBackPct(4, SocialMode::Introvert, 0, 0, 0, y, c) <
                   yawnBackPct(4, SocialMode::Extrovert, 0, 0, 0, y, c));
}

// The decision rolls at the chance for the frame's hop, meetings and crowd.
void test_catch_rolls_at_the_frames_chance() {
  YawnContagion yc;
  ContagionTuning c;
  c.familiarBoostPct = kMaxFamiliarBoostPct;
  const CopyDecision d =
      yc.catchYawn(kPeerMac, 5, SocialMode::Extrovert, 1, 6, 1000, 100, 0, 8, false, c);
  TEST_ASSERT_EQUAL_UINT8(yawnBackPct(5, SocialMode::Extrovert, 1, 6, 8, yc.tuning, c), d.pct);
  TEST_ASSERT_TRUE(d.pct < yawnBackPct(5, SocialMode::Extrovert, 0, 0, 0, yc.tuning, c));
  TEST_ASSERT_EQUAL_UINT8(2, d.hops);
}

// The observer hands the near count to the decision: a packed room damps a
// certain yawn-back to nothing.
void test_observer_damps_by_the_near_count() {
  Rig r;
  r.echo.setNearbyCount([] { return static_cast<size_t>(1000); });
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

// Two neighbours' yawns heard before either yawn-back plays book only one.
void test_booked_yawn_back_holds_the_floor() {
  Rig r;
  r.echo.onPeerExpression(kPeerMac, yawnAt(0));
  r.echo.onPeerExpression(kOtherPeerMac, yawnAt(0));
  r.advance(kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// A tired roll falling due while a yawn-back is booked does not fire.
void test_tired_roll_waits_for_a_booked_yawn_back() {
  Rig r;
  s_tired = true;
  r.tiredInterval();
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  r.echo.onPeerExpression(kPeerMac, yawnAt(0));
  r.advance(kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// A yawn-back booked before the engine goes off never plays.
void test_engine_off_drops_a_booked_yawn_back() {
  Rig r;
  r.echo.onPeerExpression(kPeerMac, yawnAt(0));
  r.config.dispositions().setEngineEnabled(false, 0);
  r.advance(kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

// No tired yawn during an OTA or while a local Test plays.
void test_tired_yawn_stands_down_for_ota_and_test() {
  Rig r;
  s_tired = true;
  s_ota = true;
  r.tiredInterval();
  r.tiredInterval();
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  s_ota = false;

  ExpressionConfig config;
  config.type = kYawnDescriptorData.id;
  config.target = TARGET_SHADE;
  r.manager.addExpression(config, /*configBacked=*/false);
  r.compositor.addBehavior(r.manager.getBehaviors()[0]);
  TEST_ASSERT_TRUE(r.manager.markTestActive(kYawnDescriptorData.id, TARGET_SHADE));
  r.tiredInterval();
  r.tiredInterval();
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// A peer's roll stamp reads as old a millis() wrap later, so it rolls again.
void test_roll_stamp_stays_old_a_wrap_later() {
  Rig r;
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  const uint64_t start = millis();
  for (uint64_t now = start; now < start + (1ull << 32); now += 1ull << 26) {
    set_mock_millis(static_cast<uint32_t>(now));
    r.manager.retireStale(millis());
    r.manager.gcTransients();
    r.echo.tick(millis());
  }
  set_mock_millis(static_cast<uint32_t>(start + (1ull << 32) + 10));
  r.manager.retireStale(millis());
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  r.hear(yawnAt(0));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

void test_tuning_range_checks() {
  YawnTuning t;
  TEST_ASSERT_TRUE(setYawnTuning(t, "yawn.fond", 40));
  TEST_ASSERT_EQUAL_UINT8(40, t.backPct[3]);
  TEST_ASSERT_FALSE(setYawnTuning(t, "yawn.fond", 101));
  TEST_ASSERT_EQUAL_UINT8(40, t.backPct[3]);
  TEST_ASSERT_FALSE(setYawnTuning(t, "yawn.bogus", 1));
  TEST_ASSERT_FALSE(setYawnTuning(t, "tiredYawnEveryMs", 0));
  TEST_ASSERT_FALSE(setYawnTuning(t, "yawnRollCooldownMs", kMaxYawnWindowMs + 1));
  TEST_ASSERT_TRUE(setYawnTuning(t, "tiredYawnPct", 0));
  TEST_ASSERT_EQUAL_UINT8(0, t.tiredYawnPct);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_tired_yawns_at_the_tuned_rate);
  RUN_TEST(test_tired_roll_holds_the_own_floor);
  RUN_TEST(test_tired_lamp_fires_and_announces_an_origin);
  RUN_TEST(test_fond_peer_catches_more_than_neutral);
  RUN_TEST(test_familiarity_boost_caps_at_ten_meetings);
  RUN_TEST(test_cooldown_caps_a_peer_at_one_roll_per_window);
  RUN_TEST(test_crowd_damps_the_yawn_back);
  RUN_TEST(test_hops_damp_the_yawn_back);
  RUN_TEST(test_home_mode_blocks_yawns);
  RUN_TEST(test_no_yawn_back_inside_the_own_floor);
  RUN_TEST(test_yawn_never_takes_the_mirror_roll);
  RUN_TEST(test_engine_off_is_the_plain_mirror);
  RUN_TEST(test_yawn_back_announces_one_hop_on);
  RUN_TEST(test_mode_factor_scales_the_yawn_back);
  RUN_TEST(test_catch_rolls_at_the_frames_chance);
  RUN_TEST(test_observer_damps_by_the_near_count);
  RUN_TEST(test_booked_yawn_back_holds_the_floor);
  RUN_TEST(test_tired_roll_waits_for_a_booked_yawn_back);
  RUN_TEST(test_engine_off_drops_a_booked_yawn_back);
  RUN_TEST(test_tired_yawn_stands_down_for_ota_and_test);
  RUN_TEST(test_roll_stamp_stays_old_a_wrap_later);
  RUN_TEST(test_tuning_range_checks);
  return UNITY_END();
}

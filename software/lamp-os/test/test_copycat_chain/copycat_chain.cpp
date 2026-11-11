// Native pin for copycat chains through the shipped SocialEchoObserver and
// ExpressionManager: a copy of a frame h hops out plays, and announces itself
// carrying hop h + 1 only while that is short of chainMaxHops. The compositor
// and mesh are link stubs; the stub mesh records each MSG_EVENT payload.

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
#include "core/fondness_engine.cpp"

namespace lamp {
OverrideAggregate overrides;
TxCredit EspNowLink::s_txCredit;

std::vector<AnimatedBehavior*> s_registered;
std::vector<std::string> s_events;

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
bool MeshLink::isOtaInProgress() const { return false; }
void MeshLink::getMyMac(uint8_t* mac) const { std::memset(mac, 0, 6); }
uint8_t Config::getDisposition(const uint8_t*) const { return 5; }
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

constexpr ExpressionDescriptor kProbe{.id = "probe", .name = "Probe", .make = &makeExpr<Probe>};
const ExpressionDescriptor& Probe::descriptor() const { return kProbe; }

const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};
const uint8_t kOtherPeerMac[6] = {1, 2, 3, 4, 5, 7};

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  MeshLink mesh;
  ExpressionManager manager;
  Config config;
  FondnessEngine fondness{config.dispositions()};
  SocialEchoObserver echo{config, manager, mesh};
  Rig() {
    echo.setFondness(&fondness);
    shade.pixelCount = 4;
    shade.buffer.assign(4, Color());
    base.pixelCount = 4;
    base.buffer.assign(4, Color());
    s_registered.clear();
    s_events.clear();
    manager.registry().add(kProbe);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    manager.setMeshLink(&mesh);
    config.lamp.socialMode = SocialMode::Extrovert;
    echo.tuning().copyRollCooldownMs = 0;
    echo.tuning().chainHopPct = 100;
  }

  // Twenty frames `hops` out, each hop rolling at the Extrovert's 3 in 4, so a
  // copy is booked; the probe stays in flight, so one copy plays and the rest
  // coalesce.
  void hear(uint8_t hops) {
    ExpressionInvocation inv;
    inv.type = "probe";
    inv.target = TARGET_SHADE;
    inv.hops = hops;
    hear(inv);
  }
  // One frame from each of twenty peers, so every roll window is fresh.
  void hearCrowd(const ExpressionInvocation& inv, uint8_t crowdId) {
    for (uint8_t i = 0; i < 20; ++i) {
      const uint8_t mac[6] = {9, 9, 9, 9, crowdId, i};
      echo.onPeerExpression(mac, inv);
    }
    set_mock_millis(millis() + kMirrorDelayFloorMs + kMirrorJitterMs + 1);
    echo.tick(millis());
  }
  void hear(const ExpressionInvocation& inv, const uint8_t* mac = kPeerMac) {
    for (int i = 0; i < 20; ++i) echo.onPeerExpression(mac, inv);
    set_mock_millis(millis() + kMirrorDelayFloorMs + kMirrorJitterMs + 1);
    echo.tick(millis());
  }
};

bool eventHops(const std::string& payload, uint8_t& hops) {
  JsonDocument doc;
  if (deserializeJson(doc, payload) != DeserializationError::Ok) return false;
  ExpressionInvocation inv;
  if (!parseInvocation(doc.as<JsonObjectConst>(), inv)) return false;
  hops = inv.hops;
  return doc["hp"].is<unsigned>();
}

}  // namespace

void setUp() { set_mock_millis(10000); }
void tearDown() {}

// Each frame short of the last hop is copied and the copy announced one hop on.
void test_copy_announces_one_hop_on() {
  for (uint8_t h = 0; h + 1 < ContagionTuning{}.chainMaxHops; ++h) {
    Rig r;
    r.hear(h);
    TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
    TEST_ASSERT_EQUAL_size_t(1, s_events.size());
    uint8_t hops = 0;
    TEST_ASSERT_TRUE(eventHops(s_events[0], hops));
    TEST_ASSERT_EQUAL_UINT8(h + 1, hops);
  }
}

// The copy that reaches chainMaxHops plays and stays quiet.
void test_copy_at_max_plays_without_announcing() {
  Rig r;
  r.hear(static_cast<uint8_t>(r.echo.tuning().chainMaxHops - 1));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(0, s_events.size());
}

// A frame already at chainMaxHops is never copied.
void test_frame_at_max_is_not_copied() {
  Rig r;
  r.hear(r.echo.tuning().chainMaxHops);
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(0, s_events.size());
}

// chainMaxHops 1 is the terminal mirror: an origin's copy plays, unannounced.
void test_max_one_never_announces() {
  Rig r;
  r.echo.tuning().chainMaxHops = 1;
  r.hear(0);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(0, s_events.size());
}

// A copy that does not play (the sender's last one still in flight) sends no
// announce either.
void test_coalesced_copy_is_not_announced() {
  Rig r;
  r.hear(0);
  TEST_ASSERT_EQUAL_size_t(1, s_events.size());
  set_mock_millis(millis() + 1000);
  r.hear(0);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(1, s_events.size());
}

// An origin frame that fits but has no room for `hp` is copied and played,
// and the copy is not announced.
void test_copy_without_room_for_hp_is_not_announced() {
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;
  std::string json;
  bool found = false;
  for (size_t k = 0; k < kExpressionParamKeyCount && !found; ++k) {
    if (std::strncmp(kExpressionParamKeys[k], "cascade", 7) == 0) continue;
    for (uint32_t v = 1; v <= 1000000000u && !found; v *= 10) {
      inv.parameters.set(kExpressionParamKeys[k], v);
      json.clear();
      serializeInvocation(inv, json);
      found = json.size() + 7 > lamp_protocol::EVENT_MAX_PAYLOAD &&
              json.size() <= lamp_protocol::EVENT_MAX_PAYLOAD;
      if (json.size() > lamp_protocol::EVENT_MAX_PAYLOAD) {
        inv.parameters.erase(kExpressionParamKeys[k]);
        break;
      }
    }
  }
  TEST_ASSERT_TRUE(found);
  Rig r;
  r.hear(inv);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(0, s_events.size());
}

static ExpressionInvocation probeAt(uint8_t hops) {
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;
  inv.hops = hops;
  return inv;
}

// A lamp that just played a type, its own fire, does not copy a peer's frame
// of it until copyRollCooldownMs has passed.
void test_own_fire_blocks_copies_for_the_window() {
  Rig r;
  r.echo.tuning().copyRollCooldownMs = ContagionTuning{}.copyRollCooldownMs;
  ExpressionConfig config;
  config.type = "probe";
  config.target = TARGET_SHADE;
  r.manager.addExpression(config, /*configBacked=*/false);
  r.compositor.addBehavior(r.manager.getBehaviors()[0]);
  TEST_ASSERT_TRUE(r.manager.triggerExpression("probe"));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());

  r.hearCrowd(probeAt(0), 1);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());

  set_mock_millis(millis() + r.echo.tuning().copyRollCooldownMs);
  r.manager.retireStale(millis());
  r.echo.tick(millis());
  r.hearCrowd(probeAt(0), 2);
  TEST_ASSERT_TRUE(s_registered.size() > 1);
}

// The manager's retire keeps a play from reading as recent a millis() wrap later.
void test_play_stamp_stays_old_a_wrap_later() {
  Rig r;
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(probeAt(0), kPeerMac));
  const uint32_t playedMs = millis();
  for (uint64_t now = playedMs; now < playedMs + (1ull << 32); now += 1ull << 28) {
    r.manager.retireStale(static_cast<uint32_t>(now));
  }
  TEST_ASSERT_FALSE(r.manager.playedWithin("probe", ContagionTuning{}.copyRollCooldownMs,
                                           playedMs + 10));
}

// A, B, A: a lamp that copied a type does not copy the chain coming back.
void test_copy_blocks_the_chain_coming_back() {
  Rig r;
  r.echo.tuning().copyRollCooldownMs = ContagionTuning{}.copyRollCooldownMs;
  r.hear(probeAt(0), kPeerMac);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  r.hearCrowd(probeAt(1), 1);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// A crowded room damps every copy: at 1000 lamps near, the chance rounds to 0.
void test_nearby_count_damps_copies() {
  Rig r;
  r.echo.setNearbyCount([] { return static_cast<size_t>(1000); });
  r.hearCrowd(probeAt(0), 1);
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

// An Introvert's booked copy holds every peer's copy until the floor ends.
void test_introvert_floor_holds_across_peers() {
  Rig r;
  r.config.lamp.socialMode = SocialMode::Introvert;
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;
  r.hear(inv, kPeerMac);
  r.hear(inv, kOtherPeerMac);
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// release/1.3.3's mirror decision, verbatim, as the oracle for the engine-off
// path.
bool mirrorDecision133(uint8_t disposition, SocialMode mode, uint8_t roll, uint32_t jitterRoll,
                       uint32_t nowMs, bool everMirrored, uint32_t lastMirrorMs,
                       uint32_t& outFireAt) {
  if (disposition < 4) return false;
  const uint8_t rate = mirrorRatePct(disposition, mode);
  if (rate == 0) return false;
  if (roll > rate) return false;
  if (mode == SocialMode::Introvert && everMirrored &&
      nowMs - lastMirrorMs < kIntrovertMirrorCooldownMs) {
    return false;
  }
  const uint32_t jitter = jitterRoll > kMirrorJitterMs ? kMirrorJitterMs : jitterRoll;
  outFireAt = nowMs + kMirrorDelayFloorMs + jitter;
  return true;
}

// With the engine off a copy is 1.3.3's: the same decision at every
// disposition, mode, roll and jitter, the introvert floor included, and none
// of the chain, roll cooldown, crowd damping or self-echo rules, so a frame
// at chainMaxHops from a packed room, heard just after this lamp played the
// type, is still copied, and the copy carries no hops and announces nothing.
void test_engine_off_copies_as_1_3_3() {
  const SocialMode modes[3] = {SocialMode::Introvert, SocialMode::Ambivert, SocialMode::Extrovert};
  for (uint8_t disp = 0; disp <= 5; ++disp) {
    for (SocialMode mode : modes) {
      for (uint8_t roll = 1; roll <= 100; ++roll) {
        for (uint32_t jitter : {0u, 137u, kMirrorJitterMs, kMirrorJitterMs + 50}) {
          Copycat c;
          uint32_t want = 0;
          const bool fires = mirrorDecision133(disp, mode, roll, jitter, 5000, false, 0, want);
          const CopyDecision d = c.mirror(disp, mode, 5000, roll, jitter);
          TEST_ASSERT_EQUAL(fires, d.outcome == CopyOutcome::Fire);
          if (fires) TEST_ASSERT_EQUAL_UINT32(want, d.fireAt);
          c.booked(5000);
          uint32_t unused = 0;
          TEST_ASSERT_EQUAL(mirrorDecision133(disp, mode, roll, jitter, 6000, true, 5000, unused),
                            c.mirror(disp, mode, 6000, roll, jitter).outcome == CopyOutcome::Fire);
        }
      }
    }
  }

  Rig r;
  r.config.dispositions().setEngineEnabled(false, 0);
  r.echo.tuning().copyRollCooldownMs = kMaxCopyRollCooldownMs;
  r.echo.setNearbyCount([] { return static_cast<size_t>(1000); });
  ExpressionConfig own;
  own.type = "probe";
  own.target = TARGET_SHADE;
  r.manager.addExpression(own, /*configBacked=*/false);
  r.compositor.addBehavior(r.manager.getBehaviors()[0]);
  TEST_ASSERT_TRUE(r.manager.triggerExpression("probe"));
  const size_t played = s_registered.size();
  const size_t sent = s_events.size();
  r.hear(r.echo.tuning().chainMaxHops);
  TEST_ASSERT_EQUAL_size_t(played + 1, s_registered.size());
  TEST_ASSERT_EQUAL_size_t(sent, s_events.size());
}

// The copy odds count the store's meetings only while the engine is on.
void test_boost_meets_follow_the_switch() {
  DispositionStore ds(100);
  const uint8_t mac[6] = {9, 8, 7, 6, 5, 4};
  for (int k = 0; k < 5; ++k) ds.meet(mac, 0, 0);
  TEST_ASSERT_EQUAL_UINT8(5, SocialEchoObserver::boostMeets(ds, mac, 0));
  ds.setEngineEnabled(false, 0);
  TEST_ASSERT_EQUAL_UINT8(0, SocialEchoObserver::boostMeets(ds, mac, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_boost_meets_follow_the_switch);
  RUN_TEST(test_copy_announces_one_hop_on);
  RUN_TEST(test_copy_at_max_plays_without_announcing);
  RUN_TEST(test_frame_at_max_is_not_copied);
  RUN_TEST(test_max_one_never_announces);
  RUN_TEST(test_coalesced_copy_is_not_announced);
  RUN_TEST(test_copy_without_room_for_hp_is_not_announced);
  RUN_TEST(test_introvert_floor_holds_across_peers);
  RUN_TEST(test_own_fire_blocks_copies_for_the_window);
  RUN_TEST(test_copy_blocks_the_chain_coming_back);
  RUN_TEST(test_nearby_count_damps_copies);
  RUN_TEST(test_play_stamp_stays_old_a_wrap_later);
  RUN_TEST(test_engine_off_copies_as_1_3_3);
  return UNITY_END();
}

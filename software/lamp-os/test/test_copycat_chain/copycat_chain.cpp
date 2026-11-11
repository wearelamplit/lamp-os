// Native pin for copycat chains through the shipped SocialEchoObserver and
// ExpressionManager: a copy of a frame h hops out plays, and announces itself
// carrying hop h + 1 only while that is short of chainMaxHops. The compositor
// and mesh are link stubs; the stub mesh records each MSG_EVENT payload.

#include <unity.h>

#include <string>

#include "core/animated_behavior.cpp"
#include "expressions/expression.cpp"
#include "expressions/expression_manager.cpp"
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
#include "config/disposition_store.cpp"

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
  SocialEchoObserver echo{config, manager, mesh};
  Rig() {
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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_copy_announces_one_hop_on);
  RUN_TEST(test_copy_at_max_plays_without_announcing);
  RUN_TEST(test_frame_at_max_is_not_copied);
  RUN_TEST(test_max_one_never_announces);
  RUN_TEST(test_coalesced_copy_is_not_announced);
  RUN_TEST(test_copy_without_room_for_hp_is_not_announced);
  RUN_TEST(test_introvert_floor_holds_across_peers);
  return UNITY_END();
}

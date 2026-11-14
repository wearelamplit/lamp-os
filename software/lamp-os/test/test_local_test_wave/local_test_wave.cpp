// Native pin for what the app's Test sends, against the shipped
// ExpressionManager: a Test follows the same rule as the expression firing on
// its own. Once it has played here it announces the play (MSG_EVENT) and
// sends the cascade (MSG_COMMAND) only when its parameters set
// cascadeEnabled; a Test that did not play sends nothing, and the next tap
// tries again. The type-only trigger shifty chains into glitchy with follows
// the same rule. A continuous type or a running OTA sends nothing. snafu's
// crowd fan-out (broadcastInvocation)
// keeps its forced cascade, and a peer's invocation sends neither. The
// compositor and mesh are link stubs; the stub mesh counts each frame.

#include <unity.h>

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

namespace lamp {
OverrideAggregate overrides;
TxCredit EspNowLink::s_txCredit;

size_t s_commands = 0;
size_t s_events = 0;
bool s_ota = false;

Compositor::Compositor() { context_.compositor = this; }
void Compositor::addBehavior(AnimatedBehavior* b) { b->setBehaviorContext(&context_); }
void Compositor::removeBehavior(AnimatedBehavior*) {}
void Compositor::setHomeModePolicy(bool socialDisabled, std::vector<std::string> disabledIds) {
  homeSocialDisabled_ = socialDisabled;
  homeDisabledExprIds_ = std::move(disabledIds);
}

bool MeshLink::sendCommand(const uint8_t*, const uint8_t*, size_t) {
  ++s_commands;
  return true;
}
bool MeshLink::sendEvent(const uint8_t*, size_t) {
  ++s_events;
  return true;
}
bool MeshLink::isOtaInProgress() const { return s_ota; }
void MeshLink::getMyMac(uint8_t* mac) const { std::memset(mac, 0, 6); }
}  // namespace lamp

using namespace lamp;

namespace {

// Paints nothing; plays one long cycle so a transient stays in flight.
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

class Steady : public Probe {
 public:
  using Probe::Probe;
  const ExpressionDescriptor& descriptor() const override;
};

constexpr ExpressionDescriptor kSteady{
    .id = "steady", .name = "Steady", .continuous = true, .make = &makeExpr<Steady>};
const ExpressionDescriptor& Steady::descriptor() const { return kSteady; }

const uint8_t kLocalTestMac[6] = {};
const uint8_t kPeerMacs[3][6] = {{1, 2, 3, 4, 5, 6}, {1, 2, 3, 4, 5, 7}, {1, 2, 3, 4, 5, 8}};

enum class Cascade { Absent, Off, On };

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  MeshLink mesh;
  ExpressionManager manager;
  Rig() {
    shade.pixelCount = 4;
    shade.buffer.assign(4, Color());
    base.pixelCount = 4;
    base.buffer.assign(4, Color());
    s_commands = 0;
    s_events = 0;
    s_ota = false;
    overrides.setSurfacePreview(0x03, false);
    lampRoster = LampRoster();
    for (const auto& mac : kPeerMacs) {
      lampRoster.addOrUpdateFromEspNow("peer", mac, Color(), Color());
    }
    manager.registry().add(kProbe);
    manager.registry().add(kSteady);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    manager.setMeshLink(&mesh);
  }

  static ExpressionInvocation probe(Cascade cascade, const char* type = "probe") {
    ExpressionInvocation inv;
    inv.type = type;
    inv.target = TARGET_SHADE;
    inv.colors = {Color(255, 0, 0, 0)};
    if (cascade != Cascade::Absent) {
      inv.parameters.set(kParamCascadeEnabled, cascade == Cascade::On ? 1 : 0);
    }
    return inv;
  }

  // dispatchLampAction's `test_expression` with colours.
  bool testWithColours(Cascade cascade, const char* type = "probe") {
    return manager.triggerInvocation(probe(cascade, type), kLocalTestMac, /*broadcast=*/true,
                                     TransientSource::Local);
  }

  void addSaved(Cascade cascade) {
    ExpressionConfig config;
    config.type = "probe";
    config.target = TARGET_SHADE;
    config.colors = {Color(255, 0, 0, 0)};
    config.parameters = probe(cascade).parameters;
    manager.addExpression(config, /*configBacked=*/false);
    for (AnimatedBehavior* b : manager.getBehaviors()) compositor.addBehavior(b);
  }

  // dispatchLampAction's `test_expression` without colours: a saved entry by name.
  bool testSaved(Cascade cascade) {
    addSaved(cascade);
    return manager.triggerExpression("probe", TARGET_SHADE);
  }

  // The type-only trigger shifty's onComplete chains into glitchy with.
  bool chainByType(Cascade cascade) {
    addSaved(cascade);
    return manager.triggerExpression("probe");
  }
};

constexpr size_t kPeerCount = sizeof(kPeerMacs) / sizeof(kPeerMacs[0]);

}  // namespace

void setUp() {
  set_mock_millis(10000);
  g_mock_largest_free_block = 1 << 20;
}
void tearDown() {
  g_mock_largest_free_block = 1 << 20;
  overrides.setSurfacePreview(0x03, false);
  s_ota = false;
}

void test_colours_test_cascade_off_announces_only() {
  Rig r;
  TEST_ASSERT_TRUE(r.testWithColours(Cascade::Off));
  TEST_ASSERT_EQUAL_size_t(0, s_commands);
  TEST_ASSERT_EQUAL_size_t(1, s_events);
}

void test_colours_test_cascade_absent_announces_only() {
  Rig r;
  TEST_ASSERT_TRUE(r.testWithColours(Cascade::Absent));
  TEST_ASSERT_EQUAL_size_t(0, s_commands);
  TEST_ASSERT_EQUAL_size_t(1, s_events);
}

void test_colours_test_cascade_on_commands_every_peer_and_announces() {
  Rig r;
  TEST_ASSERT_TRUE(r.testWithColours(Cascade::On));
  TEST_ASSERT_EQUAL_size_t(kPeerCount, s_commands);
  TEST_ASSERT_EQUAL_size_t(1, s_events);
}

void test_colours_test_that_did_not_play_sends_nothing() {
  for (Cascade cascade : {Cascade::On, Cascade::Off}) {
    {
      Rig r;
      g_mock_largest_free_block = 0;
      TEST_ASSERT_FALSE(r.testWithColours(cascade));
      TEST_ASSERT_EQUAL_size_t(0, s_commands);
      TEST_ASSERT_EQUAL_size_t(0, s_events);
    }
    g_mock_largest_free_block = 1 << 20;
    {
      Rig r;
      overrides.setSurfacePreview(0x02, true);
      TEST_ASSERT_FALSE(r.testWithColours(cascade));
      TEST_ASSERT_EQUAL_size_t(0, s_commands);
      TEST_ASSERT_EQUAL_size_t(0, s_events);
    }
  }
}

void test_saved_test_follows_the_same_toggle() {
  {
    Rig r;
    TEST_ASSERT_TRUE(r.testSaved(Cascade::Off));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(1, s_events);
  }
  {
    Rig r;
    TEST_ASSERT_TRUE(r.testSaved(Cascade::On));
    TEST_ASSERT_EQUAL_size_t(kPeerCount, s_commands);
    TEST_ASSERT_EQUAL_size_t(1, s_events);
  }
}

void test_saved_test_that_did_not_play_sends_nothing() {
  Rig r;
  overrides.setSurfacePreview(0x02, true);
  const bool fired = r.testSaved(Cascade::On);
  TEST_ASSERT_EQUAL_size_t(0, s_commands);
  TEST_ASSERT_EQUAL_size_t(0, s_events);
  TEST_ASSERT_FALSE(fired);
}

void test_a_test_tapped_again_after_a_refusal_plays() {
  Rig r;
  g_mock_largest_free_block = 0;
  TEST_ASSERT_FALSE(r.testWithColours(Cascade::On));
  g_mock_largest_free_block = 1 << 20;
  set_mock_millis(millis() + 1);
  TEST_ASSERT_TRUE(r.testWithColours(Cascade::On));
  TEST_ASSERT_EQUAL_size_t(kPeerCount, s_commands);
  TEST_ASSERT_EQUAL_size_t(1, s_events);
}

void test_type_only_trigger_sends_only_when_it_fired() {
  {
    Rig r;
    TEST_ASSERT_TRUE(r.chainByType(Cascade::On));
    TEST_ASSERT_EQUAL_size_t(kPeerCount, s_commands);
    TEST_ASSERT_EQUAL_size_t(1, s_events);
  }
  {
    Rig r;
    overrides.setSurfacePreview(0x02, true);
    const bool fired = r.chainByType(Cascade::On);
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(0, s_events);
    TEST_ASSERT_FALSE(fired);
  }
}

void test_continuous_type_sends_nothing() {
  {
    Rig r;
    TEST_ASSERT_TRUE(r.testWithColours(Cascade::On, "steady"));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(0, s_events);
  }
  {
    Rig r;
    r.manager.broadcastInvocation(Rig::probe(Cascade::On, "steady"));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(0, s_events);
  }
}

void test_ota_sends_nothing() {
  {
    Rig r;
    s_ota = true;
    TEST_ASSERT_TRUE(r.testWithColours(Cascade::On));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(0, s_events);
  }
  {
    Rig r;
    s_ota = true;
    r.manager.broadcastInvocation(Rig::probe(Cascade::On));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(0, s_events);
  }
}

void test_crowd_fan_out_cascades_without_the_toggle() {
  Rig r;
  r.manager.broadcastInvocation(Rig::probe(Cascade::Absent), kPeerMacs[0]);
  TEST_ASSERT_EQUAL_size_t(kPeerCount - 1, s_commands);
  TEST_ASSERT_EQUAL_size_t(1, s_events);
}

void test_peer_invocation_sends_nothing() {
  Rig r;
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(Rig::probe(Cascade::On), kPeerMacs[0]));
  TEST_ASSERT_EQUAL_size_t(0, s_commands);
  TEST_ASSERT_EQUAL_size_t(0, s_events);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_colours_test_cascade_off_announces_only);
  RUN_TEST(test_colours_test_cascade_absent_announces_only);
  RUN_TEST(test_colours_test_cascade_on_commands_every_peer_and_announces);
  RUN_TEST(test_colours_test_that_did_not_play_sends_nothing);
  RUN_TEST(test_saved_test_follows_the_same_toggle);
  RUN_TEST(test_saved_test_that_did_not_play_sends_nothing);
  RUN_TEST(test_a_test_tapped_again_after_a_refusal_plays);
  RUN_TEST(test_type_only_trigger_sends_only_when_it_fired);
  RUN_TEST(test_continuous_type_sends_nothing);
  RUN_TEST(test_ota_sends_nothing);
  RUN_TEST(test_crowd_fan_out_cascades_without_the_toggle);
  RUN_TEST(test_peer_invocation_sends_nothing);
  return UNITY_END();
}

// Native pin for what the app's Test sends, against the shipped
// ExpressionManager: a Test follows the same rule as the expression firing on
// its own, announcing its play (MSG_EVENT) every time and sending the cascade
// (MSG_COMMAND) only when its parameters set cascadeEnabled. snafu's crowd
// fan-out (broadcastInvocation) keeps its forced cascade, and a peer's
// invocation sends neither. The compositor and mesh are link stubs; the stub
// mesh counts each frame.

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
bool MeshLink::isOtaInProgress() const { return false; }
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
    lampRoster = LampRoster();
    for (const auto& mac : kPeerMacs) {
      lampRoster.addOrUpdateFromEspNow("peer", mac, Color(), Color());
    }
    manager.registry().add(kProbe);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    manager.setMeshLink(&mesh);
  }

  static ExpressionInvocation probe(Cascade cascade) {
    ExpressionInvocation inv;
    inv.type = "probe";
    inv.target = TARGET_SHADE;
    inv.colors = {Color(255, 0, 0, 0)};
    if (cascade != Cascade::Absent) {
      inv.parameters.set(kParamCascadeEnabled, cascade == Cascade::On ? 1 : 0);
    }
    return inv;
  }

  // dispatchLampAction's `test_expression` with colours.
  bool testWithColours(Cascade cascade) {
    return manager.triggerInvocation(probe(cascade), kLocalTestMac, /*broadcast=*/true,
                                     TransientSource::Local);
  }

  // dispatchLampAction's `test_expression` without colours: a saved entry by name.
  bool testSaved(Cascade cascade) {
    ExpressionConfig config;
    config.type = "probe";
    config.target = TARGET_SHADE;
    config.colors = {Color(255, 0, 0, 0)};
    config.parameters = probe(cascade).parameters;
    manager.addExpression(config, /*configBacked=*/false);
    return manager.triggerExpression("probe", TARGET_SHADE);
  }
};

constexpr size_t kPeerCount = sizeof(kPeerMacs) / sizeof(kPeerMacs[0]);

}  // namespace

void setUp() {
  set_mock_millis(10000);
  g_mock_largest_free_block = 1 << 20;
}
void tearDown() { g_mock_largest_free_block = 1 << 20; }

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

void test_colours_test_refused_here_still_sends_by_the_toggle() {
  {
    Rig r;
    g_mock_largest_free_block = 0;
    TEST_ASSERT_FALSE(r.testWithColours(Cascade::On));
    TEST_ASSERT_EQUAL_size_t(kPeerCount, s_commands);
    TEST_ASSERT_EQUAL_size_t(1, s_events);
  }
  {
    Rig r;
    g_mock_largest_free_block = 0;
    TEST_ASSERT_FALSE(r.testWithColours(Cascade::Off));
    TEST_ASSERT_EQUAL_size_t(0, s_commands);
    TEST_ASSERT_EQUAL_size_t(1, s_events);
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
  RUN_TEST(test_colours_test_refused_here_still_sends_by_the_toggle);
  RUN_TEST(test_saved_test_follows_the_same_toggle);
  RUN_TEST(test_crowd_fan_out_cascades_without_the_toggle);
  RUN_TEST(test_peer_invocation_sends_nothing);
  return UNITY_END();
}

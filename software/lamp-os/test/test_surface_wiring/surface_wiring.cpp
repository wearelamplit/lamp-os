// Links the real ExpressionManager against a stub compositor and mesh: the
// instances addExpression and triggerInvocation build per target, transient
// slot use, and the per-surface wisp weight.

#include <unity.h>

#include <vector>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/expressions/cascade_scheduler.cpp"
#include "../../src/expressions/expression.cpp"
#include "../../src/expressions/expression_invocation.cpp"
#include "../../src/expressions/expression_manager.cpp"
#include "../../src/expressions/recent_plays.cpp"
#include "../../src/expressions/expression_registry.cpp"
#include "../../src/expressions/pulse/pulse_expression.cpp"
#include "../../src/util/color.cpp"
#include "components/network/transport/espnow_link.hpp"

using namespace lamp;

namespace {
std::vector<AnimatedBehavior*> g_added;
float g_baseWisp = 0.0f;
float g_shadeWisp = 0.0f;
}  // namespace

namespace lamp {
OverrideAggregate overrides;
LampRoster::LampRoster() {}
LampRoster lampRoster;
TxCredit EspNowLink::s_txCredit;
bool heapCanSpare(size_t) { return true; }
std::vector<MeshPeer>& LampRoster::getMesh(uint32_t) {
  static std::vector<MeshPeer> none;
  return none;
}
void MeshLink::getMyMac(uint8_t*) const {}
bool MeshLink::isOtaInProgress() const { return false; }
bool MeshLink::sendCommand(const uint8_t*, const uint8_t*, size_t) { return true; }
bool MeshLink::sendEvent(const uint8_t*, size_t) { return true; }

Compositor::Compositor()
    : wispShadePresence_(1.0f, g_shadeWisp), wispBasePresence_(1.0f, g_baseWisp) {
  context_.compositor = this;
}
void Compositor::addBehavior(AnimatedBehavior* b) {
  b->setBehaviorContext(&context_);
  g_added.push_back(b);
}
void Compositor::removeBehavior(AnimatedBehavior* b) {
  for (auto it = g_added.begin(); it != g_added.end(); ++it) {
    if (*it == b) { g_added.erase(it); return; }
  }
}
}  // namespace lamp

namespace {

constexpr uint8_t kShadePx = 38;
constexpr uint8_t kBasePx = 35;
constexpr ExpressionDescriptor kPerSurfacePulse = [] {
  ExpressionDescriptor d = withMake(kPulseDescriptorData, &makeExpr<PulseExpression>);
  d.id = "pulse2";
  d.spansSurfaces = false;
  return d;
}();
const uint8_t kSenderA[6] = {1, 1, 1, 1, 1, 1};
const uint8_t kSenderB[6] = {2, 2, 2, 2, 2, 2};

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  ExpressionManager manager;

  Rig() {
    g_added.clear();
    shade.pixelCount = kShadePx;
    shade.buffer.assign(kShadePx, Color{});
    base.pixelCount = kBasePx;
    base.buffer.assign(kBasePx, Color{});
    manager.registry().add(PulseExpression::classDescriptor());
    manager.registry().add(kPerSurfacePulse);
    manager.setCompositor(&compositor);
    manager.begin(&shade, &base);
  }

  size_t configured(const char* type, uint8_t target) {
    ExpressionConfig config;
    config.type = type;
    config.target = target;
    config.colors = {Color{200, 0, 0, 0}};
    manager.addExpression(config, /*configBacked=*/false);
    return manager.getBehaviors().size();
  }

  bool receive(const char* type, uint8_t target, const uint8_t* sender) {
    ExpressionInvocation inv;
    inv.type = type;
    inv.target = target;
    inv.colors = {Color{200, 0, 0, 0}};
    return manager.triggerInvocation(inv, sender);
  }
};

// Plays `b` to the end and reports whether it painted the shade.
bool paintsShade(Rig& rig, AnimatedBehavior* b) {
  bool lit = false;
  for (uint32_t t = 1016; t < 30000 && !lit; t += 16) {
    set_mock_millis(t);
    b->control();
    if (b->animationState == STOPPED) break;
    b->draw();
    for (const Color& c : rig.shade.buffer) lit = lit || c.r > 0;
  }
  return lit;
}

}  // namespace

void setUp() {}
void tearDown() {}

// The configured instance reaches the shade only through the buffer the
// manager handed it.
void test_configured_both_pulse_is_one_base_instance() {
  Rig rig;
  TEST_ASSERT_EQUAL_UINT32(1, rig.configured("pulse", TARGET_BOTH));
  AnimatedBehavior* b = rig.manager.getBehaviors()[0];
  TEST_ASSERT_EQUAL_PTR(&rig.base, b->fb);
  rig.compositor.addBehavior(b);
  set_mock_millis(1000);
  TEST_ASSERT_TRUE(rig.manager.triggerExpression("pulse"));
  TEST_ASSERT_TRUE(paintsShade(rig, b));
}

void test_configured_both_per_surface_type_is_two() {
  Rig rig;
  TEST_ASSERT_EQUAL_UINT32(2, rig.configured("pulse2", TARGET_BOTH));
}

void test_configured_invalid_target_builds_nothing() {
  Rig rig;
  TEST_ASSERT_EQUAL_UINT32(0, rig.configured("pulse", 0));
  TEST_ASSERT_EQUAL_UINT32(0, rig.configured("pulse", 7));
}

// The joined instance spans both surfaces: a received pulse lights the shade
// too, which it can only reach through the buffer the manager handed it.
void test_received_both_pulse_is_one_transient_on_both_surfaces() {
  Rig rig;
  set_mock_millis(1000);
  TEST_ASSERT_TRUE(rig.receive("pulse", TARGET_BOTH, kSenderA));
  TEST_ASSERT_EQUAL_UINT32(1, g_added.size());
  TEST_ASSERT_TRUE(paintsShade(rig, g_added[0]));
}

// Remote transients may fill two slots: two senders' both-surface pulses fit,
// where one per-surface pair fills them.
void test_received_both_pulse_takes_one_remote_slot() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.receive("pulse", TARGET_BOTH, kSenderA));
  TEST_ASSERT_TRUE(rig.receive("pulse", TARGET_BOTH, kSenderB));
  TEST_ASSERT_EQUAL_UINT32(2, g_added.size());

  Rig perSurface;
  TEST_ASSERT_TRUE(perSurface.receive("pulse2", TARGET_BOTH, kSenderA));
  TEST_ASSERT_FALSE(perSurface.receive("pulse2", TARGET_BOTH, kSenderB));
}

void test_received_both_pulse_coalesces_same_sender() {
  Rig rig;
  TEST_ASSERT_TRUE(rig.receive("pulse", TARGET_BOTH, kSenderA));
  TEST_ASSERT_FALSE(rig.receive("pulse", TARGET_BOTH, kSenderA));
  TEST_ASSERT_EQUAL_UINT32(1, g_added.size());
}

void test_received_invalid_target_builds_nothing() {
  Rig rig;
  TEST_ASSERT_FALSE(rig.receive("pulse", 0, kSenderA));
  TEST_ASSERT_TRUE(g_added.empty());
}

namespace {
class WispProbe : public Expression {
 public:
  using Expression::Expression;
  using Expression::wispDimScale;
  const ExpressionDescriptor& descriptor() const override { return d_; }
  void configureFromParameters(const ExpressionParams&) override {}
  float wispDimFloor() const override { return 0.5f; }

 protected:
  void onTrigger() override {}

 private:
  ExpressionDescriptor d_{};
};
}  // namespace

// Each surface reads its own wisp presence.
void test_wisp_weight_follows_the_surface() {
  g_baseWisp = 1.0f;
  g_shadeWisp = 0.0f;
  Rig rig;
  WispProbe probe(&rig.base);
  rig.compositor.addBehavior(&probe);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, probe.wispDimScale(&rig.base));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, probe.wispDimScale(&rig.shade));
  g_baseWisp = 0.0f;
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_configured_both_pulse_is_one_base_instance);
  RUN_TEST(test_configured_both_per_surface_type_is_two);
  RUN_TEST(test_configured_invalid_target_builds_nothing);
  RUN_TEST(test_received_both_pulse_is_one_transient_on_both_surfaces);
  RUN_TEST(test_received_both_pulse_takes_one_remote_slot);
  RUN_TEST(test_received_both_pulse_coalesces_same_sender);
  RUN_TEST(test_received_invalid_target_builds_nothing);
  RUN_TEST(test_wisp_weight_follows_the_surface);
  return UNITY_END();
}

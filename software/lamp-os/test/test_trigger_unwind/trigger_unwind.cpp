// Native pin for a std::bad_alloc thrown while an expression triggers,
// against the shipped ExpressionManager: the throw leaves no behavior the
// unwind freed in the compositor, and the cascade suppression it set is lifted,
// so a later fire still announces. The compositor and mesh are link stubs; the
// stub mesh records each MSG_EVENT payload.

#include <unity.h>

#include <new>
#include <string>

#include "core/animated_behavior.cpp"
#include "core/boot_history.cpp"
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

bool g_throw = false;

class Probe : public Expression {
 public:
  explicit Probe(FrameBuffer* fb) : Expression(fb, 600) {}
  const ExpressionDescriptor& descriptor() const override;
  void configureFromParameters(const ExpressionParams&) override {}
  void draw() override { nextFrame(); }

 protected:
  void onTrigger() override {}
};

// Throws from its trigger setup while g_throw is set, as an expression whose
// onTrigger allocates does when the heap is short.
class Thrower : public Probe {
 public:
  explicit Thrower(FrameBuffer* fb) : Probe(fb) {}
  const ExpressionDescriptor& descriptor() const override;

 protected:
  void onTrigger() override {
    if (g_throw) throw std::bad_alloc();
  }
};

constexpr ExpressionDescriptor kProbe{.id = "probe", .name = "Probe", .make = &makeExpr<Probe>};
const ExpressionDescriptor& Probe::descriptor() const { return kProbe; }
constexpr ExpressionDescriptor kThrower{
    .id = "thrower", .name = "Thrower", .make = &makeExpr<Thrower>};
const ExpressionDescriptor& Thrower::descriptor() const { return kThrower; }

const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};

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
    s_registered.clear();
    s_events.clear();
    manager.registry().add(kProbe);
    manager.registry().add(kThrower);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    manager.setMeshLink(&mesh);
  }
  AnimatedBehavior* configure(const char* type) {
    ExpressionConfig config;
    config.type = type;
    config.target = TARGET_SHADE;
    manager.addExpression(config, /*configBacked=*/false);
    AnimatedBehavior* b = manager.getBehaviors().back();
    compositor.addBehavior(b);
    return b;
  }
  // A configured probe's own fire, the auto-trigger path the suppression gates.
  size_t eventsFromAProbeFire(AnimatedBehavior* probe) {
    const size_t before = s_events.size();
    static_cast<Expression*>(probe)->trigger();
    return s_events.size() - before;
  }
};

template <class F>
bool throwsBadAlloc(F&& f) {
  try {
    f();
  } catch (const std::bad_alloc&) {
    return true;
  }
  return false;
}

}  // namespace

void setUp() {
  set_mock_millis(10000);
  g_throw = false;
}
void tearDown() { g_throw = false; }

void test_throwing_transient_leaves_no_freed_behavior_and_no_latch() {
  Rig r;
  AnimatedBehavior* probe = r.configure("probe");
  ExpressionInvocation inv;
  inv.type = "thrower";
  inv.target = TARGET_SHADE;
  g_throw = true;
  TEST_ASSERT_TRUE(throwsBadAlloc([&] { r.manager.triggerInvocation(inv, kPeerMac); }));
  g_throw = false;
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_TRUE(s_registered[0] == probe);
  TEST_ASSERT_EQUAL_size_t(1, r.eventsFromAProbeFire(probe));
}

void test_throwing_configured_trigger_lifts_the_suppression() {
  Rig r;
  r.configure("thrower");
  AnimatedBehavior* probe = r.configure("probe");
  g_throw = true;
  TEST_ASSERT_TRUE(throwsBadAlloc([&] { r.manager.triggerExpression("thrower"); }));
  g_throw = false;
  TEST_ASSERT_EQUAL_size_t(1, r.eventsFromAProbeFire(probe));

  set_mock_millis(millis() + 60000);
  g_throw = true;
  TEST_ASSERT_TRUE(throwsBadAlloc(
      [&] { r.manager.triggerExpression("thrower", TARGET_SHADE); }));
  g_throw = false;
  TEST_ASSERT_EQUAL_size_t(1, r.eventsFromAProbeFire(probe));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_throwing_transient_leaves_no_freed_behavior_and_no_latch);
  RUN_TEST(test_throwing_configured_trigger_lifts_the_suppression);
  return UNITY_END();
}

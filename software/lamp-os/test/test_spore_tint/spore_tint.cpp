// A mushroom's spore command against the shipped ExpressionManager: the burst
// plays at once, then the spore tint follows from the same sender once the
// burst ends, one tint at a time. Commands shaped like a lamp's own never
// book a tint. The compositor and mesh are link stubs; the delayed-trigger
// queue is captured here and fired the way lamp.cpp's loop fires it.

#include <unity.h>

#include "core/animated_behavior.cpp"
#include "expressions/expression.cpp"
#include "expressions/expression_manager.cpp"
#include "core/boot_history.cpp"
#include "expressions/recent_plays.cpp"
#include "expressions/cascade_scheduler.cpp"
#include "expressions/expression_invocation.cpp"
#include "expressions/expression_registry.cpp"
#include "util/heap_guard.cpp"
#include "components/network/mesh/lamp_roster.cpp"
#include "core/peer_view.cpp"
#include "core/frame_buffer.cpp"
#include "util/color.cpp"
#include "util/fade.cpp"
#include "expressions/bloom/bloom_expression.cpp"
#include "expressions/glitchy/glitchy_expression.cpp"
#include "expressions/shifty/shifty_expression.cpp"
#include "expressions/shimmer/shimmer_expression.cpp"
#include "expressions/spore_tint.cpp"
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
bool MeshLink::sendEvent(const uint8_t*, size_t) { return true; }
bool MeshLink::isOtaInProgress() const { return false; }
void MeshLink::getMyMac(uint8_t* mac) const { std::memset(mac, 0, 6); }
uint8_t Config::getDisposition(const uint8_t*) const { return 5; }

struct Queued {
  ExpressionInvocation inv;
  uint8_t srcMac[6];
  uint32_t delayMs;
};
std::vector<Queued> s_queued;
void enqueueDelayedInvocation(const ExpressionInvocation& inv, const uint8_t srcMac[6],
                              uint32_t delayMs) {
  Queued& q = s_queued.emplace_back();
  q.inv = inv;
  std::memcpy(q.srcMac, srcMac, 6);
  q.delayMs = delayMs;
}
}  // namespace lamp

using namespace lamp;

namespace {

const char kGolden[] =
    R"({"type":"flicker","target":2,"colors":"00ff8000","parameters":{"count":12,)"
    R"("durationMax":6000,"fire":0,"opacity":60,"fadeDuration":120}})";
const uint8_t kMushroom[6] = {0x02, 0, 0, 0, 0, 0x01};
const uint8_t kOtherMushroom[6] = {0x02, 0, 0, 0, 0, 0x02};
const Color kSporeColour(0x00, 0xff, 0x80, 0x00);
constexpr uint32_t kStepMs = 50;

ExpressionInvocation parse(const char* json) {
  JsonDocument doc;
  TEST_ASSERT_TRUE(deserializeJson(doc, json) == DeserializationError::Ok);
  ExpressionInvocation inv;
  TEST_ASSERT_TRUE(parseInvocation(doc.as<JsonObjectConst>(), inv));
  return inv;
}

// The beat a Fireflies Moment renders (moments.cpp renderBlink).
ExpressionInvocation firefliesBeat() {
  ExpressionInvocation inv;
  inv.type = "flicker";
  inv.target = TARGET_BASE;
  inv.colors = {Color(200, 40, 0, 0)};
  inv.parameters.set("fire", 0);
  inv.parameters.set("count", 6);
  inv.parameters.set("durationMax", 700);
  inv.parameters.set("opacity", 20);
  return inv;
}

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  ExpressionManager manager;
  SporeTint sporeTint;
  uint32_t now = 10000;
  Rig() {
    for (FrameBuffer* fb : {&shade, &base}) {
      fb->pixelCount = 24;
      fb->buffer.assign(24, Color(0, 0, 0, 100));
    }
    s_registered.clear();
    s_queued.clear();
    manager.registry().add(ShimmerExpression::classDescriptor());
    manager.registry().add(ShiftyExpression::classDescriptor());
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    set_mock_millis(now);
  }
  bool play(const ExpressionInvocation& inv, const uint8_t mac[6]) {
    return playReceivedCommand(manager, sporeTint, inv, mac);
  }
  // Runs the compositor's frames and the manager's reaping until `ms` later.
  void advance(uint32_t ms) {
    for (uint32_t end = now + ms; now < end;) {
      now += kStepMs;
      set_mock_millis(now);
      for (AnimatedBehavior* b : std::vector<AnimatedBehavior*>(s_registered)) {
        b->control();
        b->draw();
      }
      manager.gcTransients();
    }
  }
  size_t running(const char* type) const {
    size_t n = 0;
    for (AnimatedBehavior* b : s_registered) {
      auto* e = static_cast<Expression*>(b);
      if (std::strcmp(e->descriptor().id, type) == 0 && !e->isAnimationComplete()) ++n;
    }
    return n;
  }
};

}  // namespace

void setUp() {}
void tearDown() {}

void test_the_golden_spore_bursts_then_tints_from_its_sender() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(1, r.running("flicker"));
  TEST_ASSERT_EQUAL_size_t(1, s_queued.size());
  const Queued q = s_queued[0];
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kMushroom, q.srcMac, 6);
  TEST_ASSERT_EQUAL_STRING("shifty", q.inv.type.c_str());
  TEST_ASSERT_EQUAL_UINT8(TARGET_BASE, q.inv.target);
  TEST_ASSERT_EQUAL_size_t(1, q.inv.colors.size());
  TEST_ASSERT_TRUE(q.inv.colors[0] == kSporeColour);
  TEST_ASSERT_EQUAL_UINT32(SporeTint::kOpacityPct, getParam(q.inv.parameters, "opacity"));

  // The tint waits for the burst: still sparkling just before, done at it.
  TEST_ASSERT_EQUAL_UINT32(6000, q.delayMs);
  r.advance(q.delayMs - kStepMs);
  TEST_ASSERT_EQUAL_size_t(1, r.running("flicker"));
  r.advance(2 * kStepMs);
  TEST_ASSERT_EQUAL_size_t(0, r.running("flicker"));

  // Fired as lamp.cpp's loop fires a queued invocation: a peer's, keyed by the sender.
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(q.inv, q.srcMac));
  TEST_ASSERT_EQUAL_size_t(1, r.running("shifty"));
  for (AnimatedBehavior* b : s_registered) {
    auto* e = static_cast<Expression*>(b);
    if (std::strcmp(e->descriptor().id, "shifty") != 0) continue;
    TEST_ASSERT_EQUAL_PTR(&r.base, e->fb);
    TEST_ASSERT_TRUE(e->isTransient);
  }
  // The same sender's tint is in flight, so a copy of it coalesces.
  TEST_ASSERT_FALSE(r.manager.triggerInvocation(q.inv, q.srcMac));
}

// The tint completes before the transient lifetime cap would reap it, even at
// its longest fade back, and fires no glitchy.
void test_the_tint_completes_inside_the_lifetime_cap() {
  Rig r;
  ExpressionInvocation spore = parse(kGolden);
  spore.parameters.set("fadeDuration", 600);
  TEST_ASSERT_TRUE(r.play(spore, kMushroom));
  const Queued q = s_queued[0];
  TEST_ASSERT_TRUE(getParam(q.inv.parameters, "durationMax") <= SporeTint::kMaxFadeBackS);
  r.advance(q.delayMs);
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(q.inv, q.srcMac));
  const uint32_t start = r.now;
  while (r.running("shifty") > 0) {
    r.advance(1000);
    TEST_ASSERT_TRUE_MESSAGE(r.now - start < TransientSlots::kMaxLifetimeMs, "tint reaped");
  }
  r.advance(1000);
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

void test_a_second_spore_inside_the_tint_books_no_second_tint() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(1, s_queued.size());
  const Queued first = s_queued[0];
  r.advance(first.delayMs + kStepMs);
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(first.inv, first.srcMac));

  // Mid-tint, another mushroom and the same one again: both sparkle, neither tints.
  r.advance(20000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(7000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(1, s_queued.size());

  // Once the first tint is over, the next spore tints again.
  while (r.running("shifty") > 0) r.advance(1000);
  r.advance(10000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  TEST_ASSERT_EQUAL_size_t(2, s_queued.size());
}

// A burst that does not start (the same sender's burst still in flight, or
// home mode refusing peers) books no tint.
void test_a_spore_that_does_not_play_books_no_tint() {
  Rig fresh;
  TEST_ASSERT_TRUE(fresh.play(parse(kGolden), kMushroom));
  fresh.advance(1000);
  s_queued.clear();
  TEST_ASSERT_FALSE(fresh.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(0, s_queued.size());

  Rig home;
  home.compositor.homeMode = true;
  TEST_ASSERT_FALSE(home.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(0, s_queued.size());
}

// Lamps never send a flicker burst carrying both a colour and fadeDuration, so
// none of these shapes tints: a Fireflies beat, an untinted spore, a tinted
// flicker with fadeDuration but no burst, and another type with the keys.
void test_lamp_shaped_commands_book_no_tint() {
  ExpressionInvocation untinted = parse(kGolden);
  untinted.colors.clear();
  ExpressionInvocation noBurst = parse(kGolden);
  noBurst.parameters.erase("count");
  ExpressionInvocation otherType = parse(kGolden);
  otherType.type = "shifty";
  for (const ExpressionInvocation& inv : {firefliesBeat(), untinted, noBurst, otherType}) {
    Rig r;
    TEST_ASSERT_TRUE(r.play(inv, kMushroom));
    TEST_ASSERT_EQUAL_size_t(0, s_queued.size());
  }
}

void test_the_busy_window_survives_the_millis_wrap() {
  SporeTint t;
  ExpressionInvocation tint;
  uint32_t afterMs = 0;
  const ExpressionInvocation spore = parse(kGolden);
  const uint32_t start = UINT32_MAX - 1000;
  TEST_ASSERT_TRUE(t.follow(spore, start, tint, afterMs));
  TEST_ASSERT_FALSE(t.follow(spore, start + 5000, tint, afterMs));
  TEST_ASSERT_FALSE(t.follow(spore, start + 60000, tint, afterMs));
  TEST_ASSERT_TRUE(t.follow(spore, start + TransientSlots::kMaxLifetimeMs + 10000, tint, afterMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_golden_spore_bursts_then_tints_from_its_sender);
  RUN_TEST(test_the_tint_completes_inside_the_lifetime_cap);
  RUN_TEST(test_a_second_spore_inside_the_tint_books_no_second_tint);
  RUN_TEST(test_a_spore_that_does_not_play_books_no_tint);
  RUN_TEST(test_lamp_shaped_commands_book_no_tint);
  RUN_TEST(test_the_busy_window_survives_the_millis_wrap);
  return UNITY_END();
}

// A mushroom's spore command against the shipped ExpressionManager: the burst
// plays at once, then the spore tint follows from the same sender once the
// burst ends, one tint at a time. Commands shaped like a lamp's own never
// book a tint. The compositor and mesh are link stubs; the
// loop runs in lamp.cpp's order: commands drain, the frame is drawn, finished
// transients are reaped, then the spore tint ticks.

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
  // Each tint that started: when, and how many flicker bursts still ran then.
  std::vector<uint32_t> tintStartsMs;
  std::vector<size_t> burstsAtTintStart;

  Rig() {
    for (FrameBuffer* fb : {&shade, &base}) {
      fb->pixelCount = 24;
      fb->buffer.assign(24, Color(0, 0, 0, 100));
    }
    s_registered.clear();
    manager.registry().add(ShimmerExpression::classDescriptor());
    manager.registry().add(ShiftyExpression::classDescriptor());
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
    set_mock_millis(now);
  }
  bool play(const ExpressionInvocation& inv, const uint8_t mac[6]) {
    return playReceivedCommand(manager, sporeTint, inv, mac);
  }
  void tick() {
    const size_t tintsBefore = running("shifty");
    const size_t bursts = running("flicker");
    sporeTint.tick(manager);
    if (running("shifty") > tintsBefore) {
      tintStartsMs.push_back(now);
      burstsAtTintStart.push_back(bursts);
    }
  }
  void pass() {
    now += kStepMs;
    set_mock_millis(now);
    for (AnimatedBehavior* b : std::vector<AnimatedBehavior*>(s_registered)) {
      b->control();
      b->draw();
    }
    manager.gcTransients();
    tick();
  }
  void advance(uint32_t ms) {
    for (uint32_t end = now + ms; static_cast<int32_t>(now - end) < 0;) pass();
  }
  size_t running(const char* type) const {
    size_t n = 0;
    for (AnimatedBehavior* b : s_registered) {
      auto* e = static_cast<Expression*>(b);
      if (std::strcmp(e->descriptor().id, type) == 0 && !e->isAnimationComplete()) ++n;
    }
    return n;
  }
  Expression* tintOnScreen() const {
    for (AnimatedBehavior* b : s_registered) {
      auto* e = static_cast<Expression*>(b);
      if (std::strcmp(e->descriptor().id, "shifty") == 0) return e;
    }
    return nullptr;
  }
};

// The spore's burst length, after which its tint starts.
uint32_t tintAfterMs(const ExpressionInvocation& burst) {
  return getParam(burst.parameters, "durationMax");
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_the_golden_spore_names_its_tint() {
  ExpressionInvocation tint;
  TEST_ASSERT_TRUE(SporeTint::tintFor(parse(kGolden), tint));
  TEST_ASSERT_EQUAL_STRING("shifty", tint.type.c_str());
  TEST_ASSERT_EQUAL_UINT8(TARGET_BASE, tint.target);
  TEST_ASSERT_EQUAL_size_t(1, tint.colors.size());
  TEST_ASSERT_TRUE(tint.colors[0] == kSporeColour);
  TEST_ASSERT_EQUAL_UINT32(kShiftyFillBloom, getParam(tint.parameters, "fillMode"));
  TEST_ASSERT_EQUAL_UINT32(SporeTint::kFadeS, getParam(tint.parameters, "fadeDuration"));
  TEST_ASSERT_EQUAL_UINT32(SporeTint::kHoldS, getParam(tint.parameters, "shiftDurationMin"));
  TEST_ASSERT_EQUAL_UINT32(SporeTint::kHoldS, getParam(tint.parameters, "shiftDurationMax"));
  TEST_ASSERT_EQUAL_UINT32(SporeTint::kOpacityPct, getParam(tint.parameters, "opacity"));
}

// The fade back follows the spore's fadeDuration up to the cap.
void test_the_tint_fades_back_over_the_spores_fade_duration() {
  for (uint32_t asked : {1u, 60u, SporeTint::kMaxFadeBackS, SporeTint::kMaxFadeBackS + 1, 600u}) {
    ExpressionInvocation spore = parse(kGolden);
    spore.parameters.set("fadeDuration", asked);
    ExpressionInvocation tint;
    TEST_ASSERT_TRUE(SporeTint::tintFor(spore, tint));
    TEST_ASSERT_EQUAL_UINT32(std::min(asked, SporeTint::kMaxFadeBackS),
                             getParam(tint.parameters, "durationMax"));
  }
}

void test_the_golden_spore_bursts_then_tints_from_its_sender() {
  Rig r;
  const uint32_t start = r.now;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  TEST_ASSERT_EQUAL_size_t(1, r.running("flicker"));
  r.advance(tintAfterMs(parse(kGolden)) + 2 * kStepMs);

  // One tint, started once the 6 s burst was over and reaped, within a second.
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
  TEST_ASSERT_EQUAL_size_t(0, r.burstsAtTintStart[0]);
  TEST_ASSERT_TRUE(r.tintStartsMs[0] - start >= 6000 && r.tintStartsMs[0] - start < 7000);
  Expression* tint = r.tintOnScreen();
  TEST_ASSERT_NOT_NULL(tint);
  TEST_ASSERT_EQUAL_PTR(&r.base, tint->fb);
  TEST_ASSERT_TRUE(tint->isTransient);
  TEST_ASSERT_EQUAL_size_t(1, tint->getColors().size());
  TEST_ASSERT_TRUE(tint->getColors()[0] == kSporeColour);
  // From the spore's sender: a peer's shifty from that MAC coalesces with it.
  ExpressionInvocation same;
  same.type = "shifty";
  same.target = TARGET_BASE;
  TEST_ASSERT_FALSE(r.manager.triggerInvocation(same, kMushroom));
}

// With another peer's transient holding the second Remote slot, the tint
// still starts: its own burst has given up the first.
void test_the_tint_starts_beside_another_peers_transient() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(1000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(6000);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
  TEST_ASSERT_EQUAL_size_t(1, r.running("shifty"));
}

// The tint completes before the transient lifetime cap would reap it, even at
// its longest fade back.
void test_the_tint_completes_inside_the_lifetime_cap() {
  Rig r;
  ExpressionInvocation spore = parse(kGolden);
  spore.parameters.set("fadeDuration", 600);
  TEST_ASSERT_TRUE(r.play(spore, kMushroom));
  r.advance(tintAfterMs(spore) + kStepMs);
  TEST_ASSERT_EQUAL_size_t(1, r.running("shifty"));
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
  r.advance(tintAfterMs(parse(kGolden)) + kStepMs);
  TEST_ASSERT_EQUAL_size_t(1, r.running("shifty"));

  // Mid-tint, another mushroom and the same one again: both sparkle, neither tints.
  r.advance(20000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(7000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(60000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(10000);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());

  // Once the first tint is over, the next spore tints again.
  while (r.running("shifty") > 0) r.advance(1000);
  r.advance(10000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(tintAfterMs(parse(kGolden)) + kStepMs);
  TEST_ASSERT_EQUAL_size_t(2, r.tintStartsMs.size());
}

// A second spore while the first's tint waits for its burst books none.
void test_a_second_spore_while_a_tint_waits_books_none() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(5000);
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(20000);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
  ExpressionInvocation same;
  same.type = "shifty";
  same.target = TARGET_BASE;
  TEST_ASSERT_FALSE(r.manager.triggerInvocation(same, kMushroom));
}

// The loop stalls past the tint's start time and a second spore drains on the
// pass that ends the stall: the booked tint still starts alone.
void test_a_stalled_loop_still_starts_one_tint() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(3000);
  r.now += 5000;
  set_mock_millis(r.now);
  const uint32_t stallEnd = r.now;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(30000);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
  // The first spore's tint, started on the passes right after the stall.
  TEST_ASSERT_TRUE(r.tintStartsMs[0] - stallEnd <= 2 * kStepMs);
  ExpressionInvocation same;
  same.type = "shifty";
  same.target = TARGET_BASE;
  TEST_ASSERT_FALSE(r.manager.triggerInvocation(same, kMushroom));
}

// A tint refused when its time comes (home mode refusing peers) is dropped and
// holds off nothing: the next spore tints.
void test_a_refused_tint_does_not_block_the_next() {
  Rig r;
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(tintAfterMs(parse(kGolden)) - 1000);
  r.compositor.homeMode = true;
  r.advance(2000);
  r.compositor.homeMode = false;
  r.advance(1000);
  TEST_ASSERT_EQUAL_size_t(0, r.tintStartsMs.size());
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kOtherMushroom));
  r.advance(tintAfterMs(parse(kGolden)) + kStepMs);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
}

// A burst that does not start (the same sender's burst still in flight, or
// home mode refusing peers) books no tint.
void test_a_spore_that_does_not_play_books_no_tint() {
  Rig busy;
  TEST_ASSERT_TRUE(busy.play(parse(kGolden), kMushroom));
  busy.sporeTint = SporeTint{};
  busy.advance(1000);
  TEST_ASSERT_FALSE(busy.play(parse(kGolden), kMushroom));
  busy.advance(20000);
  TEST_ASSERT_EQUAL_size_t(0, busy.tintStartsMs.size());

  Rig home;
  home.compositor.homeMode = true;
  TEST_ASSERT_FALSE(home.play(parse(kGolden), kMushroom));
  home.compositor.homeMode = false;
  home.advance(20000);
  TEST_ASSERT_EQUAL_size_t(0, home.tintStartsMs.size());
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
    ExpressionInvocation tint;
    TEST_ASSERT_FALSE(SporeTint::tintFor(inv, tint));
    Rig r;
    TEST_ASSERT_TRUE(r.play(inv, kMushroom));
    const size_t shiftyAtPlay = r.running("shifty");
    r.advance(20000);
    TEST_ASSERT_EQUAL_size_t(0, r.tintStartsMs.size());
    TEST_ASSERT_TRUE(r.running("shifty") <= shiftyAtPlay);
  }
}

// Only a peer's shifty counts as a tint playing: a peer's flicker or the
// lamp's own shifty (an app Test) does not hold off the next spore's tint.
void test_only_a_peers_shifty_holds_off_a_tint() {
  Rig r;
  ExpressionInvocation peerFlicker = parse(kGolden);
  peerFlicker.colors.clear();
  TEST_ASSERT_TRUE(r.play(peerFlicker, kOtherMushroom));
  ExpressionInvocation ownShifty;
  ownShifty.type = "shifty";
  ownShifty.target = TARGET_SHADE;
  ownShifty.colors = {kSporeColour};
  const uint8_t kOwn[6] = {};
  TEST_ASSERT_TRUE(r.manager.triggerInvocation(ownShifty, kOwn, false, TransientSource::Local));
  TEST_ASSERT_TRUE(r.play(parse(kGolden), kMushroom));
  r.advance(tintAfterMs(parse(kGolden)) + kStepMs);
  TEST_ASSERT_EQUAL_size_t(1, r.tintStartsMs.size());
}

void test_a_playing_tint_blocks_a_booking() {
  SporeTint t;
  TEST_ASSERT_FALSE(t.follow(parse(kGolden), kMushroom, true));
  TEST_ASSERT_TRUE(t.follow(parse(kGolden), kMushroom, false));
  TEST_ASSERT_FALSE(t.follow(parse(kGolden), kOtherMushroom, false));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_the_golden_spore_names_its_tint);
  RUN_TEST(test_the_tint_fades_back_over_the_spores_fade_duration);
  RUN_TEST(test_the_golden_spore_bursts_then_tints_from_its_sender);
  RUN_TEST(test_the_tint_starts_beside_another_peers_transient);
  RUN_TEST(test_the_tint_completes_inside_the_lifetime_cap);
  RUN_TEST(test_a_second_spore_inside_the_tint_books_no_second_tint);
  RUN_TEST(test_a_second_spore_while_a_tint_waits_books_none);
  RUN_TEST(test_a_stalled_loop_still_starts_one_tint);
  RUN_TEST(test_a_refused_tint_does_not_block_the_next);
  RUN_TEST(test_a_spore_that_does_not_play_books_no_tint);
  RUN_TEST(test_lamp_shaped_commands_book_no_tint);
  RUN_TEST(test_only_a_peers_shifty_holds_off_a_tint);
  RUN_TEST(test_a_playing_tint_blocks_a_booking);
  return UNITY_END();
}

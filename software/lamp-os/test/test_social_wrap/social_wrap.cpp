// Native pins that SocialBehavior's greeting cooldowns, re-greet window and
// introvert fatigue hold past the 49.7-day millis() wrap. Links the shipped
// social.cpp; its personality and mesh dependencies are link stubs.

#include <unity.h>

#include "../../src/core/animated_behavior.cpp"
#include "../../src/behaviors/social.cpp"
#include "../../src/components/network/mesh/lamp_roster.cpp"
#include "../../src/core/peer_view.cpp"
#include "../../src/util/color.cpp"
#include "../../src/util/fade.cpp"
#include "../../src/util/gradient.cpp"
#include "../../src/core/frame_buffer.cpp"
#include "../../src/behaviors/social_reunion.cpp"
#include "../../src/behaviors/copycat.cpp"
#include "../../src/behaviors/roll_cooldown.cpp"
#include "../../src/config/disposition_store.cpp"
#include "../../src/util/heap_guard.cpp"

namespace lamp {
OverrideAggregate overrides;
PersonalityEngine personalityEngine;
GreetingTuning PersonalityEngine::greetingFor(const uint8_t*) const { return {}; }
bool MeshLink::sendColorQuery(const uint8_t*) { return true; }
const std::vector<NearbyCopy>& PersonalityEngine::nearbyPeers() const {
  static const std::vector<NearbyCopy> none;
  return none;
}
bool ExpressionManager::triggerInvocation(const ExpressionInvocation&, const uint8_t*, bool,
                                          TransientSource) {
  return false;
}
bool BehaviorContext::ignoresPeers() const { return false; }
}  // namespace lamp

using namespace lamp;

namespace {
constexpr uint64_t kWrapMs = 1ull << 32;
const char* const kPeerBle = "AA:BB:CC:DD:EE:F0";

// Sets the mock clock to `uptime`; millis() reads its low 32 bits.
void at(uint64_t uptime) {
  g_mock_millis_wraps = static_cast<uint32_t>(uptime >> 32);
  set_mock_millis(static_cast<uint32_t>(uptime));
}

// One control() pass with the peer freshly sighted nearby; true when it
// starts a greeting.
bool greetsAt(SocialBehavior& social, uint64_t uptime, const char* ble = kPeerBle) {
  at(uptime);
  lampRoster.addOrUpdateFromBle("near", ble, Color(), Color(), -50);
  social.animationState = STOPPED;
  social.control();
  return social.animationState != STOPPED;
}
}  // namespace

void setUp() {
  lampRoster = LampRoster();
  at(0);
}
void tearDown() { at(0); }

// A lamp that has greeted no one for its first 24.8 days still greets the
// first peer it meets after that.
void test_first_greeting_after_half_a_wrap() {
  BehaviorContext ctx;
  SocialBehavior social(nullptr, 1200);
  social.setBehaviorContext(&ctx);
  TEST_ASSERT_TRUE(greetsAt(social, (1ull << 31) + 1000));
}

// A peer greeted once is greeted again a whole wrap later, when its re-greet
// stamp would read as 40 s old.
void test_regreet_a_wrap_later() {
  BehaviorContext ctx;
  SocialBehavior social(nullptr, 1200);
  social.setBehaviorContext(&ctx);
  TEST_ASSERT_TRUE(greetsAt(social, 100000));
  lampRoster = LampRoster();  // the peer leaves and returns unacknowledged
  TEST_ASSERT_TRUE(greetsAt(social, 100000 + kWrapMs + 40000));
}

// An introvert that never tired still greets after half a wrap.
void test_introvert_greets_after_half_a_wrap() {
  Config cfg;
  cfg.lamp.socialMode = SocialMode::Introvert;
  BehaviorContext ctx;
  SocialBehavior social(nullptr, 1200);
  social.setConfig(&cfg);
  social.setBehaviorContext(&ctx);
  TEST_ASSERT_TRUE(greetsAt(social, (1ull << 31) + 1000));
}

// Two greetings, then one a whole wrap later: the old two must not count
// toward the introvert's fatigue, so a fourth peer is still greeted.
void test_introvert_fatigue_forgets_greetings_a_wrap_old() {
  Config cfg;
  cfg.lamp.socialMode = SocialMode::Introvert;
  BehaviorContext ctx;
  SocialBehavior social(nullptr, 1200);
  social.setConfig(&cfg);
  social.setBehaviorContext(&ctx);
  const uint64_t t0 = 100000;
  TEST_ASSERT_TRUE(greetsAt(social, t0, "AA:BB:CC:DD:EE:01"));
  TEST_ASSERT_TRUE(greetsAt(social, t0 + 70000, "AA:BB:CC:DD:EE:02"));
  TEST_ASSERT_TRUE(greetsAt(social, t0 + kWrapMs + 140000, "AA:BB:CC:DD:EE:03"));
  TEST_ASSERT_TRUE(greetsAt(social, t0 + kWrapMs + 210000, "AA:BB:CC:DD:EE:04"));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_greeting_after_half_a_wrap);
  RUN_TEST(test_regreet_a_wrap_later);
  RUN_TEST(test_introvert_greets_after_half_a_wrap);
  RUN_TEST(test_introvert_fatigue_forgets_greetings_a_wrap_old);
  return UNITY_END();
}

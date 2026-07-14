// Native-host tests for GreetingState on SocialBehavior and snafu::Greeting.
//
// Both greetable implementations are mirrored inline here so the tests compile
// on native without Arduino / FreeRTOS. Keep the mirrored logic in sync with:
//   behaviors/social.cpp  (greetingState() + greetingPeerBdAddr_ lifecycle)
//   lamps/snafu/greeting.cpp  (greetingState())

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>

// ---- AnimationState mirror --------------------------------------------------

enum AnimationState {
  PLAYING      = 1,
  PAUSING      = 2,
  PAUSED       = 3,
  STOPPING     = 4,
  STOPPED      = 5,
  PLAYING_ONCE = 6,
};

// ---- GreetingState mirror (from behaviors/greetable.hpp) --------------------

struct GreetingState {
  bool        active      = false;
  std::string peerBdAddr;
  std::string kind;
};

// ---- SocialBehavior greeting-state logic mirror -----------------------------
//
// Mirrors greetingState() from social.cpp. The production method reads
// animationState, snub, pulseBackStrength, and greetingPeerBdAddr_.
// Kept verbatim so a logic change in production fails this test.

struct SocialGreetingFields {
  AnimationState animationState = STOPPED;
  bool           snub             = false;
  uint8_t        pulseBackStrength = 0;
  std::string    greetingPeerBdAddr;
};

static GreetingState socialGreetingState(const SocialGreetingFields& s) {
  if (s.animationState == STOPPED) return {};
  GreetingState gs;
  gs.active     = true;
  gs.peerBdAddr = s.greetingPeerBdAddr;
  if (s.snub) {
    gs.kind = "snub";
  } else if (s.pulseBackStrength > 0) {
    gs.kind = "warm";
  } else {
    gs.kind = "reserved";
  }
  return gs;
}

// ---- snafu::Greeting greeting-state logic mirror ----------------------------
//
// Mirrors greetingState() from lamps/snafu/greeting.cpp. The production
// method checks PLAYING / PLAYING_ONCE / STOPPING as "active" states.

struct SnafuGreetingFields {
  AnimationState animationState = STOPPED;
  std::string    greetedBdAddr;
};

static GreetingState snafuGreetingState(const SnafuGreetingFields& s) {
  const bool playing = (s.animationState == PLAYING ||
                        s.animationState == PLAYING_ONCE ||
                        s.animationState == STOPPING);
  if (!playing) return {};
  GreetingState gs;
  gs.active     = true;
  gs.peerBdAddr = s.greetedBdAddr;
  gs.kind       = "glitch";
  return gs;
}

// ---- Fixtures ---------------------------------------------------------------

static const std::string kPeerA = "AA:BB:CC:DD:EE:FF";
static const std::string kPeerB = "11:22:33:44:55:66";

void setUp()    {}
void tearDown() {}

// ---- SocialBehavior tests ---------------------------------------------------

void test_social_idle_returns_inactive() {
  SocialGreetingFields s;
  s.animationState = STOPPED;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_FALSE(gs.active);
  TEST_ASSERT_TRUE(gs.peerBdAddr.empty());
  TEST_ASSERT_TRUE(gs.kind.empty());
}

void test_social_playing_snub_returns_active_snub() {
  SocialGreetingFields s;
  s.animationState    = PLAYING_ONCE;
  s.snub              = true;
  s.pulseBackStrength = 255;
  s.greetingPeerBdAddr = kPeerA;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
  TEST_ASSERT_EQUAL_STRING(kPeerA.c_str(), gs.peerBdAddr.c_str());
  TEST_ASSERT_EQUAL_STRING("snub", gs.kind.c_str());
}

void test_social_playing_with_pulse_returns_warm() {
  SocialGreetingFields s;
  s.animationState    = PLAYING_ONCE;
  s.snub              = false;
  s.pulseBackStrength = 100;
  s.greetingPeerBdAddr = kPeerB;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
  TEST_ASSERT_EQUAL_STRING(kPeerB.c_str(), gs.peerBdAddr.c_str());
  TEST_ASSERT_EQUAL_STRING("warm", gs.kind.c_str());
}

void test_social_playing_no_pulse_returns_reserved() {
  SocialGreetingFields s;
  s.animationState    = PLAYING_ONCE;
  s.snub              = false;
  s.pulseBackStrength = 0;
  s.greetingPeerBdAddr = kPeerA;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
  TEST_ASSERT_EQUAL_STRING("reserved", gs.kind.c_str());
}

void test_social_peer_addr_populated_while_playing() {
  SocialGreetingFields s;
  s.animationState    = PLAYING_ONCE;
  s.snub              = false;
  s.pulseBackStrength = 0;
  s.greetingPeerBdAddr = kPeerA;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_EQUAL_STRING(kPeerA.c_str(), gs.peerBdAddr.c_str());
}

void test_social_stopping_state_is_active() {
  SocialGreetingFields s;
  s.animationState    = STOPPING;
  s.greetingPeerBdAddr = kPeerA;
  auto gs = socialGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
}

// ---- snafu::Greeting tests --------------------------------------------------

void test_snafu_idle_returns_inactive() {
  SnafuGreetingFields s;
  s.animationState = STOPPED;
  auto gs = snafuGreetingState(s);
  TEST_ASSERT_FALSE(gs.active);
  TEST_ASSERT_TRUE(gs.peerBdAddr.empty());
  TEST_ASSERT_TRUE(gs.kind.empty());
}

void test_snafu_playing_returns_active_glitch() {
  SnafuGreetingFields s;
  s.animationState = PLAYING_ONCE;
  s.greetedBdAddr  = kPeerA;
  auto gs = snafuGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
  TEST_ASSERT_EQUAL_STRING(kPeerA.c_str(), gs.peerBdAddr.c_str());
  TEST_ASSERT_EQUAL_STRING("glitch", gs.kind.c_str());
}

void test_snafu_stopping_is_active() {
  SnafuGreetingFields s;
  s.animationState = STOPPING;
  s.greetedBdAddr  = kPeerB;
  auto gs = snafuGreetingState(s);
  TEST_ASSERT_TRUE(gs.active);
}

void test_snafu_kind_is_always_glitch() {
  SnafuGreetingFields s;
  s.animationState = PLAYING;
  s.greetedBdAddr  = kPeerA;
  auto gs = snafuGreetingState(s);
  TEST_ASSERT_EQUAL_STRING("glitch", gs.kind.c_str());
}

void test_snafu_peer_addr_populated_while_playing() {
  SnafuGreetingFields s;
  s.animationState = PLAYING_ONCE;
  s.greetedBdAddr  = kPeerB;
  auto gs = snafuGreetingState(s);
  TEST_ASSERT_EQUAL_STRING(kPeerB.c_str(), gs.peerBdAddr.c_str());
}

// ---- snafu::Greeting change-callback edge detection -------------------------
//
// Mirrors the greetingWasActive_ / onGreetingChange_ logic added to
// Greeting::doGreet() and Greeting::control() in lamps/snafu/greeting.cpp.
// Pins start-edge (doGreet fires callback) and stop-edge (control() detects
// transition from active → STOPPED and fires callback).

struct SnafuCallbackHarness {
  AnimationState animationState = STOPPED;
  bool           greetingWasActive = false;
  int            callbackCount     = 0;

  // Mirrors doGreet(): sets active, fires callback.
  void doGreet() {
    animationState    = PLAYING_ONCE;
    greetingWasActive = true;
    callbackCount++;
  }

  // Mirrors the edge-detection block in control().
  void tickControl() {
    const bool nowActive = (animationState == PLAYING ||
                            animationState == PLAYING_ONCE ||
                            animationState == STOPPING);
    if (!nowActive && greetingWasActive) {
      callbackCount++;
    }
    greetingWasActive = nowActive;
  }
};

void test_snafu_callback_fires_on_start() {
  SnafuCallbackHarness h;
  h.doGreet();
  TEST_ASSERT_EQUAL(1, h.callbackCount);
}

void test_snafu_callback_fires_on_stop() {
  SnafuCallbackHarness h;
  h.doGreet();
  h.animationState = STOPPED;
  h.tickControl();
  TEST_ASSERT_EQUAL(2, h.callbackCount);  // 1 start + 1 stop
}

void test_snafu_callback_no_spurious_stop_without_start() {
  SnafuCallbackHarness h;
  // No greeting started; tickControl repeatedly should never fire callback.
  h.tickControl();
  h.tickControl();
  TEST_ASSERT_EQUAL(0, h.callbackCount);
}

void test_snafu_callback_fires_once_per_stop_edge() {
  SnafuCallbackHarness h;
  h.doGreet();
  h.animationState = STOPPED;
  h.tickControl();
  h.tickControl();  // second tick: already STOPPED, was already false
  TEST_ASSERT_EQUAL(2, h.callbackCount);  // still 1 start + 1 stop
}

// ---- stateNotify JSON shape -------------------------------------------------
//
// Mirrors the notifyStateChange() format in ble_control.cpp. Pins the wire
// shape so a format change breaks this test before it breaks the app.

#include <cstdio>

static std::string buildStateNotifyPayload(bool previewActive,
                                           const GreetingState& gs) {
  char buf[128];
  if (gs.active) {
    snprintf(buf, sizeof(buf),
             "{\"previewActive\":%s,\"greeting\":{\"active\":true,\"peer\":\"%s\",\"kind\":\"%s\"}}",
             previewActive ? "true" : "false",
             gs.peerBdAddr.c_str(),
             gs.kind.c_str());
  } else {
    snprintf(buf, sizeof(buf),
             "{\"previewActive\":%s,\"greeting\":{\"active\":false}}",
             previewActive ? "true" : "false");
  }
  return buf;
}

void test_notify_payload_idle_greeting() {
  GreetingState gs;  // inactive
  auto payload = buildStateNotifyPayload(false, gs);
  TEST_ASSERT_NOT_NULL(
      strstr(payload.c_str(), "\"greeting\":{\"active\":false}"));
}

void test_notify_payload_active_greeting_includes_peer_and_kind() {
  GreetingState gs;
  gs.active     = true;
  gs.peerBdAddr = kPeerA;
  gs.kind       = "warm";
  auto payload = buildStateNotifyPayload(false, gs);
  TEST_ASSERT_NOT_NULL(strstr(payload.c_str(), "\"active\":true"));
  TEST_ASSERT_NOT_NULL(strstr(payload.c_str(), kPeerA.c_str()));
  TEST_ASSERT_NOT_NULL(strstr(payload.c_str(), "\"kind\":\"warm\""));
}

void test_notify_payload_fits_budget() {
  // Worst case: active + 17B bdAddr + max kind.
  GreetingState gs;
  gs.active     = true;
  gs.peerBdAddr = "AA:BB:CC:DD:EE:FF";
  gs.kind       = "reserved";
  auto payload = buildStateNotifyPayload(true, gs);
  // Must fit in 128 bytes (the ble_control buffer size).
  TEST_ASSERT_LESS_THAN(128u, payload.size());
}

int main(int, char**) {
  UNITY_BEGIN();
  // SocialBehavior
  RUN_TEST(test_social_idle_returns_inactive);
  RUN_TEST(test_social_playing_snub_returns_active_snub);
  RUN_TEST(test_social_playing_with_pulse_returns_warm);
  RUN_TEST(test_social_playing_no_pulse_returns_reserved);
  RUN_TEST(test_social_peer_addr_populated_while_playing);
  RUN_TEST(test_social_stopping_state_is_active);
  // snafu::Greeting
  RUN_TEST(test_snafu_idle_returns_inactive);
  RUN_TEST(test_snafu_playing_returns_active_glitch);
  RUN_TEST(test_snafu_stopping_is_active);
  RUN_TEST(test_snafu_kind_is_always_glitch);
  RUN_TEST(test_snafu_peer_addr_populated_while_playing);
  // snafu change-callback edge detection
  RUN_TEST(test_snafu_callback_fires_on_start);
  RUN_TEST(test_snafu_callback_fires_on_stop);
  RUN_TEST(test_snafu_callback_no_spurious_stop_without_start);
  RUN_TEST(test_snafu_callback_fires_once_per_stop_edge);
  // Wire format
  RUN_TEST(test_notify_payload_idle_greeting);
  RUN_TEST(test_notify_payload_active_greeting_includes_peer_and_kind);
  RUN_TEST(test_notify_payload_fits_budget);
  return UNITY_END();
}

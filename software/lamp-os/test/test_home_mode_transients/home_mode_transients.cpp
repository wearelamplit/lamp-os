// Native pin for the transient side of home mode's social-off, against the
// shipped ExpressionManager: while home mode is active with social off, a
// peer's invocation (cascade, delayed cascade, event mirror) never starts,
// the lamp's own invocation still does, and a peer transient already running
// fades out and is reaped. The compositor and mesh are link stubs; the home
// gate the manager consults is the compositor's real inline blocksRemote.

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
#include "expressions/bloom/bloom_expression.cpp"
#include "expressions/glitchy/glitchy_expression.cpp"
#include "behaviors/social_echo.cpp"
#include "behaviors/copycat.cpp"
#include "behaviors/roll_cooldown.cpp"
#include "behaviors/yawn_contagion.cpp"
#include "config/disposition_store.cpp"

namespace lamp {
OverrideAggregate overrides;
TxCredit EspNowLink::s_txCredit;

std::vector<AnimatedBehavior*> s_registered;

// Registers into the shared context exactly as compositor.cpp's does, without
// its render dependencies; the policy setter stores what compositor.cpp's does.
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
// Every peer is Smitten, so an Extrovert lamp mirrors three events in four.
uint8_t Config::getDisposition(const uint8_t*) const { return 5; }
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

const uint8_t kPeerMac[6] = {1, 2, 3, 4, 5, 6};
const uint8_t kLocalMac[6] = {};

struct Rig {
  FrameBuffer shade;
  FrameBuffer base;
  Compositor compositor;
  ExpressionManager manager;
  Rig() {
    shade.pixelCount = 4;
    shade.buffer.assign(4, Color());
    base.pixelCount = 4;
    base.buffer.assign(4, Color());
    s_registered.clear();
    manager.registry().add(kProbe);
    manager.begin(&shade, &base);
    manager.setCompositor(&compositor);
  }
  bool invoke(TransientSource source) {
    ExpressionInvocation inv;
    inv.type = "probe";
    inv.target = TARGET_SHADE;
    const uint8_t* mac = source == TransientSource::Local ? kLocalMac : kPeerMac;
    return manager.triggerInvocation(inv, mac, /*broadcast=*/false, source);
  }
};

}  // namespace

void setUp() { set_mock_millis(10000); }
void tearDown() {}

// Default policy is social off, so home mode alone blocks peers.
void test_peer_invocation_blocked_in_home_social_off() {
  Rig r;
  r.compositor.homeMode = true;
  TEST_ASSERT_FALSE(r.invoke(TransientSource::Remote));
  TEST_ASSERT_EQUAL_size_t(0, s_registered.size());
}

void test_peer_invocation_runs_outside_home_mode() {
  Rig r;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

void test_peer_invocation_runs_in_home_with_social_on() {
  Rig r;
  r.compositor.homeMode = true;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/false, {"glitchy"});
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
}

void test_local_invocation_runs_in_home_social_off() {
  Rig r;
  r.compositor.homeMode = true;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Local));
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// The type list still drops a listed type from any source in home mode.
void test_type_list_still_blocks_local() {
  Rig r;
  r.compositor.homeMode = true;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/false, {"probe"});
  TEST_ASSERT_FALSE(r.invoke(TransientSource::Local));
}

// A peer transient running when home mode turns on fades out, then is reaped;
// the lamp's own keeps running.
void test_running_peer_transient_fades_out_on_home_entry() {
  Rig r;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Local));
  TEST_ASSERT_EQUAL_size_t(2, s_registered.size());
  auto* peer = static_cast<Probe*>(s_registered[0]);

  r.compositor.homeMode = true;
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(2, s_registered.size());  // still fading

  set_mock_millis(10000 + Expression::kRetireFadeMs / 2);
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(2, s_registered.size());
  TEST_ASSERT_FALSE(peer->retired());

  set_mock_millis(10000 + Expression::kRetireFadeMs);
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_TRUE(s_registered[0] != peer);
}

void test_running_peer_transient_untouched_outside_home_mode() {
  Rig r;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  set_mock_millis(10000 + Expression::kRetireFadeMs * 5);
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// The app's Home Mode preview blocks new peer invocations but retires nothing.
void test_preview_blocks_peers_without_retiring() {
  Rig r;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  r.compositor.homeMode = true;
  r.compositor.homePreview = true;
  TEST_ASSERT_FALSE(r.invoke(TransientSource::Remote));
  r.manager.gcTransients();
  set_mock_millis(10000 + Expression::kRetireFadeMs * 2);
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
  TEST_ASSERT_FALSE(static_cast<Probe*>(s_registered[0])->retired());
}

// Ten announces from a Smitten peer book at least one mirror on an Extrovert
// lamp outside home mode. The roll cooldown is off so all ten roll.
static size_t mirrorsBookedThenFired(Rig& r, bool homeDuringAnnounce, bool preview) {
  Config config;
  config.lamp.socialMode = SocialMode::Extrovert;
  MeshLink mesh;
  SocialEchoObserver echo(config, r.manager, mesh);
  echo.tuning().copyRollCooldownMs = 0;
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;

  r.compositor.homeMode = homeDuringAnnounce;
  r.compositor.homePreview = preview;
  for (int i = 0; i < 10; ++i) echo.onPeerExpression(kPeerMac, inv);
  r.compositor.homeMode = false;
  r.compositor.homePreview = false;
  set_mock_millis(10000 + kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  echo.tick(millis());
  return s_registered.size();
}

void test_echo_books_outside_home_mode() {
  Rig r;
  TEST_ASSERT_EQUAL_size_t(1, mirrorsBookedThenFired(r, false, false));
}

// Nothing booked at home or during the app's preview fires after it ends.
void test_echo_books_nothing_at_home_or_in_preview() {
  {
    Rig r;
    TEST_ASSERT_EQUAL_size_t(0, mirrorsBookedThenFired(r, true, false));
  }
  set_mock_millis(10000);
  {
    Rig r;
    TEST_ASSERT_EQUAL_size_t(0, mirrorsBookedThenFired(r, true, true));
  }
}

// Home mode with social on still lets a peer's own invocation play, but the
// lamp rolls no copy of it.
void test_echo_books_nothing_at_home_with_social_on() {
  Rig r;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/false, {});
  TEST_ASSERT_EQUAL_size_t(0, mirrorsBookedThenFired(r, true, false));
}

// A frame heard at home spends none of the peer's roll window, so its first
// frame after home mode ends still rolls with the default cooldown on.
static size_t copiesAfterHome(bool frameAtHome) {
  Rig r;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/false, {});
  Config config;
  config.lamp.socialMode = SocialMode::Extrovert;
  MeshLink mesh;
  SocialEchoObserver echo(config, r.manager, mesh);
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;
  if (frameAtHome) {
    r.compositor.homeMode = true;
    echo.onPeerExpression(kPeerMac, inv);
    r.compositor.homeMode = false;
  }
  echo.onPeerExpression(kPeerMac, inv);
  set_mock_millis(10000 + kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  echo.tick(millis());
  return s_registered.size();
}

void test_frames_at_home_spend_no_roll() {
  TEST_ASSERT_EQUAL_size_t(1, copiesAfterHome(false));
  set_mock_millis(10000);
  TEST_ASSERT_EQUAL_size_t(1, copiesAfterHome(true));
}

// A mirror booked just before home mode begins is dropped, so it does not
// fire once home mode ends, whether or not home mode ignores peers.
static size_t mirrorsBookedBeforeHomeThenFired(bool socialDisabled) {
  Rig r;
  r.compositor.setHomeModePolicy(socialDisabled, {});
  Config config;
  config.lamp.socialMode = SocialMode::Extrovert;
  MeshLink mesh;
  SocialEchoObserver echo(config, r.manager, mesh);
  echo.tuning().copyRollCooldownMs = 0;
  ExpressionInvocation inv;
  inv.type = "probe";
  inv.target = TARGET_SHADE;
  for (int i = 0; i < 10; ++i) echo.onPeerExpression(kPeerMac, inv);

  r.compositor.homeMode = true;
  echo.tick(millis());
  r.compositor.homeMode = false;
  set_mock_millis(10000 + kMirrorDelayFloorMs + kMirrorJitterMs + 1);
  echo.tick(millis());
  return s_registered.size();
}

void test_echo_booked_before_home_is_dropped() {
  TEST_ASSERT_EQUAL_size_t(0, mirrorsBookedBeforeHomeThenFired(true));
  set_mock_millis(10000);
  TEST_ASSERT_EQUAL_size_t(0, mirrorsBookedBeforeHomeThenFired(false));
}

void test_manager_ignores_peers_in_home_and_preview() {
  Rig r;
  TEST_ASSERT_FALSE(r.manager.ignoresPeers());
  r.compositor.homeMode = true;
  TEST_ASSERT_TRUE(r.manager.ignoresPeers());
  r.compositor.homePreview = true;
  TEST_ASSERT_TRUE(r.manager.ignoresPeers());
  r.compositor.setHomeModePolicy(/*socialDisabled=*/false, {"glitchy"});
  TEST_ASSERT_FALSE(r.manager.ignoresPeers());
}

// A peer transient fading out after home mode ends leaves the sender's place
// free: the same peer's next invocation starts.
void test_retiring_transient_does_not_coalesce() {
  Rig r;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  r.compositor.homeMode = true;
  r.manager.gcTransients();
  r.compositor.homeMode = false;
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Remote));
  TEST_ASSERT_EQUAL_size_t(2, s_registered.size());
}

// A Local transient of a home-disabled type (the Lioness greeting's glitch)
// is reaped once home mode discards peers, so it neither freezes nor resumes
// mid-animation when home mode ends; the preview leaves it be.
static size_t typeSkippedTransientsLeft(bool preview) {
  Rig r;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/true, {"probe"});
  if (!r.invoke(TransientSource::Local)) return 99;
  r.compositor.homeMode = true;
  r.compositor.homePreview = preview;
  r.manager.gcTransients();
  return s_registered.size();
}

void test_type_skipped_transient_reaped_at_home() {
  TEST_ASSERT_EQUAL_size_t(0, typeSkippedTransientsLeft(false));
}

void test_type_skipped_transient_kept_in_preview() {
  TEST_ASSERT_EQUAL_size_t(1, typeSkippedTransientsLeft(true));
}

void test_listed_type_untouched_outside_home_mode() {
  Rig r;
  r.compositor.setHomeModePolicy(/*socialDisabled=*/true, {"probe"});
  TEST_ASSERT_TRUE(r.invoke(TransientSource::Local));
  r.manager.gcTransients();
  TEST_ASSERT_EQUAL_size_t(1, s_registered.size());
}

// Bloom scales its swell by the retire fade alone, so a retired bloom fades
// with the rest while the wisp leaves it at full strength.
void test_retired_bloom_paints_nothing() {
  Rig r;
  BloomExpression bloom(&r.shade);
  bloom.configure({}, 60, 900, TARGET_SHADE);
  r.compositor.addBehavior(&bloom);
  TEST_ASSERT_TRUE(bloom.trigger());
  bloom.frame = kBloomFrames / 2;

  bloom.draw();
  TEST_ASSERT_TRUE(r.shade.buffer[0].w > 0);

  r.shade.buffer.assign(4, Color());
  bloom.frame = kBloomFrames / 2;
  bloom.retire();
  set_mock_millis(10000 + Expression::kRetireFadeMs);
  bloom.draw();
  TEST_ASSERT_TRUE(r.shade.buffer[0] == Color());
}

// A retiring glitch hands the background back to the live scene as it fades:
// the snapshot taken at its trigger no longer holds the surface still.
void test_retiring_glitch_releases_the_background() {
  Rig r;
  GlitchyExpression glitch(&r.shade);
  glitch.configure({Color(0xFF, 0, 0, 0)}, 60, 900, TARGET_SHADE);
  glitch.configureFromParameters({{"durationMin", 1000}, {"durationMax", 1000}});
  r.compositor.addBehavior(&glitch);
  TEST_ASSERT_TRUE(glitch.trigger());  // snapshot: dark
  glitch.draw();

  const Color live(0, 0, 0, 0xFF);
  r.shade.buffer.assign(4, live);
  glitch.draw();
  TEST_ASSERT_EQUAL_UINT8(0, r.shade.buffer[0].w);  // running: snapshot holds

  glitch.retire();
  // Early in the fade the snapshot still dominates; late, the live scene does.
  set_mock_millis(10000 + Expression::kRetireFadeMs / 10);
  r.shade.buffer.assign(4, live);
  glitch.draw();
  TEST_ASSERT_TRUE(r.shade.buffer[0].w < 0x40);

  set_mock_millis(10000 + Expression::kRetireFadeMs / 2);
  r.shade.buffer.assign(4, live);
  glitch.draw();
  TEST_ASSERT_TRUE(r.shade.buffer[0].w > 0x30);
  TEST_ASSERT_TRUE(r.shade.buffer[0].w < 0xFF);

  set_mock_millis(10000 + Expression::kRetireFadeMs * 9 / 10);
  r.shade.buffer.assign(4, live);
  glitch.draw();
  TEST_ASSERT_TRUE(r.shade.buffer[0].w > 0xB0);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_peer_invocation_blocked_in_home_social_off);
  RUN_TEST(test_peer_invocation_runs_outside_home_mode);
  RUN_TEST(test_peer_invocation_runs_in_home_with_social_on);
  RUN_TEST(test_local_invocation_runs_in_home_social_off);
  RUN_TEST(test_type_list_still_blocks_local);
  RUN_TEST(test_running_peer_transient_fades_out_on_home_entry);
  RUN_TEST(test_running_peer_transient_untouched_outside_home_mode);
  RUN_TEST(test_retired_bloom_paints_nothing);
  RUN_TEST(test_preview_blocks_peers_without_retiring);
  RUN_TEST(test_echo_books_outside_home_mode);
  RUN_TEST(test_echo_books_nothing_at_home_or_in_preview);
  RUN_TEST(test_echo_books_nothing_at_home_with_social_on);
  RUN_TEST(test_frames_at_home_spend_no_roll);
  RUN_TEST(test_manager_ignores_peers_in_home_and_preview);
  RUN_TEST(test_echo_booked_before_home_is_dropped);
  RUN_TEST(test_retiring_transient_does_not_coalesce);
  RUN_TEST(test_type_skipped_transient_reaped_at_home);
  RUN_TEST(test_type_skipped_transient_kept_in_preview);
  RUN_TEST(test_listed_type_untouched_outside_home_mode);
  RUN_TEST(test_retiring_glitch_releases_the_background);
  return UNITY_END();
}

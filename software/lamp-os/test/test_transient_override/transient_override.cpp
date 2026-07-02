// Native-host unit tests for Phase C transient color + brightness override
// modules.
//
// Context: C.2 introduces ColorOverride / BrightnessOverride with an
// explicit FadeState (Idle/FadingIn/Holding/Restoring), source-ownership
// rules, watchdog auto-restore, mid-fade interrupt, and change-driven
// brightness callbacks.
//
// Following test_fade / test_dedup_ring / test_pending_json_slot
// convention: the modules under test are re-implemented inline here so
// the native test rig doesn't need to link the firmware. The shape of
// each test pins the OBSERVABLE contract (state transitions, what
// gets passed to configurator/callback hooks) so the production code
// can refactor freely as long as the contract holds.
//
// The configurator hookup is replaced with a tiny FakeConfigurator that
// records beginFade() calls. The wisp-pairing check is exercised by a
// small WispCache stub. millis() is faked via a globally-incrementable
// uint32_t — tests advance time explicitly.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

namespace test {

// --- Time scaffolding ------------------------------------------------------
//
// The production modules call ::millis() from Arduino. The native rig
// has no Arduino — we substitute a global g_nowMs and let each test
// advance it explicitly. The mock modules below read g_nowMs in lieu
// of millis().
static uint32_t g_nowMs = 0;
static uint32_t mockMillis() { return g_nowMs; }

// --- Mirrors of the production wire-format enums --------------------------
namespace lamp_protocol {

enum class OverrideSurface : uint8_t {
  Base  = 0x01,
  Shade = 0x02,
};

enum class OverrideSource : uint8_t {
  None     = 0x00,
  Wisp     = 0x01,
  Other    = 0x10,  // synthetic "non-Wisp" source used by cross-source tests
  Any      = 0xFF,
};

constexpr uint8_t kBrightnessOverrideMin = 5;

}  // namespace lamp_protocol

// --- Mirror of util/color.hpp's Color shape -------------------------------
struct Color {
  uint8_t r = 0, g = 0, b = 0, w = 0;
  Color() = default;
  Color(uint8_t inR, uint8_t inG, uint8_t inB, uint8_t inW)
      : r(inR), g(inG), b(inB), w(inW) {}
  bool operator==(const Color& o) const {
    return r == o.r && g == o.g && b == o.b && w == o.w;
  }
};

// --- FakeConfigurator records beginFade() calls --------------------------
//
// The production ConfiguratorBehavior owns the per-pixel interpolation;
// ColorOverride only POKES it via beginFade(target, durationMs). The
// fake replays that contract: every beginFade is recorded so the test
// can assert (a) it was called at all, (b) with the right duration, (c)
// the target was passed through.
struct FakeConfigurator {
  std::vector<Color> colors;            // current "target" the configurator holds
  std::vector<Color> lastFadeFrom;      // snapshot at last beginFade call
  uint32_t fadeStartMs_ = 0;
  uint32_t fadeDurationMs_ = 0;
  int beginFadeCount = 0;
  // Mirrors ConfiguratorBehavior::lastWebSocketUpdateTimeMs. ColorOverride
  // bumps this from apply() + restore() so the production configurator
  // doesn't lapse to STOPPED while an override is active. See the
  // production class at src/behaviors/configurator.{hpp,cpp}.
  uint32_t lastWebSocketUpdateTimeMs = 0;

  uint32_t fadeStartMs() const { return fadeStartMs_; }
  uint32_t fadeDurationMs() const { return fadeDurationMs_; }

  void beginFade(const std::vector<Color>& target, uint32_t durationMs) {
    // Snapshot the "current" view as a stand-in for the buffer. In the
    // production code the snapshot reads the frame buffer; here we use
    // the previous target as a proxy so the mid-fade-interrupt test can
    // pin that fadeFromColors_ moves from old target to the in-progress
    // value.
    lastFadeFrom = colors;
    colors = target;
    fadeStartMs_ = mockMillis();
    fadeDurationMs_ = durationMs;
    beginFadeCount += 1;
  }
};

// --- Mirror of FadeState enum --------------------------------------------
enum class FadeState : uint8_t {
  Idle = 0,
  FadingIn = 1,
  Holding = 2,
  Restoring = 3,
};

// --- ColorOverride: native mirror -----------------------------------------
//
// Behavior must match src/components/transient_override/color_override.cpp.
// If the production module changes its state transitions, mirror here.
class ColorOverride {
 public:
  FakeConfigurator* configurator_ = nullptr;
  uint8_t pixelCount_ = 0;

  FadeState state_ = FadeState::Idle;
  lamp_protocol::OverrideSource activeSource_ = lamp_protocol::OverrideSource::None;
  uint8_t activeMac_[6] = {0};

  uint32_t lastApplyMs_ = 0;
  uint32_t lastWispSeenMs_ = 0;
  uint32_t currentFadeDurationMs_ = 0;

  uint32_t restoreStartMs_ = 0;
  uint32_t restoreDurationMs_ = 0;

  std::vector<Color> savedColors_;

  static constexpr uint32_t kPaintWatchdogMs = 60000;

  void bind(FakeConfigurator* cfg, uint8_t pixelCount) {
    configurator_ = cfg;
    pixelCount_ = pixelCount;
  }

  // Build a flat "gradient" of pixelCount copies of colors[0] for the
  // test. The production version uses buildGradientWithStops; for the
  // FadeState transition tests the per-pixel layout is irrelevant, only
  // the vector length matters.
  std::vector<Color> expandToPixelCount(const Color* colors, uint8_t numColors) {
    std::vector<Color> out(pixelCount_);
    for (size_t i = 0; i < pixelCount_; ++i) {
      out[i] = colors[i % numColors];
    }
    return out;
  }

  void apply(const uint8_t sourceMac[6],
             lamp_protocol::OverrideSource source,
             const Color* colors, uint8_t numColors,
             uint32_t fadeDurationMs) {
    if (!configurator_ || numColors == 0) return;
    if (state_ == FadeState::Idle) {
      savedColors_ = configurator_->colors;
    }
    std::vector<Color> target = expandToPixelCount(colors, numColors);
    configurator_->beginFade(target, fadeDurationMs);
    // Mirror production: bump the configurator's wake-up timestamp so it
    // doesn't lapse to STOPPED while an override is active. See
    // color_override.cpp:apply().
    configurator_->lastWebSocketUpdateTimeMs = mockMillis();
    state_ = FadeState::FadingIn;
    activeSource_ = source;
    std::memcpy(activeMac_, sourceMac, 6);
    lastApplyMs_ = configurator_->fadeStartMs();
    lastWispSeenMs_ = lastApplyMs_;
    currentFadeDurationMs_ = fadeDurationMs;
  }

  void restore(const uint8_t sourceMac[6],
               lamp_protocol::OverrideSource source,
               uint32_t fadeDurationMs) {
    (void)sourceMac;
    if (!configurator_ || state_ == FadeState::Idle) return;
    if (source != lamp_protocol::OverrideSource::Any &&
        source != activeSource_) return;
    configurator_->beginFade(savedColors_, fadeDurationMs);
    configurator_->lastWebSocketUpdateTimeMs = mockMillis();
    state_ = FadeState::Restoring;
    restoreStartMs_ = configurator_->fadeStartMs();
    restoreDurationMs_ = fadeDurationMs;
  }

  void tick(uint32_t nowMs) {
    if ((state_ == FadeState::FadingIn || state_ == FadeState::Holding) &&
        nowMs - lastWispSeenMs_ >= kPaintWatchdogMs) {
      restore(activeMac_, lamp_protocol::OverrideSource::Any,
              currentFadeDurationMs_);
      return;
    }
    switch (state_) {
      case FadeState::Idle:
        return;
      case FadeState::FadingIn:
        if (nowMs - lastApplyMs_ >= currentFadeDurationMs_) {
          state_ = FadeState::Holding;
        }
        return;
      case FadeState::Holding:
        return;  // watchdog handled above
      case FadeState::Restoring: {
        const uint32_t elapsed = nowMs - restoreStartMs_;
        if (elapsed >= restoreDurationMs_) {
          state_ = FadeState::Idle;
          activeSource_ = lamp_protocol::OverrideSource::None;
          std::memset(activeMac_, 0, 6);
        }
        return;
      }
    }
  }

  void rebaseline(const std::vector<Color>& currentSavedColors) {
    if (state_ == FadeState::Idle) return;
    savedColors_ = currentSavedColors;
  }

  // Refresh the auto-restore watchdog without running a fade — the wisp's
  // paint-mode HELLO keepalive touches this so a long drift fade holds while
  // the wisp is present. Advances lastWispSeenMs_ (watchdog) only, not
  // lastApplyMs_ (fade clock), so it can't block the FadingIn→Holding latch.
  void touchApply(uint32_t nowMs) {
    if (state_ == FadeState::FadingIn || state_ == FadeState::Holding) {
      lastWispSeenMs_ = nowMs;
    }
  }

  bool isActive() const { return state_ != FadeState::Idle; }
  lamp_protocol::OverrideSource activeSource() const { return activeSource_; }
  FadeState state() const { return state_; }
};

// --- BrightnessOverride: native mirror -----------------------------------

// ease() copy — uses a tiny synthetic LUT-like math equivalent. For test
// purposes we don't care about the exact curve, only that effective()
// interpolates monotonically between from and to and the change-driven
// callback fires on integer transitions. Use linear interpolation here so
// the test math is auditable; the production code uses quadEaseInOut but
// the contract (callback only on integer-rounded changes) is the same.
static uint8_t linearLerp(uint8_t from, uint8_t to, uint32_t duration, uint32_t elapsed) {
  if (elapsed >= duration) return to;
  if (from == to) return to;
  int32_t diff = static_cast<int32_t>(to) - static_cast<int32_t>(from);
  int32_t step = (diff * static_cast<int32_t>(elapsed)) / static_cast<int32_t>(duration);
  return static_cast<uint8_t>(static_cast<int32_t>(from) + step);
}

class BrightnessOverride {
 public:
  FadeState state_ = FadeState::Idle;
  uint8_t fromBrightness_ = 0;
  uint8_t toBrightness_ = 0;
  bool fromSeeded_ = false;
  uint8_t lastReportedBrightness_ = 0;
  uint32_t fadeStartMs_ = 0;
  uint16_t fadeDurationMs_ = 0;
  uint32_t lastApplyMs_ = 0;
  uint16_t currentFadeDurationMs_ = 0;
  uint32_t restoreStartMs_ = 0;
  uint16_t restoreDurationMs_ = 0;
  lamp_protocol::OverrideSource activeSource_ = lamp_protocol::OverrideSource::None;
  uint8_t activeMac_[6] = {0};
  std::function<void()> onChange_;

  static constexpr uint32_t kPaintWatchdogMs = 60000;

  void apply(const uint8_t sourceMac[6],
             lamp_protocol::OverrideSource source,
             lamp_protocol::OverrideSurface surface,
             uint8_t brightness, uint16_t fadeDurationMs) {
    (void)surface;
    if (state_ == FadeState::Idle) {
      fromSeeded_ = false;
    } else {
      fromBrightness_ = toBrightness_;
      fromSeeded_ = true;
    }
    toBrightness_ = brightness;
    state_ = FadeState::FadingIn;
    activeSource_ = source;
    std::memcpy(activeMac_, sourceMac, 6);
    fadeStartMs_ = mockMillis();
    fadeDurationMs_ = fadeDurationMs;
    lastApplyMs_ = fadeStartMs_;
    currentFadeDurationMs_ = fadeDurationMs;
  }

  void restore(const uint8_t sourceMac[6],
               lamp_protocol::OverrideSource source,
               uint16_t fadeDurationMs) {
    (void)sourceMac;
    if (state_ == FadeState::Idle) return;
    if (source != lamp_protocol::OverrideSource::Any &&
        source != activeSource_) return;
    fromBrightness_ = toBrightness_;
    state_ = FadeState::Restoring;
    restoreStartMs_ = mockMillis();
    restoreDurationMs_ = fadeDurationMs;
  }

  uint8_t effective(uint32_t nowMs, uint8_t baseline) const {
    switch (state_) {
      case FadeState::Idle:
        return baseline;
      case FadeState::FadingIn: {
        const uint32_t elapsed = nowMs - fadeStartMs_;
        if (fadeDurationMs_ == 0 || elapsed >= fadeDurationMs_) return toBrightness_;
        const uint8_t from = fromSeeded_ ? fromBrightness_ : baseline;
        return linearLerp(from, toBrightness_, fadeDurationMs_, elapsed);
      }
      case FadeState::Holding:
        return toBrightness_;
      case FadeState::Restoring: {
        const uint32_t elapsed = nowMs - restoreStartMs_;
        if (restoreDurationMs_ == 0 || elapsed >= restoreDurationMs_) return baseline;
        return linearLerp(fromBrightness_, baseline, restoreDurationMs_, elapsed);
      }
    }
    return baseline;
  }

  void tick(uint32_t nowMs, uint8_t baseline) {
    switch (state_) {
      case FadeState::Idle:
        return;
      case FadeState::FadingIn: {
        if (!fromSeeded_) {
          fromBrightness_ = baseline;
          fromSeeded_ = true;
        }
        const uint32_t elapsed = nowMs - fadeStartMs_;
        if (elapsed >= fadeDurationMs_) state_ = FadeState::Holding;
        break;
      }
      case FadeState::Holding: {
        const uint32_t elapsed = nowMs - lastApplyMs_;
        if (elapsed >= kPaintWatchdogMs) {
          restore(activeMac_, lamp_protocol::OverrideSource::Any,
                  currentFadeDurationMs_);
        }
        break;
      }
      case FadeState::Restoring: {
        const uint32_t elapsed = nowMs - restoreStartMs_;
        if (elapsed >= restoreDurationMs_) {
          state_ = FadeState::Idle;
          activeSource_ = lamp_protocol::OverrideSource::None;
          std::memset(activeMac_, 0, 6);
        }
        break;
      }
    }
    const uint8_t current = effective(nowMs, baseline);
    if (current != lastReportedBrightness_) {
      lastReportedBrightness_ = current;
      if (onChange_) onChange_();
    }
  }

  void setOnChangeCallback(std::function<void()> cb) { onChange_ = std::move(cb); }
  bool isActive() const { return state_ != FadeState::Idle; }
  FadeState state() const { return state_; }
};

}  // namespace test

void setUp(void) { test::g_nowMs = 0; }
void tearDown(void) {}

// Common test fixtures.
static const uint8_t kMacWisp[6]     = {0xAA, 0x01, 0x02, 0x03, 0x04, 0x05};
static const uint8_t kMacPeerSwap[6] = {0xBB, 0x11, 0x22, 0x33, 0x44, 0x55};

// ============================================================================
// ColorOverride tests
// ============================================================================

void test_color_override_apply_bumps_configurator_websocket_timestamp() {
  // ColorOverride::apply() must bump the configurator's
  // lastWebSocketUpdateTimeMs alongside beginFade(). Without this, the
  // production configurator's CONFIGURATOR_WEBSOCKET_TIMEOUT_MS=60s
  // gate would lapse to STOPPED after ~60s of no BLE writes — the
  // wisp paint would still be APPLY'd but the Compositor would skip
  // the configurator's draw(), leaving the LED buffer on whatever
  // last painted it (a stale Pulse from a social greet, the initial
  // boot colors, etc.). The wisp's ~5-10s paint cadence keeps this
  // bumped continuously while paint is flowing; the override's own
  // 60s watchdog handles the fall-back when the wisp goes silent.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(100, 100, 100, 0);
  test::g_nowMs = 12345;
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
           &target, 1, /*fadeDurationMs=*/100);
  TEST_ASSERT_EQUAL(12345u, cfg.lastWebSocketUpdateTimeMs);

  // A second apply() at a later time bumps the timestamp again — this
  // is what keeps the configurator perpetually awake while wisp paint
  // is flowing every ~5s.
  test::g_nowMs = 50000;
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
           &target, 1, 100);
  TEST_ASSERT_EQUAL(50000u, cfg.lastWebSocketUpdateTimeMs);

  // restore() also bumps — the configurator must stay awake through
  // the fade-back animation, otherwise the watchdog-driven restore
  // would hand off to a STOPPED configurator.
  test::g_nowMs = 80000;
  ov.restore(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, 200);
  TEST_ASSERT_EQUAL(80000u, cfg.lastWebSocketUpdateTimeMs);
}

void test_color_override_apply_transitions_fading_then_holding() {
  // apply() must immediately put state into FadingIn. tick() must move
  // to Holding once the fade window has elapsed but not before.
  test::FakeConfigurator cfg;
  cfg.colors.resize(10, test::Color(10, 20, 30, 0));  // initial baseline
  test::ColorOverride ov;
  ov.bind(&cfg, /*pixelCount=*/10);

  test::Color target(200, 100, 50, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
           &target, 1, /*fadeDurationMs=*/100);
  TEST_ASSERT_EQUAL(test::FadeState::FadingIn, ov.state());
  TEST_ASSERT_EQUAL(1, cfg.beginFadeCount);
  TEST_ASSERT_EQUAL_UINT32(100u, cfg.fadeDurationMs());

  // Mid-fade: still FadingIn.
  test::g_nowMs = 50;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::FadingIn, ov.state());

  // Past the fade window: Holding.
  test::g_nowMs = 100;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());
}

void test_color_override_restore_transitions_restoring_then_idle() {
  // restore() puts state into Restoring; tick() lands Idle after the
  // restore fade window elapses.
  test::FakeConfigurator cfg;
  cfg.colors.resize(8, test::Color(0, 0, 0, 0));
  test::ColorOverride ov;
  ov.bind(&cfg, 8);

  test::Color target(255, 255, 255, 255);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 0);
  test::g_nowMs = 1;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());

  test::g_nowMs = 500;
  ov.restore(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
             /*fadeDurationMs=*/200);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());

  test::g_nowMs = 600;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());

  test::g_nowMs = 700;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Idle, ov.state());
}

void test_color_override_watchdog_auto_restores_after_60s() {
  // No re-apply within kPaintWatchdogMs (60s) of lastApplyMs_ → tick()
  // auto-fires restore(). The override transitions Holding → Restoring.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color(50, 50, 50, 0));
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(255, 0, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 50);
  test::g_nowMs = 60;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());

  // Just before the watchdog: still Holding.
  test::g_nowMs = 59999;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());

  // At the boundary: Restoring.
  test::g_nowMs = 60000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());
}

void test_color_override_touch_apply_defers_watchdog() {
  // The wisp's paint-mode HELLO keepalive calls touchApply() every ~5s, which
  // advances the watchdog (lastWispSeenMs_) so a wisp-painted surface holds
  // while the wisp is present. Pins that the watchdog anchors to the last
  // touchApply, not the original apply.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color(50, 50, 50, 0));
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(255, 0, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 50);
  test::g_nowMs = 60;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());

  // 50 s into Holding (10 s before watchdog) — touchApply from a sibling
  // surface drain. Watchdog reference moves forward to 50 s.
  test::g_nowMs = 50000;
  ov.touchApply(test::g_nowMs);

  // 60 s into the original lifetime: STILL Holding because the watchdog
  // now anchors to the touchApply at 50 s, not the original apply.
  test::g_nowMs = 60000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());

  // 110 s — 60 s past the touchApply — the watchdog finally fires.
  test::g_nowMs = 110000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());
}

void test_color_override_watchdog_fires_during_long_fade() {
  // A drift fade longer than the watchdog window with no keepalive: the
  // watchdog fires while still FadingIn (it no longer waits for Holding).
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color(50, 50, 50, 0));
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::g_nowMs = 0;
  test::Color target(255, 0, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1,
           /*fadeDurationMs=*/1800000);  // 30 min, well past the 60s watchdog
  test::g_nowMs = 30000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::FadingIn, ov.state());  // fade far from done

  test::g_nowMs = 60000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());  // fired mid-fade
}

void test_color_override_touch_apply_does_not_block_latch() {
  // touchApply advances the watchdog clock only, never the fade clock, so a
  // mid-fade keepalive must not stop FadingIn from latching to Holding.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color(50, 50, 50, 0));
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::g_nowMs = 0;
  test::Color target(255, 0, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 1000);
  test::g_nowMs = 500;
  ov.touchApply(test::g_nowMs);  // mid-fade keepalive
  test::g_nowMs = 1000;
  ov.tick(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());  // latch not blocked
}

void test_color_override_touch_apply_noop_when_idle() {
  // touchApply on an Idle override is a no-op — it can't re-anchor a
  // watchdog that isn't armed, and must not transition state.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  TEST_ASSERT_EQUAL(test::FadeState::Idle, ov.state());
  test::g_nowMs = 12345;
  ov.touchApply(test::g_nowMs);
  TEST_ASSERT_EQUAL(test::FadeState::Idle, ov.state());
}

void test_color_override_source_ownership_blocks_cross_source_restore() {
  // Wisp-owned override + non-Wisp restore → no-op (state unchanged).
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(100, 100, 100, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 0);
  test::g_nowMs = 1;
  ov.tick(test::g_nowMs);  // Holding

  const int fadesBefore = cfg.beginFadeCount;
  ov.restore(kMacPeerSwap, test::lamp_protocol::OverrideSource::Other, 200);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, ov.state());
  TEST_ASSERT_EQUAL(fadesBefore, cfg.beginFadeCount);
}

void test_color_override_source_any_restore_succeeds_regardless() {
  // Wisp-owned override + Any-source restore → succeeds. Lets the
  // watchdog and admin/shutdown paths reset without needing to know
  // the original source.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(100, 100, 100, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 0);
  test::g_nowMs = 1;
  ov.tick(test::g_nowMs);

  ov.restore(kMacWisp, test::lamp_protocol::OverrideSource::Any, 200);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, ov.state());
}

void test_color_override_rebaseline_updates_saved_colors_mid_holding() {
  // BLE shade write while Holding → the user-supplied colors become the
  // new restore-to baseline. The next restore lands on those colors
  // instead of the pre-override baseline.
  test::FakeConfigurator cfg;
  const test::Color preBaseline(10, 20, 30, 0);
  cfg.colors.resize(4, preBaseline);
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(200, 100, 50, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &target, 1, 0);
  test::g_nowMs = 1;
  ov.tick(test::g_nowMs);

  // BLE write: new baseline.
  const test::Color newBaseline(99, 88, 77, 0);
  std::vector<test::Color> newColors(4, newBaseline);
  ov.rebaseline(newColors);

  ov.restore(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, 100);
  // beginFade's target should be the new baseline now.
  TEST_ASSERT_EQUAL_UINT8(newBaseline.r, cfg.colors[0].r);
  TEST_ASSERT_EQUAL_UINT8(newBaseline.g, cfg.colors[0].g);
  TEST_ASSERT_EQUAL_UINT8(newBaseline.b, cfg.colors[0].b);
}

void test_color_override_rebaseline_noop_when_idle() {
  // rebaseline() must NOT do anything when state is Idle (no override
  // active → BLE write goes straight to the configurator, there's no
  // savedColors_ to swap).
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  std::vector<test::Color> later(4, test::Color(1, 2, 3, 4));
  ov.rebaseline(later);
  TEST_ASSERT_EQUAL(test::FadeState::Idle, ov.state());
  // savedColors_ stays empty (default).
  TEST_ASSERT_EQUAL(0u, ov.savedColors_.size());
}

void test_color_override_mid_fade_interrupt_restarts_fade() {
  // apply() while FadingIn → beginFade() called again with the new target
  // and a fresh fadeStartMs_. The configurator's lastFadeFrom captures
  // wherever the interpolation was mid-fade (in the fake, this is the
  // previous target; in production it's the live buffer state).
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color first(255, 0, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &first, 1, 200);
  TEST_ASSERT_EQUAL(1, cfg.beginFadeCount);
  TEST_ASSERT_EQUAL_UINT32(200u, cfg.fadeDurationMs());

  // Mid-fade re-apply.
  test::g_nowMs = 50;
  test::Color second(0, 255, 0, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, &second, 1, 300);
  TEST_ASSERT_EQUAL(2, cfg.beginFadeCount);
  TEST_ASSERT_EQUAL_UINT32(300u, cfg.fadeDurationMs());
  TEST_ASSERT_EQUAL(test::FadeState::FadingIn, ov.state());
  // fadeStartMs_ moved to the second-apply timestamp.
  TEST_ASSERT_EQUAL_UINT32(50u, cfg.fadeStartMs());
}

void test_color_override_uint32_fade_duration_no_truncation() {
  // A 30-minute fade (1,800,000 ms) must survive apply() without truncation.
  // Before this fix, fadeDurationMs_ was uint16: 1,800,000 & 0xFFFF = 50,752.
  test::FakeConfigurator cfg;
  cfg.colors.resize(4, test::Color());
  test::ColorOverride ov;
  ov.bind(&cfg, 4);

  test::Color target(200, 100, 50, 0);
  ov.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
           &target, 1, /*fadeDurationMs=*/1800000u);

  TEST_ASSERT_EQUAL_UINT32(1800000u, cfg.fadeDurationMs());
  TEST_ASSERT_EQUAL_UINT32(1800000u, ov.currentFadeDurationMs_);

  // The critical product in ease() at the midpoint: 900,000 * 511 = 459,900,000.
  // Must stay below UINT32_MAX (~4.29e9) so no uint64 intermediate is needed.
  const uint32_t elapsed = 1800000u / 2;
  const uint32_t product = elapsed * 511u;
  TEST_ASSERT_EQUAL_UINT32(459900000u, product);  // no overflow → exact value
  // LUT index at midpoint: 459,900,000 / 1,800,000 * 511 / 511 = 255.
  TEST_ASSERT_EQUAL_UINT32(255u, (product / 1800000u * 511u) / 511u);
}

// ============================================================================
// BrightnessOverride tests
// ============================================================================

void test_brightness_override_apply_transitions_to_holding() {
  test::BrightnessOverride b;
  test::g_nowMs = 100;
  b.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
          test::lamp_protocol::OverrideSurface::Base,
          /*brightness=*/40, /*fadeDurationMs=*/100);
  TEST_ASSERT_EQUAL(test::FadeState::FadingIn, b.state());

  test::g_nowMs = 200;
  b.tick(test::g_nowMs, /*baseline=*/100);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, b.state());
}

void test_brightness_override_restore_returns_to_baseline() {
  test::BrightnessOverride b;
  test::g_nowMs = 0;
  b.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
          test::lamp_protocol::OverrideSurface::Base, 30, 0);
  test::g_nowMs = 1;
  b.tick(test::g_nowMs, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, b.state());

  // Restore — over a 100ms window.
  test::g_nowMs = 1000;
  b.restore(kMacWisp, test::lamp_protocol::OverrideSource::Wisp, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, b.state());

  // Past the window: Idle, effective returns baseline.
  test::g_nowMs = 1101;
  b.tick(test::g_nowMs, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Idle, b.state());
  TEST_ASSERT_EQUAL_UINT8(100, b.effective(test::g_nowMs, 100));
}

void test_brightness_override_change_driven_callback_only_on_int_change() {
  // Over a long-duration fade the callback must fire roughly N times,
  // where N is the integer step count between from and to — NOT once per
  // tick. Capture a count and verify it's bounded.
  test::BrightnessOverride b;
  int callbackCount = 0;
  b.setOnChangeCallback([&]() { callbackCount += 1; });

  test::g_nowMs = 0;
  b.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
          test::lamp_protocol::OverrideSurface::Base,
          /*brightness=*/20, /*fadeDurationMs=*/1000);
  // From 100 → 20 over 1000ms is 80 integer steps. Tick at 1ms granularity
  // (1000 ticks); callbacks should fire ~80 times (one per integer step).
  for (int i = 1; i <= 1000; ++i) {
    test::g_nowMs = i;
    b.tick(test::g_nowMs, 100);
  }
  // Allow some slack for the integer rounding boundary (the apply()
  // doesn't fire a callback for the immediate-snap value at t=0; tick()
  // only ever fires when the rounded value actually changes).
  TEST_ASSERT_TRUE_MESSAGE(callbackCount >= 70 && callbackCount <= 90,
                           "callback fires roughly once per integer brightness step");
}

void test_brightness_override_floor_rejects_below_min_unless_wisp_paired() {
  // This is the contract enforced by MeshLink, NOT by
  // BrightnessOverride itself — but the test pins the rule: brightness <
  // kBrightnessOverrideMin from a non-Wisp-paired source is dropped;
  // from a Wisp-paired source it's accepted.
  //
  // The "wisp paired" check looks up the source MAC in a recent WispCache
  // entry. Here we re-implement the floor predicate inline so the test
  // documents the rule without needing to drag in mesh_link.cpp.
  struct WispCache {
    bool present;
    uint8_t mac[6];
    uint32_t lastHelloMs;
  };
  auto floorAccepts = [](uint8_t brightness, const uint8_t srcMac[6],
                         const WispCache& wisp, uint32_t nowMs) {
    if (brightness >= test::lamp_protocol::kBrightnessOverrideMin) return true;
    constexpr uint32_t kWindow = 60000;
    return wisp.present &&
           std::memcmp(wisp.mac, srcMac, 6) == 0 &&
           (nowMs - wisp.lastHelloMs) < kWindow;
  };

  WispCache empty{};
  TEST_ASSERT_FALSE(floorAccepts(2, kMacPeerSwap, empty, 1000));

  WispCache paired{};
  paired.present = true;
  std::memcpy(paired.mac, kMacWisp, 6);
  paired.lastHelloMs = 100;
  // Within the window: accepted even at brightness=2.
  TEST_ASSERT_TRUE(floorAccepts(2, kMacWisp, paired, 1000));
  // Outside the window (60s): rejected.
  TEST_ASSERT_FALSE(floorAccepts(2, kMacWisp, paired, 100 + 60000));
  // Wisp-paired but wrong sender: rejected.
  TEST_ASSERT_FALSE(floorAccepts(2, kMacPeerSwap, paired, 1000));
  // Above the floor: always accepted (paired or not).
  TEST_ASSERT_TRUE(floorAccepts(50, kMacPeerSwap, empty, 1000));
}

void test_brightness_override_watchdog_auto_restores() {
  test::BrightnessOverride b;
  test::g_nowMs = 0;
  b.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
          test::lamp_protocol::OverrideSurface::Base, 50, 0);
  test::g_nowMs = 1;
  b.tick(test::g_nowMs, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, b.state());

  test::g_nowMs = 60000;
  b.tick(test::g_nowMs, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Restoring, b.state());
}

void test_brightness_override_cross_source_restore_blocked() {
  // Non-Wisp restore against Wisp-owned override: no-op.
  test::BrightnessOverride b;
  b.apply(kMacWisp, test::lamp_protocol::OverrideSource::Wisp,
          test::lamp_protocol::OverrideSurface::Base, 40, 0);
  test::g_nowMs = 1;
  b.tick(test::g_nowMs, 100);

  b.restore(kMacPeerSwap, test::lamp_protocol::OverrideSource::Other, 100);
  TEST_ASSERT_EQUAL(test::FadeState::Holding, b.state());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();

  RUN_TEST(test_color_override_apply_bumps_configurator_websocket_timestamp);
  RUN_TEST(test_color_override_apply_transitions_fading_then_holding);
  RUN_TEST(test_color_override_restore_transitions_restoring_then_idle);
  RUN_TEST(test_color_override_watchdog_auto_restores_after_60s);
  RUN_TEST(test_color_override_touch_apply_defers_watchdog);
  RUN_TEST(test_color_override_watchdog_fires_during_long_fade);
  RUN_TEST(test_color_override_touch_apply_does_not_block_latch);
  RUN_TEST(test_color_override_touch_apply_noop_when_idle);
  RUN_TEST(test_color_override_source_ownership_blocks_cross_source_restore);
  RUN_TEST(test_color_override_source_any_restore_succeeds_regardless);
  RUN_TEST(test_color_override_rebaseline_updates_saved_colors_mid_holding);
  RUN_TEST(test_color_override_rebaseline_noop_when_idle);
  RUN_TEST(test_color_override_mid_fade_interrupt_restarts_fade);
  RUN_TEST(test_color_override_uint32_fade_duration_no_truncation);

  RUN_TEST(test_brightness_override_apply_transitions_to_holding);
  RUN_TEST(test_brightness_override_restore_returns_to_baseline);
  RUN_TEST(test_brightness_override_change_driven_callback_only_on_int_change);
  RUN_TEST(test_brightness_override_floor_rejects_below_min_unless_wisp_paired);
  RUN_TEST(test_brightness_override_watchdog_auto_restores);
  RUN_TEST(test_brightness_override_cross_source_restore_blocked);

  return UNITY_END();
}

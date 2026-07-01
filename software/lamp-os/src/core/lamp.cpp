// software/lamp-os/src/core/lamp.cpp
//
// Framework heart: file-scope state (singletons, pending slots, mutexes),
// Lamp::setup() boot wiring, Lamp::tick() orchestrator, initBehaviors,
// brightness/home-mode helpers, dispatchLampAction.
//
// Sibling TUs share file-scope state via core/lamp_internal.hpp:
//   - core/lamp_drains.cpp    — 22 Lamp::drainX() method bodies
//   - core/lamp_remote_op.cpp — cascade-receive routing

#include "lamp.hpp"
#include "lamp_internal.hpp"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <Preferences.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_lamp.hpp"
#include "components/apply/apply_settings_blob.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/firmware_distributor.hpp"
#include "components/firmware/fs_ota.hpp"
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <NimBLEDevice.h>
#include <esp_ota_ops.h>
#endif
#include "components/network/bluetooth.hpp"
#include "components/network/ble_control.hpp"
#include "components/network/nearby_lamps.hpp"
#include "components/network/mesh_link.hpp"
#include "components/network/wifi.hpp"
#if LAMP_WEBAPP_ENABLED
#include "components/webapp/webapp.hpp"
#endif
#include "core/personality_engine.hpp"
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "expressions/expression_manager.hpp"
#include "behaviors/configurator.hpp"
#include "behaviors/fade_in.hpp"
#include "behaviors/fade_out.hpp"
#include "behaviors/idle.hpp"
#include "behaviors/knockout.hpp"
#include "behaviors/social.hpp"
#include "config/config.hpp"
#include "config/config_types.hpp"
#include "core/animated_behavior.hpp"
#include "core/behavior_context.hpp"
#include "core/compositor.hpp"
#include "core/frame_buffer.hpp"
#include "core/ota_quiet_mode.hpp"
#include "core/pending_json_slot.hpp"
#include "core/pending_typed_slot.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/override_aggregate.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"
#include "util/gradient.hpp"
#include "util/levels.hpp"

Adafruit_NeoPixel* shadeStrip = nullptr;
Adafruit_NeoPixel* baseStrip = nullptr;
Preferences prefs;

// Zero-allocation pending slots. BLE callbacks on the NimBLE host task
// (Core 0) only do a fixed-size memcpy under portMUX into these slots. The
// loop task (Core 1) drains them and does ALL heap work — JSON parsing,
// vector building, state mutation. Single-core memory pattern.
//
// Each post-helper below is a one-line forwarder into a slot's `post()`;
// the matching drain body lives in core/lamp_drains.cpp.

volatile int8_t pendingBrightness = -1;

// ── User brightness micro-fade ─────────────────────────────────────────────
// The pendingBrightness drain (live-preview from CHAR_BRIGHTNESS) used to
// call shadeStrip->setBrightness directly on every write. Each WriteCoalescer
// window delivers a write every ~60-100 ms, so a sustained slider drag landed
// as visibly stepped brightness changes — user described it as "not smooth".
//
// This small fade state interpolates between successive user writes over
// kUserBrightnessFadeMs (~80 ms — slightly longer than the coalescer floor so
// each step bleeds into the next without overshooting). Drain stamps a new
// (source, target, start) triple per write; the loop tick interpolates and
// applies. Tick is gated to brightnessOverride.isActive() == false so an
// active wisp override owns the strip uncontested via the existing
// applyEffectiveBrightness change-callback path.
//
// Source initialisation: drain reads the current visual level by lerping the
// previous fade in flight, so an immediate re-write picks up wherever the
// strip actually is. Cold-start (no prior write) seeds source = target so the
// very first write snaps without a black-up ramp.
static constexpr uint16_t kUserBrightnessFadeMs = 80;
static uint8_t  s_userBrightnessSource = 0;
static uint8_t  s_userBrightnessTarget = 0;
static uint32_t s_userBrightnessFadeStartMs = 0;
static bool     s_userBrightnessSeeded = false;

// HwConfig-sourced brightness cap. Set in Lamp::setup() from hw_.maxBrightness
// so free functions across this TU + sibling TUs use the correct variant
// ceiling without importing a variant header.
uint8_t s_hwMaxBrightness = 180;

// Compute the current interpolated brightness for the live user-write fade.
// Returns target once the fade window has elapsed.
namespace lamp {
uint8_t computeUserBrightnessNow(uint32_t nowMs) {
  if (s_userBrightnessSource == s_userBrightnessTarget) {
    return s_userBrightnessTarget;
  }
  const uint32_t elapsed = nowMs - s_userBrightnessFadeStartMs;
  if (elapsed >= kUserBrightnessFadeMs) {
    return s_userBrightnessTarget;
  }
  const int32_t span =
      static_cast<int32_t>(s_userBrightnessTarget) - s_userBrightnessSource;
  return static_cast<uint8_t>(
      s_userBrightnessSource +
      (span * static_cast<int32_t>(elapsed)) /
          static_cast<int32_t>(kUserBrightnessFadeMs));
}

// Accessors for src/components/apply/apply_brightness.hpp — exposed
// here so the apply module can drive the micro-fade triple without
// re-exposing it globally.
uint8_t  brightnessFadeSource()    { return s_userBrightnessSource; }
uint8_t  brightnessFadeTarget()    { return s_userBrightnessTarget; }
uint32_t brightnessFadeStartMs()   { return s_userBrightnessFadeStartMs; }
bool     brightnessFadeSeeded()    { return s_userBrightnessSeeded; }

void setBrightnessFade(uint8_t source, uint8_t target, uint32_t startMs) {
  s_userBrightnessSource      = source;
  s_userBrightnessTarget      = target;
  s_userBrightnessFadeStartMs = startMs;
  s_userBrightnessSeeded      = true;
}

void clearBrightnessFadeSeed() {
  s_userBrightnessSeeded = false;
}

}  // namespace lamp
// Bring apply_brightness helpers into file scope so existing unqualified
// call sites (applyEffectiveBrightness, computeUserBrightnessNow) continue
// to resolve without touching every call site individually.
using lamp::applyEffectiveBrightness;
using lamp::computeUserBrightnessNow;
// Flag set from Core 0 (BLE callbacks) when the home-mode preview state
// changes — either the flag itself flipped, or homeMode.brightness was
// updated via CHAR_HOME_PREVIEW cmd 0x02. The loop task on Core 1 drains
// it and calls applyEffectiveBrightness so the strip transitions cleanly.
volatile bool pendingApplyEffectiveBrightness = false;
// Flag set from Core 0 (BLE ServerCallbacks::onDisconnect) when the phone
// walks away — forces a synchronous disposition NVS commit so the user's
// final slider value survives even if power is yanked before the debounce
// window elapses. Core 1 drain calls config.flushDispositionsNow().
// See DispositionDebouncer in config.hpp for the NVS-wear rationale.
volatile bool pendingFlushDispositionsRequested = false;

PendingKnockoutUpdate pendingKnockout;

// Pending triggerExpression invocations whose delayMs > 0 — drained from the
// loop task once millis() reaches fireAtMs. Loop-task only (the WiFi-task
// CONTROL_OP handler just posts JSON into lamp::pendingSlots.inboundOp, and
// the drain that parses it into this queue runs on the loop task), so no mutex.
struct DelayedInvocation {
  lamp::ExpressionInvocation inv;
  uint32_t fireAtMs;
  uint8_t srcMac[6];   // carried through so receiver-side coalesce still
                       // works when the actual fire is delayed.
};
static constexpr size_t MAX_PENDING_TRIGGERS = 16;
std::vector<DelayedInvocation> pendingTriggers;
portMUX_TYPE pendingMux = portMUX_INITIALIZER_UNLOCKED;

// Thin forwarders into the template. Each slot's bounds check + mux
// discipline lives inside PendingJsonSlot::post — these helpers exist
// only so ble_control.cpp (and the ESP-NOW recv callback) can stay
// blissfully unaware of which slot a particular char's payload lands in.
void postPendingShadeColorsJson(const char* data, size_t len) { lamp::pendingSlots.shadeColors.post(pendingMux, data, len); }
void postPendingBaseColorsJson(const char* data, size_t len)  { lamp::pendingSlots.baseColors.post(pendingMux, data, len); }
void postPendingBrightness(int8_t level) { pendingBrightness = level; }
void postPendingApplyEffectiveBrightness() { pendingApplyEffectiveBrightness = true; }
// Single-bit post called from the NimBLE host task (Core 0) inside
// ServerCallbacks::onDisconnect — NVS is NOT Core-0-safe so we cannot
// call config.flushDispositionsNow() there directly. The loop drain on
// Core 1 picks this up next iteration and runs the synchronous flush.
void postPendingFlushDispositions() { pendingFlushDispositionsRequested = true; }
void postPendingKnockout(uint8_t pixel, uint8_t brightness) {
  portENTER_CRITICAL(&pendingMux);
  pendingKnockout.pixel = pixel;
  pendingKnockout.brightness = brightness;
  pendingKnockout.valid = true;
  portEXIT_CRITICAL(&pendingMux);
}

void postPendingExpressionOpJson(const char* data, size_t len)      { lamp::pendingSlots.expressionOp.post(pendingMux, data, len); }
void postPendingWifiOpJson(const char* data, size_t len)            { lamp::pendingSlots.wifiOp.post(pendingMux, data, len); }
void postPendingTestActionJson(const char* data, size_t len)        { lamp::pendingSlots.testAction.post(pendingMux, data, len); }
void postPendingSettingsBlobJson(const char* data, size_t len)      { lamp::pendingSlots.settingsBlob.post(pendingMux, data, len); }
void postPendingRemoteOpJson(const char* data, size_t len)          { lamp::pendingSlots.remoteOp.post(pendingMux, data, len); }
void postPendingSocialDispositionsJson(const char* data, size_t len){ lamp::pendingSlots.socialDispositions.post(pendingMux, data, len); }
// wispOp: app → CHAR_WISP_OP → here → drain → MSG_CONTROL_OP
// broadcast. The slot carries no mac because the drain inserts the
// broadcast target itself.
void postPendingWispOpJson(const char* data, size_t len)            { lamp::pendingSlots.wispOp.post(pendingMux, data, len); }

namespace lamp {
// Typed-payload posts called from MeshLink's WiFi recv task.
// Same Core 0 → Core 1 hand-off discipline as the JSON slots above —
// memcpy under portMUX, no heap, no parsing on the recv task.
void postPendingOverrideColors(const PendingOverrideColors& src)         { pendingSlots.overrideColors.post(pendingMux, src); }
void postPendingRestoreColors(const PendingRestoreColors& src)           { pendingSlots.restoreColors.post(pendingMux, src); }
void postPendingOverrideBrightness(const PendingOverrideBrightness& src) { pendingSlots.overrideBrightness.post(pendingMux, src); }
void postPendingRestoreBrightness(const PendingRestoreBrightness& src)   { pendingSlots.restoreBrightness.post(pendingMux, src); }
void postPendingWispHello(const PendingWispHello& src)                   { pendingSlots.wispHello.post(pendingMux, src); }
void postPendingWispPalette(const PendingWispPalette& src)               { pendingSlots.wispPalette.post(pendingMux, src); }
void postPendingEvent(const PendingEvent& src)                           { pendingSlots.event.post(pendingMux, src); }
void postPendingFirmwareControl(const PendingFirmwareControl& src)       { pendingSlots.firmwareControl.post(pendingMux, src); }

// Forwarder used by ExpressionManager::tryHandleExpressionEvent (and the
// legacy triggerExpression CONTROL_OP recv path) to schedule a future
// triggerInvocation without each call site having to know the queue
// storage lives here. Runs on Core 1 (drain task); the pendingTriggers
// vector is loop-task-only so no mutex needed. FIFO eviction on overflow
// — most-recent intent wins, dropping the newest would lose the user's
// latest cascade in a mesh storm.
void enqueueDelayedInvocation(const ExpressionInvocation& inv,
                              const uint8_t srcMac[6],
                              uint32_t delayMs) {
  if (pendingTriggers.size() >= MAX_PENDING_TRIGGERS) {
#ifdef LAMP_DEBUG
    Serial.println("[loop] delayed-trigger queue full, evicting oldest");
#endif
    pendingTriggers.erase(pendingTriggers.begin());
  }
  DelayedInvocation d;
  d.inv = inv;
  d.fireAtMs = millis() + delayMs;
  std::memcpy(d.srcMac, srcMac, 6);
  pendingTriggers.push_back(d);
}
}  // namespace lamp

lamp::BluetoothComponent bt;
lamp::Compositor compositor;
lamp::FrameBuffer shade;
lamp::FrameBuffer base;
lamp::SocialBehavior shadeSocialBehavior;
lamp::ConfiguratorBehavior shadeConfiguratorBehavior;
lamp::ConfiguratorBehavior baseConfiguratorBehavior;

// stampConfiguratorActivity defined here (after shadeConfiguratorBehavior /
// baseConfiguratorBehavior) so the function body can reference them without
// a forward-declared extern. Declared in apply_brightness.hpp as
// lamp::stampConfiguratorActivity.
namespace lamp {
void stampConfiguratorActivity(uint32_t nowMs) {
  shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = nowMs;
  baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = nowMs;
}
}  // namespace lamp

lamp::FadeOutBehavior shadeFadeOutBehavior;
lamp::FadeOutBehavior baseFadeOutBehavior;
lamp::KnockoutBehavior baseKnockoutBehavior;
lamp::ExpressionManager expressionManager;
lamp::Config config;
lamp::MeshLink meshLink;
// Lamp-side OTA receiver. Bound to meshLink via
// setFirmwareReceiver() in setup() so the WiFi recv path can hand
// MSG_FW_CHUNK directly to handleChunkOnRecvTask (Core 0). OFFER/DONE
// arrive via pendingFirmwareControl and are drained on Core 1.
lamp::FirmwareReceiver    firmwareReceiver;
// firmwareDistributor's global instance lives in firmware_distributor.cpp
// (extern decl in firmware_distributor.hpp) so SocialBehavior can reference
// it as `lamp::firmwareDistributor` from any TU — matches the pattern used
// by `lamp::nearbyLamps` and `lamp::personalityEngine`.

// Post-OTA "mark this app valid" state.
//
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1 ships in pioarduino's prebuilt
// sdkconfig. After the first OTA reboot, the new partition is in
// ESP_OTA_IMG_PENDING_VERIFY state; a second esp_ota_begin against that
// partition returns ESP_ERR_OTA_ROLLBACK_INVALID_STATE — every subsequent
// OTA fails until the new firmware calls
// esp_ota_mark_app_valid_cancel_rollback().
//
// We arm a 30-second timer in setup() iff we boot in pending-verify
// state. The loop tick checks the timer; on fire, we re-confirm basic
// health (BLE running, loop has iterated) and call mark_app_valid.
// Otherwise: do nothing — the bootloader will revert on the next reset.
// These globals live alongside the framework Lamp class — they are a
// framework concern (OTA self-health), not per-variant state.
bool g_pendingVerify = false;
uint32_t g_markValidDeadlineMs = 0;

// ----- Local appliers for mesh-received ops --------------------------------
//
// Shared between the slot drains (lamp_drains.cpp) and the cascade-receive
// router (lamp_remote_op.cpp): apply a JSON colors array / expression op to
// configurator + advert + section cache in one shot.
//
// INVARIANT: loop task (Core 1) only. Touch compositor behaviors, bluetooth
// advert state, and config sections — none Core-0-safe. The BLE host-task
// path stays on the post-to-pending-slot pattern (see ble_control.cpp).

// Walk a JsonArray of hex strings into a std::vector<Color>. Shared between
// the two color helpers below.
static std::vector<lamp::Color> jsonArrayToColors(JsonArray arr) {
  std::vector<lamp::Color> colors;
  colors.reserve(arr.size());
  for (JsonVariant v : arr) {
    colors.push_back(lamp::hexStringToColor(v));
  }
  return colors;
}

// renderShadeColors / renderBaseColors — the render-only core of the former
// applyShadeColorsLocal / applyBaseColorsLocal helpers. Exposed in
// namespace lamp so apply_shade_colors.hpp and apply_base_colors.hpp can
// call them without pulling in the rest of this file. Callers handle the
// bookkeeping (configurator timestamps + section invalidate).
namespace lamp {
void renderShadeColors(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<lamp::Color> colors = jsonArrayToColors(arr);
  std::vector<lamp::Color> gradient =
      lamp::buildGradientWithStops(shade.pixelCount, colors);
  // Drive the change through beginFade() so the BLE color picker
  // keeps its fade UX (~250ms ease). Mid-fade interrupts (rapid writes
  // during a drag) are handled inside ConfiguratorBehavior::beginFade —
  // it computes the in-progress lerp value as the new fade-from endpoint,
  // so successive writes rubber-band smoothly without re-snapshotting the
  // post-overlay buffer (which on Base contains knockout dimming and
  // would otherwise flicker on dimmed pixels).
  shadeConfiguratorBehavior.beginFade(gradient, lamp::kDefaultFadeMs);
  lamp::overrides.shade.rebaseline(gradient);
  // Reflect the new shade in the BLE adv so phones and v1 neighbours see it
  // without having to connect. Use the first stop — shade in this build is
  // a single color.
  bt.setAdvertisedColors(config.base.colors[config.base.ac], colors[0]);
}

// renderBaseColors — counterpart to renderShadeColors.
// See the bookkeeping note above — callers handle timestamps + invalidate.
void renderBaseColors(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<lamp::Color> colors = jsonArrayToColors(arr);
  std::vector<lamp::Color> gradient =
      lamp::buildGradientWithStops(base.pixelCount, colors);
  // See renderShadeColors: the fade-snapshot-from-buffer flicker on
  // knockout pixels is now handled at the ConfiguratorBehavior::beginFade
  // call site itself (snapshots from `colors` / in-progress lerp, not
  // fb->buffer). No mid-fade guard needed here.
  baseConfiguratorBehavior.beginFade(gradient, lamp::kDefaultFadeMs);
  lamp::overrides.base.rebaseline(gradient);
  // Reflect the new base in the BLE adv — first stop is what the adv carries
  // (we don't know the user's active-stop index from this drain, and the
  // first stop is what bt.begin used initially).
  bt.setAdvertisedColors(colors[0], config.shade.colors[0]);
}
}  // namespace lamp

namespace lamp {
// Internal helper for the apply_expressions.hpp split. mutateConfig=true is
// the user-source path (BLE expressionOp drain) — drives expressionManager
// AND mirrors into config.expressions so persistConfig() captures it.
// mutateConfig=false is the remote-source path (applyRemoteOpLocal for a
// cascade-relayed CONTROL_OP) — drives expressionManager only so the
// cascade's transient state never becomes persistable.
void runExpressionOp(JsonObject doc, bool mutateConfig) {
  if (doc.isNull()) return;
  const char* op = doc["op"].as<const char*>();
  if (op && strcmp(op, "upsert") == 0 && doc["entry"].is<JsonObject>()) {
    JsonObject entry = doc["entry"].as<JsonObject>();
    lamp::ExpressionConfig cfg;
    cfg.type = std::string(entry["type"] | "");
    cfg.enabled = entry["enabled"] | true;
    cfg.intervalMin = entry["intervalMin"] | 60;
    cfg.intervalMax = entry["intervalMax"] | 900;
    cfg.target = entry["target"] | 3;
    for (JsonPair kv : entry) {
      std::string key(kv.key().c_str());
      // `disabledDuringWispOverride` is tolerated from older app payloads
      // but ignored — it's now a pure type-property on the Expression
      // subclass, not config-driven.
      if (key == "type" || key == "enabled" ||
          key == "disabledDuringWispOverride" ||
          key == "intervalMin" || key == "intervalMax" ||
          key == "target" || key == "colors") continue;
      JsonVariant v = kv.value();
      if (v.is<uint32_t>()) cfg.setParameter(key, v.as<uint32_t>());
      else if (v.is<int>()) cfg.setParameter(key, static_cast<uint32_t>(v.as<int>()));
    }
    // Store the JsonArray in a local so iteration doesn't reference a
    // temporary that's destroyed at the end of the full expression
    // (ArduinoJson 7.4.x tightened lifetime semantics on chained calls;
    // before this the same code happened to work).
    JsonArray colorsArr = entry["colors"].as<JsonArray>();
    for (JsonVariant cv : colorsArr) {
      cfg.colors.push_back(lamp::hexStringToColor(cv));
    }
    if (!cfg.type.empty()) {
      ::expressionManager.upsertExpression(cfg, &::compositor);
      if (mutateConfig) {
        // Mirror into config.expressions so the next settings_blob save
        // persists the user's latest edits. expressionManager is the runtime
        // animator; config is what gets serialized to NVS.
        auto& exprs = ::config.expressions.expressions;
        bool found = false;
        for (auto& e : exprs) {
          if (e.type == cfg.type && e.target == cfg.target) {
            e = cfg;
            found = true;
            break;
          }
        }
        if (!found) exprs.push_back(cfg);
      }
    }
  } else if (op && strcmp(op, "remove") == 0) {
    const char* type = doc["type"].as<const char*>();
    int tgt = doc["target"] | 0;
    if (type && tgt >= 1 && tgt <= 3) {
      ::expressionManager.removeExpression(type, static_cast<lamp::ExpressionTarget>(tgt), &::compositor);
      if (mutateConfig) {
        // Mirror removal into config.expressions.
        auto& exprs = ::config.expressions.expressions;
        exprs.erase(std::remove_if(exprs.begin(), exprs.end(),
                      [&](const lamp::ExpressionConfig& e) {
                        return e.type == type && e.target == tgt;
                      }),
                    exprs.end());
      }
    }
  }
  // NOTE: callers invalidate the expressions section themselves so that the
  // drain block preserves its prior behavior of always marking dirty (even
  // on parse failure where nothing actually changed).
}
}  // namespace lamp

// Forward decl — defined later, alongside effectiveBrightness which
// shares the same gate. initBehaviors uses it to seed compositor.begin.
static bool calculateEffectiveHomeMode();
// Forward decl — same reasoning. The BrightnessOverride callback wired
// in initBehaviors needs to reach the same brightness applier the rest
// of the lamp uses. Defined below alongside effectiveBrightness.
namespace lamp { void applyEffectiveBrightness(); }
static uint8_t effectiveBrightness();

void initBehaviors(lamp::Features features) {
  // ── Unconditional: framework-essential behaviors ──────────────────────────
  // Configurators are always needed (they hold saved colors + wisp paint).
  shadeConfiguratorBehavior = lamp::ConfiguratorBehavior(&shade, 120);
  shadeConfiguratorBehavior.colors = shade.defaultColors;
  baseConfiguratorBehavior = lamp::ConfiguratorBehavior(&base, 120);
  baseConfiguratorBehavior.colors = base.defaultColors;

  // ExpressionManager always begins (it owns the expression band in the
  // compositor). Whether it loads saved expressions is gated below.
  expressionManager.begin(&shade, &base);

  // ── Feature-gated behavior construction ──────────────────────────────────
  if (lamp::any(features, lamp::Features::SocialBehavior)) {
    shadeSocialBehavior = lamp::SocialBehavior(&shade, 1200);
    // Live config pointer so SocialBehavior::control reads the current
    // socialMode each tick (user can change personality at runtime; the
    // change rides through settings_blob save + reboot, but the wiring
    // is per-instance regardless).
    shadeSocialBehavior.setConfig(&config);
    // Pause social greetings when the lamp is in home mode — home mode is
    // the user's "I'm home, calm down" mode. Compositor gates this via
    // the homeMode flag, kept in sync by reapplyHomeModeState().
    shadeSocialBehavior.allowedInHomeMode = false;
  }

  if (lamp::any(features, lamp::Features::FadeOutBehavior)) {
    shadeFadeOutBehavior = lamp::FadeOutBehavior(&shade, REBOOT_ANIMATION_FRAMES);
    baseFadeOutBehavior = lamp::FadeOutBehavior(&base, REBOOT_ANIMATION_FRAMES);
  }

  if (lamp::any(features, lamp::Features::KnockoutBehavior)) {
    baseKnockoutBehavior = lamp::KnockoutBehavior(&base, 0, true);
    baseKnockoutBehavior.knockoutPixels = config.base.knockoutPixels;
  }

  // Features::DefaultExpressions gates whether saved NVS expressions are
  // loaded on boot. Subclasses that replace the expression set skip this
  // so their own expressions aren't shadowed by stale NVS data.
  if (lamp::any(features, lamp::Features::DefaultExpressions)) {
    expressionManager.loadFromConfig(config.expressions);
  }

  // ── Assemble behavior stack ───────────────────────────────────────────────
  // Draw order = registration order, last-writer-wins on the surface buffer.
  //
  // Configurator (wisp paint + saved colors) goes FIRST — it's the base
  // scene. Social greetings overlay next. Expressions come LAST so brief
  // transient effects (glitchy / pulse / breathing / shifty) compose on
  // top of everything else and naturally yield when their animation
  // completes (animationState=STOPPED → Compositor skips them →
  // configurator's writes are the final state).
  //
  // The configurator must register BEFORE the expressions so that
  // expressions compose ON TOP of whatever the configurator writes.
  // (Reversing the order would let the configurator overwrite per-pixel
  // expression writes every frame, making non-exclusive expressions
  // invisible during wisp paint — the prior bug that the now-removed
  // `configurator.disabled` flag papered over.)

  std::vector<lamp::AnimatedBehavior*> allBehaviors = {};

  // Configurator (base scene — saved colors + wisp paint via beginFade)
  allBehaviors.push_back(&baseConfiguratorBehavior);
  allBehaviors.push_back(&shadeConfiguratorBehavior);

  // Social greeting behaviors
  if (lamp::any(features, lamp::Features::SocialBehavior)) {
    allBehaviors.push_back(&shadeSocialBehavior);
  }

  // Expression behaviors LAST — transient effects compose on top of
  // everything below. When their animationState transitions to STOPPED
  // the compositor skips them and the configurator's base scene shows
  // through (no recovery needed).
  auto exprBehaviors = expressionManager.getBehaviors();
  allBehaviors.insert(allBehaviors.end(), exprBehaviors.begin(), exprBehaviors.end());

  // Fade-out behaviors run last so reboot animation is on top of everything
  if (lamp::any(features, lamp::Features::FadeOutBehavior)) {
    allBehaviors.push_back(&baseFadeOutBehavior);
    allBehaviors.push_back(&shadeFadeOutBehavior);
  }

  std::vector<lamp::AnimatedBehavior*> underlayBehaviors;
  std::vector<lamp::AnimatedBehavior*> startupBehaviors;
  for (auto* fb : std::vector<lamp::FrameBuffer*>{&shade, &base}) {
    underlayBehaviors.push_back(new lamp::IdleBehavior(fb, 0, true));
    startupBehaviors.push_back(new lamp::FadeInBehavior(fb, STARTUP_ANIMATION_FRAMES));
  }
  compositor.begin(allBehaviors, {&shade, &base}, underlayBehaviors, startupBehaviors, calculateEffectiveHomeMode());
  // Mark where the initial expression band ends so runtime-added transient
  // invocations are inserted into the expression block (preserves "all
  // expressions draw together, late in the list" ordering).
  // Offset accounts for the fade-out behaviors appended after the expression
  // band (2 when FadeOutBehavior is enabled, 0 otherwise).
  const size_t fadeOutCount =
      lamp::any(features, lamp::Features::FadeOutBehavior) ? 2u : 0u;
  compositor.setExpressionBandEnd(allBehaviors.size() - fadeOutCount);

  if (lamp::any(features, lamp::Features::KnockoutBehavior)) {
    compositor.overlayBehaviors.push_back(&baseKnockoutBehavior);
  }

  // Finish wiring the shared BehaviorContext. The Compositor self-publishes
  // in its constructor; we publish the ExpressionManager + frame buffer list
  // here so the expressions just registered by compositor.begin() can reach
  // both from this point onward. (setCompositor() later in setup() repeats
  // these writes idempotently — they're cheap pointer assignments.)
  auto& behaviorCtx = compositor.behaviorContext();
  behaviorCtx.expressionManager = &expressionManager;
  behaviorCtx.expressionFrameBuffers.clear();
  behaviorCtx.expressionFrameBuffers.push_back(&shade);
  behaviorCtx.expressionFrameBuffers.push_back(&base);
  // Publish the two configurator pointers so the per-surface
  // ColorOverride instances can resolve their target configurator via
  // bind() without grabbing globals.
  behaviorCtx.baseConfigurator = &baseConfiguratorBehavior;
  behaviorCtx.shadeConfigurator = &shadeConfiguratorBehavior;
  // Mesh + identity surface for custom behaviors
  behaviorCtx.nearbyLamps = &lamp::nearbyLamps;
  // bind() the override instances. From here on apply()/restore() will
  // drive the right configurator's beginFade.
  lamp::overrides.base.bind(behaviorCtx, lamp_protocol::OverrideSurface::Base);
  lamp::overrides.shade.bind(behaviorCtx, lamp_protocol::OverrideSurface::Shade);
  // Wisp-state change callbacks. Each surface's ColorOverride fires
  // when wisp goes from un-controlling → controlling or vice versa
  // (edge-triggered inside maybeNotifyWispStateChange). The Flutter
  // app subscribes to CHAR_WISP_STATUS so a notify lands the moment
  // a surface transitions; the indicator widget pops on / off without
  // having to poll.
  lamp::overrides.base.setOnWispStateChangeCallback(
      []() { ble_control::notifyWispStatus(); });
  lamp::overrides.shade.setOnWispStateChangeCallback(
      []() { ble_control::notifyWispStatus(); });
  // Provider that the CHAR_WISP_STATUS read merges into the JSON. Lives
  // here so the ColorOverride globals stay out of the network layer.
  lamp::nearbyLamps.setLampWispStateProvider([]() {
    lamp::NearbyLamps::LampWispState ws;
    ws.controllingBase  = lamp::overrides.base.isWispActive();
    ws.controllingShade = lamp::overrides.shade.isWispActive();
    if (lamp::overrides.base.hasLastWispColor()) {
      ws.baseWispColor = lamp::colorToHexString(
          lamp::overrides.base.lastWispColor());
    }
    if (lamp::overrides.shade.hasLastWispColor()) {
      ws.shadeWispColor = lamp::colorToHexString(
          lamp::overrides.shade.lastWispColor());
    }
#ifdef LAMP_DEBUG
    // Provider-side view; pair with [wisp_state] notify in ble_control.cpp.
    Serial.printf("[wisp_state] provider isWispActive base=%d shade=%d hasBaseC=%d hasShadeC=%d\n",
                  ws.controllingBase ? 1 : 0,
                  ws.controllingShade ? 1 : 0,
                  lamp::overrides.base.hasLastWispColor() ? 1 : 0,
                  lamp::overrides.shade.hasLastWispColor() ? 1 : 0);
#endif
    return ws;
  });
  // BrightnessOverride routes its change-driven callback into the
  // existing applyEffectiveBrightness path so master-brightness fades
  // share the same NeoPixel setBrightness entry point.
  lamp::overrides.brightness.setOnChangeCallback([]() { applyEffectiveBrightness(); });

  // Init order: needs config + expressionManager + meshLink — all
  // constructed above. See personality_engine.hpp for behavior details.
  lamp::personalityEngine.begin(&config, &expressionManager, &meshLink);
}

/**
 * Parse ExpressionConfig from JSON object using generic parameter system
 */
lamp::ExpressionConfig parseExpressionConfig(JsonObject node) {
  lamp::ExpressionConfig expr;
  expr.type = std::string(node["type"] | "");
  expr.enabled = node["enabled"] | false;
  expr.intervalMin = node["intervalMin"] | 60;
  expr.intervalMax = node["intervalMax"] | 900;
  expr.target = node["target"] | 3;

  JsonArray colors = node["colors"];
  if (colors.size()) {
    for (JsonVariant color : colors) {
      expr.colors.push_back(lamp::hexStringToColor(color));
    }
  }

  for (JsonPair kv : node) {
    const char* key = kv.key().c_str();
    std::string keyStr(key);
    // `disabledDuringWispOverride` tolerated from older payloads but
    // ignored — pure type-property now.
    if (keyStr == "type" || keyStr == "enabled" ||
        keyStr == "disabledDuringWispOverride" ||
        keyStr == "intervalMin" || keyStr == "intervalMax" ||
        keyStr == "target" || keyStr == "colors") {
      continue;
    }
    JsonVariant value = kv.value();
    if (value.is<uint32_t>()) {
      expr.setParameter(keyStr, value.as<uint32_t>());
    } else if (value.is<int>()) {
      expr.setParameter(keyStr, static_cast<uint32_t>(value.as<int>()));
    }
  }

  return expr;
}

void dispatchLampAction(JsonDocument& doc, unsigned long updateTimeMs) {
  shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = updateTimeMs;
  baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = updateTimeMs;

  String action = String(doc["a"]);
  if (action == "test_expression") {
    String type = String(doc["type"]);
    if (type.length() > 0) {
      lamp::ExpressionTarget target = doc["target"].is<int>()
        ? static_cast<lamp::ExpressionTarget>(doc["target"].as<int>())
        : lamp::TARGET_BOTH;
      std::vector<lamp::Color> payloadColors =
          jsonArrayToColors(doc["colors"].as<JsonArray>());
      if (!payloadColors.empty()) {
        // Colors provided → run a transient one-shot seeded with those colors.
        // Works on factory lamps with zero configured expressions (no lookup).
        // Zero MAC is the local-test sentinel; triggerInvocation coalesces
        // rapid re-fires from the same (srcMac, type) pair so spam-tapping
        // Test doesn't pile up transients.
        static const uint8_t kLocalTestMac[6] = {0, 0, 0, 0, 0, 0};
        lamp::ExpressionInvocation inv;
        inv.type = type.c_str();
        inv.target = static_cast<uint8_t>(target);
        inv.colors = std::move(payloadColors);
#ifdef LAMP_DEBUG
        Serial.printf("[test] transient pulse colors=%zu type=%s target=%d\n",
                      inv.colors.size(), inv.type.c_str(),
                      static_cast<int>(target));
#endif
        expressionManager.triggerInvocation(inv, kLocalTestMac);
        // markTestActive scans configured expressions; for transients it's a
        // no-op (returns false), so notifyStateChange is not called here.
        // The transient self-cleans on animation complete, no reap needed.
        expressionManager.markTestActive(type.c_str(), target);
      } else {
        // No colors payload → trigger an already-configured expression by name.
        // This is the expression-editor "test a saved expression" flow.
#ifdef LAMP_DEBUG
        auto cfgColors = expressionManager.getExpressionColors(type.c_str());
        String colorList;
        for (const auto& c : cfgColors) {
          if (colorList.length() > 0) colorList += " ";
          colorList += lamp::colorToHexString(c).c_str();
        }
        Serial.printf("[test] configured trigger: %s target=%d [%s]\n",
                      type.c_str(), static_cast<int>(target), colorList.c_str());
#endif
        // Just trigger the expression. No configurator gating needed:
        // expressions draw AFTER the configurator in the behavior list, so
        // they compose on top of wisp paint naturally and yield (via
        // animationState=STOPPED) when their one-shot animation completes.
        expressionManager.triggerExpression(type.c_str(), target);
        // Track the just-fired entries as active previews so the loop drain's
        // reapCompletedTests() can flip previewActive=false the instant they
        // STOP. Emit the previewActive=true edge if this was the first one.
        if (expressionManager.markTestActive(type.c_str(), target)) {
          ble_control::notifyStateChange();
        }
      }
    }
  } else if (action == "test_expression_complete") {
    // Clear the active-test set first so the immediate state-notify carries
    // previewActive=false. Stops continuous expressions (breathing, shifty)
    // is the responsibility of the configurator re-asserting baseline below.
    if (expressionManager.clearAllTestActive()) {
      ble_control::notifyStateChange();
    }
    // App may have edited saved colors during the test; snap configurator
    // to the new values + re-assert any active wisp paint so the new
    // baseline doesn't briefly stomp it.
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();

    if (doc["shadeColors"]) {
      JsonArray shadeColors = doc["shadeColors"];
      if (shadeColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant shadeColor : shadeColors) {
          updatedColors.push_back(lamp::hexStringToColor(shadeColor));
        }
        shadeConfiguratorBehavior.colors = lamp::buildGradientWithStops(shade.pixelCount, updatedColors);
      }
    }
    if (doc["baseColors"]) {
      JsonArray baseColors = doc["baseColors"];
      if (baseColors.size()) {
        std::vector<lamp::Color> updatedColors;
        for (JsonVariant baseColor : baseColors) {
          updatedColors.push_back(lamp::hexStringToColor(baseColor));
        }
        baseConfiguratorBehavior.colors = lamp::buildGradientWithStops(base.pixelCount, updatedColors);
      }
    }
    // If the wisp was painting either surface before the expression
    // test, the configurator-color writes just above stomped the wisp's
    // target gradient with the lamp's saved colors. Re-assert the wisp
    // paint immediately so the surface returns to what it was showing
    // pre-test, rather than waiting up to ~10s for the wisp's next
    // backstop paint cycle. No-op when the override isn't in Holding.
    lamp::overrides.shade.reassertHold();
    lamp::overrides.base.reassertHold();
  }
#ifdef LAMP_DEBUG
  // Personality dev-injection hook: replaces nearbyLamps view inside
  // PersonalityEngine so a developer can simulate a crowd from one
  // physical lamp + the Flutter app's test-action button. Without this,
  // verifying the 50% crowd-dim floor would need 10 lamps in BLE range
  // simultaneously.
  //
  // Payload:
  //   {"a":"inject_nearby","peers":[
  //     {"name":"red","baseColor":"#FF0000FF","disposition":5},
  //     {"name":"blue","baseColor":"#0000FFFF","disposition":1}
  //   ]}
  // Pair with {"a":"clear_nearby"} to drop back to live data.
  else if (action == "inject_nearby") {
    std::vector<lamp::NearbyLamp> peers;
    JsonArray arr = doc["peers"];
    for (JsonVariant v : arr) {
      lamp::NearbyLamp p;
      p.name = String(v["name"] | "").c_str();
      if (p.name.empty()) continue;
      const String baseHex = String(v["baseColor"] | "#FFFFFFFF");
      p.baseColor = lamp::hexStringToColor(baseHex.c_str());
      p.lastRssi = v["rssi"].is<int>() ? static_cast<int8_t>(v["rssi"].as<int>()) : -50;
      const int disp = v["disposition"] | 3;
      if (disp >= 1 && disp <= 5 && !p.name.empty()) {
        config.setDisposition(p.name, static_cast<uint8_t>(disp));
      }
      peers.push_back(p);
    }
    Serial.printf("[personality] inject_nearby count=%u\n", (unsigned)peers.size());
    lamp::personalityEngine.setNearbyOverride(std::move(peers));
  } else if (action == "clear_nearby") {
    Serial.println("[personality] clear_nearby");
    lamp::personalityEngine.clearNearbyOverride();
  }
#endif
}

extern void lamp_register_panic_handler();

// Effective brightness, mirroring calculateEffectiveHomeMode() so the
// brightness value and the compositor's homeMode gate stay in lockstep.
// See calculateEffectiveHomeMode below for the rule.
static uint8_t effectiveBrightness();
static bool calculateEffectiveHomeMode();

// ── Crowd-dim micro-fade ───────────────────────────────────────────────────
// PersonalityEngine commits a new crowdDimFactor() when its smoothed
// weighted-peer count crosses the deadband (≥ 2/100 change). The factor
// is a hard step — without interpolation, a deadband crossing snaps
// brightness by ≥ 2 levels in one frame, visible at low overall
// brightness. This triple interpolates between successive engine targets
// over kCrowdDimFadeMs so the transition reads as smooth.
//
// Distinct from the user-write triple (s_userBrightness*) because that
// path only runs while no transient override is active and is driven by
// a different trigger (BLE writes). Crowd-dim fades happen on the engine
// tick path, runs every frame inside effectiveBrightness(), and stays
// out of the OTA pulse multiplier's way by only re-arming when the
// engine's TARGET factor changes (not when raw brightness changes).
static constexpr uint16_t kCrowdDimFadeMs = 80;
static float    s_crowdAppliedFactor = 1.0f;
static float    s_crowdTargetFactor  = 1.0f;
static uint32_t s_crowdFadeStartMs   = 0;
static bool     s_crowdFadeActive    = false;

// Current interpolated crowd-dim factor. Detects target changes against
// the engine and (re)starts a fade from wherever we are now, so a
// mid-fade retrigger glides instead of snapping.
static float currentCrowdDimFactor(uint32_t nowMs) {
  const float engineTarget = lamp::personalityEngine.crowdDimFactor();
  if (engineTarget != s_crowdTargetFactor) {
    // Snapshot the currently-interpolated value before swapping the
    // target so the new fade starts from where we visibly are.
    float startFrom = s_crowdAppliedFactor;
    if (s_crowdFadeActive) {
      const uint32_t elapsed = nowMs - s_crowdFadeStartMs;
      if (elapsed >= kCrowdDimFadeMs) {
        startFrom = s_crowdTargetFactor;
      } else {
        const float span = s_crowdTargetFactor - s_crowdAppliedFactor;
        startFrom = s_crowdAppliedFactor +
                    span * (static_cast<float>(elapsed) /
                            static_cast<float>(kCrowdDimFadeMs));
      }
    }
    s_crowdAppliedFactor = startFrom;
    s_crowdTargetFactor  = engineTarget;
    s_crowdFadeStartMs   = nowMs;
    s_crowdFadeActive    = true;
  }
  if (!s_crowdFadeActive) return s_crowdTargetFactor;
  const uint32_t elapsed = nowMs - s_crowdFadeStartMs;
  if (elapsed >= kCrowdDimFadeMs) {
    s_crowdAppliedFactor = s_crowdTargetFactor;
    s_crowdFadeActive = false;
    return s_crowdTargetFactor;
  }
  const float span = s_crowdTargetFactor - s_crowdAppliedFactor;
  return s_crowdAppliedFactor +
         span * (static_cast<float>(elapsed) /
                 static_cast<float>(kCrowdDimFadeMs));
}

static uint8_t effectiveBrightness() {
  const uint8_t raw = calculateEffectiveHomeMode() ? config.homeMode.brightness
                                                    : config.lamp.brightness;
  // PersonalityEngine crowd-dim (Introvert / Ambivert + smoothed peer weight).
  // Applied BEFORE the transient brightnessOverride.effective() so paint /
  // wisp overrides continue to work normally over the dimmed baseline.
  // The engine reports a target factor; we interpolate between successive
  // targets over kCrowdDimFadeMs so deadband-crossing commits don't snap.
  const float factor = currentCrowdDimFactor(millis());
  uint8_t afterCrowd;
  if (factor >= 0.999f) {
    afterCrowd = raw;
  } else {
    const float scaled = static_cast<float>(raw) * factor;
    afterCrowd = static_cast<uint8_t>(scaled + 0.5f);
    if (afterCrowd < 1) afterCrowd = 1;  // never blank from personality alone
  }
  return afterCrowd;
}

namespace lamp {
void applyEffectiveBrightness() {
  uint8_t baseline = effectiveBrightness();
  uint8_t level = lamp::overrides.brightness.effective(millis(), baseline);
  if (shadeStrip) shadeStrip->setBrightness(lamp::calculateBrightnessLevel(s_hwMaxBrightness, level));
  if (baseStrip) baseStrip->setBrightness(lamp::calculateBrightnessLevel(s_hwMaxBrightness, level));
}
}  // namespace lamp

#if defined(ARDUINO) || defined(ESP_PLATFORM)
namespace lamp {

void updateAdvertisedDeviceName(const char* newName) {
  // Update the GAP device name AND rebuild the active advertisement
  // payload so the new name surfaces to mid-scan phones without a
  // reboot. NimBLE caches the advertisement frame; the stop/start
  // pair forces it to re-emit with the freshly-set device name on
  // the very next advertising tick. Existing GATT connections are
  // unaffected (advertising and connections are separate state
  // machines in NimBLE). The brief gap (~50ms — well under the
  // advertising interval) is below human perception.
  NimBLEDevice::setDeviceName(newName);
  auto* adv = NimBLEDevice::getAdvertising();
  if (adv != nullptr) {
    adv->stop();
    adv->start();
  }
}

}  // namespace lamp
#endif  // defined(ARDUINO) || defined(ESP_PLATFORM)

namespace lamp {

void applyKnockoutPixel(uint8_t pixel, uint8_t brightness) {
  if (pixel < ::config.base.px && brightness <= 100) {
    ::baseKnockoutBehavior.knockoutPixels[pixel] = brightness;
    ::config.base.knockoutPixels[pixel] = brightness;
    // Live per-pixel knockout — does NOT invalidate the base section
    // cache. See the architectural invariant comment at the brightness
    // drain — CHAR_COMMIT is what invalidates the cache, not per-pixel
    // knockout writes.
  }
}

}  // namespace lamp

// Two regimes:
//   1. BT client connected (the app is the "configurator"): home mode is
//      forced OFF unless the user is on the Home Mode page, in which case
//      it's forced ON so they can preview brightness / behavior changes.
//      The flag is set by the app via CHAR_HOME_MODE_FOCUS and cleared on
//      BT disconnect.
//   2. No BT client connected: presence-based. Home mode iff the user
//      has enabled it AND has a saved SSID AND the most recent wifi scan
//      saw that SSID nearby. The lamp never associates — just sniffs
//      beacons. No password ever leaves the lamp.
static bool calculateEffectiveHomeMode() {
  if (ble_control::isClientConnected()) {
    return ble_control::isHomeModePageActive();
  }
  return config.homeMode.enabled
      && !config.homeMode.ssid.empty()
      && wifi::homeSsidVisible(config.homeMode.ssid);
}

// Single funnel for "home mode state may have changed" — keeps the
// compositor's behavior gate and the strip brightness in lockstep so
// the lamp transitions cleanly when preview flips or WiFi associates /
// disassociates.
static void reapplyHomeModeState() {
  compositor.setHomeMode(calculateEffectiveHomeMode());
  applyEffectiveBrightness();
}

static void onWifiStateChanged() {
  // This callback fires from Arduino-ESP32's WiFi event task — NOT Core 1.
  // Calling into compositor.setHomeMode / shadeStrip->setBrightness from
  // here races Core 1's compositor.tick + frame_buffer.flush, corrupting
  // the NeoPixel byte buffer and the behavior vector. Symptom: lamp
  // crash-loops with rst:0x3 (SW_RESET) + _invalid_pc_placeholder during
  // background scan completion or any other WiFi state transition.
  //
  // Safe path: post the pending flag and let Core 1's loop drain call
  // reapplyHomeModeState on its own thread.
  postPendingApplyEffectiveBrightness();
  ble_control::notifyWifiState();
}

// ── FrameBuffer accessors (for lamp.hpp's shadeFb()/baseFb()) ─────────────
lamp::FrameBuffer* lamp::Lamp::shadeFb() { return &::shade; }
lamp::FrameBuffer* lamp::Lamp::baseFb()  { return &::base; }

void lamp::Lamp::setup() {
  // Validate HwConfig before any hardware init. Malformed configs halt
  // with a visible blink (recoverable only via re-flash).
  if (!validateHwConfig(hw_)) {
    Serial.println("[lamp] FATAL: malformed HwConfig — halting");
    pinMode(LED_BUILTIN, OUTPUT);
    for (;;) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }
  }
  // Seed the file-scope brightness cap from the variant's HwConfig so
  // free functions (applyEffectiveBrightness, applyRemoteOpLocal) use
  // the correct ceiling without importing a variant header.
  s_hwMaxBrightness = hw_.maxBrightness;

  // `config` as an unqualified name inside a Lamp member function resolves to
  // Lamp::config() (the accessor method). Bring the file-scope global into
  // scope as a local reference so the method body can use `config` unqualified
  // the same way all the free functions and apply-helpers do.
  lamp::Config& config = ::config;
#ifdef LAMP_DEBUG
  Serial.begin(115200);
#endif
  lamp_register_panic_handler();

  config = lamp::Config(&prefs);
  // Re-populate the in-memory lampType from NVS — Config::Config loads
  // the JSON blob, but lampType lives under its own NVS key (set by
  // main.cpp's resolveLampType chain before this setup() runs).
  config.loadLampType();

  // Apply subclass defaults AFTER NVS load. Fields that NVS left at their
  // factory value (e.g. name="stray", colors empty) get the subclass's
  // preferred first-boot value. Fields the user has already saved in NVS
  // are NOT touched — the NVS value is authoritative.
  config.applyDefaults(defaults());

  // First-ever-boot housekeeping, persisted once so it sticks across reboots:
  //  - Random color per surface so lamps aren't visually identical out of the
  //    box. Shade hue is offset from base by [60,300]°, keeping the two at
  //    least 60° apart on the wheel so a lamp never boots base≈shade.
  //  - Migrate already-configured lamps (custom name OR a control password)
  //    onto the explicit `setup` flag, so the fielded fleet isn't treated as
  //    unconfigured after taking this firmware.
  bool persistFirstBoot = false;
  if (!config.lamp.colorsRandomized) {
    FastRng rng;
    int baseHue = rng.range(0, 359);
    int shadeHue = (baseHue + rng.range(60, 300)) % 360;
    config.base.colors = {colorFromHue(baseHue)};
    config.shade.colors = {colorFromHue(shadeHue)};
    config.base.ac = 0;
    config.lamp.colorsRandomized = true;
    persistFirstBoot = true;
  }
  if (!config.lamp.setup &&
      (config.lamp.name != defaults().name || !config.lamp.password.empty())) {
    config.lamp.setup = true;
    persistFirstBoot = true;
  }
  if (persistFirstBoot) config.persistConfig("first-boot-init");

  wifi::begin();
  wifi::setStateChangeCallback(onWifiStateChanged);
  wifi::setHomeModeEnabledGetter([]() { return config.homeMode.enabled; });
  // Suspend WiFi background scans while OTA is in flight — the scan hops
  // the radio across 11+ channels for ~5s and silently drops ESP-NOW
  // unicast (both OFFER→lamp AND lamp→wisp ACCEPT/REQ) during that window.
  wifi::setOtaInProgressGetter([]() { return firmwareReceiver.isInProgress(); });

  bt.begin(config.lamp.name, config.base.colors[config.base.ac],
           config.shade.colors[0], config.lamp.setup);
  bt.activateGattServices(&config);

#if LAMP_WEBAPP_ENABLED
  if (config.lamp.webappEnabled) webapp::begin(config);
#endif

  // Map the section's byteOrder string to the NeoPixel format flag. The
  // bpp-derived fallback covers lamps that didn't carry the new field in
  // their NVS payload (see config.cpp's loader — byteOrder is back-filled
  // there, so this branch shouldn't fire in practice).
  auto pickStripFmt = [](const std::string& order, uint8_t bpp) -> uint16_t {
    if (order == "GRBW") return NEO_GRBW;
    if (order == "GRB")  return NEO_GRB;
    if (order == "BGR")  return NEO_BGR;
    return (bpp == 4) ? NEO_GRBW : NEO_GRB;
  };

  // Build NeoPixel strips. Pin comes from HwConfig (hardware-immutable
  // per variant). Pixel count comes from Config (NVS-loaded, app-editable,
  // seeded on first boot by the variant's defaults()::{base,shade}Px).
  // Byte-order/bpp likewise come from Config so saved overrides survive.
  const uint16_t shadeFmt = pickStripFmt(config.shade.byteOrder, config.shade.bpp);
  const uint16_t baseFmt  = pickStripFmt(config.base.byteOrder,  config.base.bpp);

  uint8_t shadePin = 0;
  uint8_t basePin = 0;
  for (const auto& s : hw_.surfaces) {
    if      (s.id == lamp::Surface::Shade) shadePin = s.pin;
    else if (s.id == lamp::Surface::Base)  basePin  = s.pin;
  }

  shadeStrip = new Adafruit_NeoPixel(config.shade.px, shadePin, shadeFmt + NEO_KHZ800);
  baseStrip  = new Adafruit_NeoPixel(config.base.px,  basePin,  baseFmt  + NEO_KHZ800);
  applyEffectiveBrightness();
  shade.begin(lamp::buildGradientWithStops(config.shade.px, config.shade.colors), config.shade.px, shadeStrip);
  base.begin(lamp::buildGradientWithStops(config.base.px, config.base.colors), config.base.px, baseStrip);
  initBehaviors(featuresEnabled());

  // Give the subclass a chance to register any extra behaviors not covered
  // by the Feature flags. StandardLamp's createBehaviors is empty since all
  // its behaviors are Feature-flag driven; SnafuLamp adds its three custom
  // AnimatedBehavior subclasses (BackgroundFade, PaintSpots, Greeting) on
  // the shade frame buffer.
  {
    BehaviorStackBuilder builder;
    createBehaviors(builder);
    for (auto* b : builder.behaviors()) {
      compositor.addBehavior(b);
    }
  }

  // Route inbound CONTROL_OP payloads (addressed to us or broadcast) into a
  // pending slot. WiFi-task safe: pure memcpy under portMUX, no heap work.
  // MUST be installed BEFORE meshLink.begin() — otherwise any CONTROL_OP
  // that arrives in the gap is dropped because controlOpHandler_ is null.
  meshLink.setControlOpHandler(
      [](const uint8_t* payload, size_t len, const uint8_t srcMac[6]) {
        lamp::pendingSlots.inboundOp.post(
            pendingMux, reinterpret_cast<const char*>(payload), len, srcMac);
      });
  // Install the OTA receiver BEFORE meshLink.begin() so the
  // chunk fast-path is wired by the time MSG_FW_* frames start arriving.
  // FirmwareReceiver::begin captures myMac via meshLink.getMyMac(),
  // which is populated after the EspNowLink::begin() call inside
  // meshLink.begin() — so receiver-side init has to run AFTER
  // meshLink.begin(). The setFirmwareReceiver() call can happen any
  // time before the first MSG_FW_OFFER arrives, but installing it right
  // here keeps the wiring co-located.
  meshLink.setFirmwareReceiver(&firmwareReceiver);
  // Bring up ESP-NOW grid presence (HELLO + COLORS). Independent of home
  // WiFi — runs on whatever channel the radio is on. See lamp_protocol.hpp.
  meshLink.begin(&config);
  // Wire the ESP-NOW transport adapter onto the FirmwareReceiver. The
  // mesh path (wisp → lamp MSG_FW_*) emits via this transport. The BLE
  // transport is late-bound by ble_control::setFirmwareReceiver() below
  // (which uses the static BleFirmwareTransport instance internal to
  // ble_control.cpp).
  static lamp::EspNowFirmwareTransport meshFwTransport(&meshLink);
  firmwareReceiver.begin(&meshFwTransport);

  // Wire app-driven BLE OTA: registers FirmwareReceiver with ble_control so
  // CHAR_FW_CONTROL writes (OFFER/DONE) and CHAR_FW_CHUNK writes route into
  // the receiver, and the receiver can notify ACCEPT/REQ/RESULT on
  // that only one transport (mesh OR BLE) drives an active OTA at a time.
  ble_control::setFirmwareReceiver(&firmwareReceiver);

  // Gossip OTA: the lamp can ALSO originate offers to peers
  // it meets via the social system. Wire the distributor through the same
  // EspNowFirmwareTransport (it emits MSG_FW_OFFER/CHUNK/DONE and listens
  // for ACCEPT/REQ/RESULT via mesh_link's dispatch ladder).
  lamp::firmwareDistributor.begin(&meshFwTransport);
  meshLink.setFirmwareDistributor(&lamp::firmwareDistributor);
  // FS-image OTA: a second receiver/distributor pair targeting the spiffs
  // partition (shares the mesh transport; cross-OTA guard prevents overlap).
  fs_ota::begin(&meshFwTransport, &firmwareReceiver, &lamp::firmwareDistributor);

  // Arm the post-OTA self-health timer. esp_ota_get_state_partition
  // returns the ota-image-state of the running partition. If we just
  // booted from a freshly-OTA'd image, state == ESP_OTA_IMG_PENDING_VERIFY
  // and the bootloader will auto-rollback on the next reset unless we
  // explicitly mark the partition valid. We give the lamp 30 seconds of
  // steady-state runtime before declaring it healthy — long enough to
  // surface any boot-time crashes that would justify a rollback.
  //
  // Without this code path, the SECOND OTA permanently fails (esp_ota_begin
  // returns ESP_ERR_OTA_ROLLBACK_INVALID_STATE). One-shot-OTA trap.
  {
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK) {
      if (state == ESP_OTA_IMG_PENDING_VERIFY) {
        g_pendingVerify = true;
        g_markValidDeadlineMs = millis() + 30000;
#ifdef LAMP_DEBUG
        Serial.println("[ota] booted in PENDING_VERIFY, 30s self-health check armed");
#endif
      }
    }
  }
  // Wire the cascade fan-out path. The manager only sends after this; before
  // begin/setMeshLink, local triggers fire but never cascade.
  expressionManager.setMeshLink(&meshLink);
  // Compositor wired so the manager can lazy-upsert a transient entry when a
  // remote cascade arrives for an expression type that's not configured on
  // this lamp — receivers no longer need to pre-configure every type.
  expressionManager.setCompositor(&compositor);
  // One-shot reservation so the loop-task drain never reallocates mid-frame.
  pendingTriggers.reserve(MAX_PENDING_TRIGGERS);
}

// Drain order matters — the per-slot drains below run in a fixed sequence
// each loop tick on Core 1. Invariants to preserve when reordering:
//
//   * drainExpressionOp must run BEFORE drainSettingsBlob — settings_blob
//     re-applies the canonical config snapshot, and the just-arrived
//     expression edits must already be mirrored into config.expressions
//     before that dispatch.
//
//   * drainCommit runs AFTER all live-preview drains (brightness,
//     shadeColors, baseColors, knockout, expressionOp) — those mutate
//     config in RAM and the commit hash-dedups against the resulting
//     snapshot. It runs BEFORE the section-cache push at the tail
//     (ble_control::tick) so a successful commit's invalidateAllSections
//     lands in the same tick the app gets notified.
//
//   * drainSocialDispositions runs AFTER drainSettingsBlob so both NVS
//     writers serialise against the shared `prefs` instance on this core.
//
//   * Transient-override drains (overrideColors / restoreColors /
//     overrideBrightness / restoreBrightness) run BEFORE the override
//     state-machine tick at the tail so any newly-armed fade animates
//     starting on the same tick it arrived.
//
// See each drain helper's body for the per-slot detail.
void lamp::Lamp::tick() {
  // `config` shadowing kept for source fidelity with the pre-extraction
  // tick() body — the bt.tickAdvertising / maybeFlushDispositions /
  // flushDispositionsNow calls below reference it as `config`. Drain
  // helpers each re-bind locally where they need it.
  lamp::Config& config = ::config;

  // Drain pending BLE actions on the loop task (Core 1). All heap allocation
  // (JsonDocument parse, std::vector, gradient construction) happens here,
  // NOT in BLE callbacks on Core 0.

  // Debounced flush of any pending BLE advertisement color update. The
  // drain blocks below call bt.setAdvertisedColors() freely (it's a
  // fast cache write); the actual NimBLE setAdvertisementData() call
  // is rate-limited inside tickAdvertising() to avoid the host-task
  // race that panics the lamp on rapid color picker drags.
  bt.tickAdvertising();

  if (pendingApplyEffectiveBrightness && !ota_quiet_mode::isQuiet()) {
    pendingApplyEffectiveBrightness = false;
    // Preview enter/exit (cmd 0x01/0x00) and live home-brightness writes
    // (cmd 0x02) all funnel here — refresh the compositor homeMode gate
    // and the strip brightness together.
    // Held off during OTA quiet-mode so the crowd-dim micro-fade math
    // doesn't snap-apply 5 minutes of accumulated wall-clock drift in
    // one frame when quiet-mode exits. The pending flag stays set; it
    // drains on the next non-quiet tick.
    reapplyHomeModeState();
  }

  // NVS write amplification on disposition slider drag was eliminated by
  // replacing the eager persist inside Config::setDisposition with a
  // dirty-flag + timestamp. This poll runs the actual commit when the
  // user has been idle for kDispositionFlushIdleMs (5s). Cheap when
  // nothing is dirty — single bool check + uint32_t subtraction.
  config.maybeFlushDispositions(millis());
  if (pendingFlushDispositionsRequested) {
    // Phone disconnected (set on Core 0 in ble_control's onDisconnect).
    // Force-commit so the user's final slider value survives even if
    // power is yanked before the next 5s idle window would fire.
    pendingFlushDispositionsRequested = false;
    config.flushDispositionsNow();
  }

  drainBrightness();

  drainShadeColors();
  drainBaseColors();
  drainKnockout();

  drainExpressionOp();

  drainCommit();

  drainSettingsBlob();
  drainSocialDispositions();

  drainTestAction();
  drainWifiOp();
  drainInboundOp();
  drainRemoteOp();

  // Transient-override drains. Each block drains its typed slot
  // and dispatches to the matching override instance based on the
  // `surface` byte (Base / Shade / BaseAndShade).
  drainOverrideColors();
  drainRestoreColors();
  drainOverrideBrightness();
  drainRestoreBrightness();
  drainWispHello();
  drainWispPalette();
  drainWispOp();
  drainWispStatus();
  drainEvent();
  drainFirmwareControl();

  // Drive the override state machines. tick() is cheap when Idle
  // (single load + branch) so call unconditionally. Brightness tick uses
  // the same baseline as applyEffectiveBrightness so the change-driven
  // callback's view stays consistent across the fade window.
  {
    const uint32_t now = millis();
    lamp::overrides.base.tick(now);
    lamp::overrides.shade.tick(now);
    lamp::overrides.brightness.tick(now, effectiveBrightness());

    // User brightness micro-fade tick. Only runs when no transient
    // brightness override is active — applyEffectiveBrightness's
    // change-callback path owns the strip while a wisp override is
    // animating. Without this gate the two would race on every frame.
    //
    // During the 80ms fade window this path writes `level` directly
    // and intentionally bypasses both the crowd-dim factor and the OTA
    // pulse multiplier. Slider drags need to land instantly on the
    // commanded value; layering crowd-dim or OTA pulse mid-drag would
    // make the slider feel laggy and disconnected. Once the fade
    // window elapses the next applyEffectiveBrightness call reapplies
    // both multipliers cleanly.
    if (!lamp::overrides.brightness.isActive() && s_userBrightnessSeeded &&
        s_userBrightnessSource != s_userBrightnessTarget) {
      const uint8_t level = computeUserBrightnessNow(now);
      if (shadeStrip)
        shadeStrip->setBrightness(
            lamp::calculateBrightnessLevel(hw_.maxBrightness, level));
      if (baseStrip)
        baseStrip->setBrightness(
            lamp::calculateBrightnessLevel(hw_.maxBrightness, level));
      // Mark the fade complete so subsequent frames are no-ops once
      // the window has elapsed.
      if (level == s_userBrightnessTarget) {
        s_userBrightnessSource = s_userBrightnessTarget;
      }
    }

    // Personality engine — runs after the transient-override block so it
    // reads a consistent post-fade view this tick. Cheap when nothing's
    // changed (1 Hz internal cadence). consumePendingApply() trips the
    // existing applyEffectiveBrightness pump when the crowd-dim factor
    // crosses the deadband, or when SocialMode changes the dim regime.
    lamp::personalityEngine.tick(now);
    if (lamp::personalityEngine.consumePendingApply()) {
      pendingApplyEffectiveBrightness = true;
    }
  }

  // Fire any delayed triggerExpression invocations whose deadline has passed.
  // Bounded queue (MAX_PENDING_TRIGGERS) keeps this O(1) amortised; ordering
  // is INSERTION-order, not deadline-order — if a short-delayMs invocation
  // arrives after a long-delayMs one, the long one fires first when its
  // deadline hits. Best-effort by design; the cascade UX tolerates the
  // jitter and avoiding a priority queue keeps the drain trivial.
  if (!pendingTriggers.empty()) {
    const uint32_t now = millis();
    for (auto it = pendingTriggers.begin(); it != pendingTriggers.end();) {
      if (static_cast<int32_t>(now - it->fireAtMs) >= 0) {
        expressionManager.triggerInvocation(it->inv, it->srcMac);
        it = pendingTriggers.erase(it);
      } else {
        ++it;
      }
    }
  }

  wifi::tick();
#if LAMP_WEBAPP_ENABLED
  webapp::tick();
#endif
  meshLink.tick();
  // Drive OTA stall watchdog + REQ generation + 60s hard cap.
  // Cheap when state == Idle (single switch + return).
  firmwareReceiver.tick(millis());
  lamp::firmwareDistributor.tick(millis());
  fs_ota::tick(millis());
  // Post-OTA self-health check. After 30 seconds of steady-state
  // loop iteration, if BLE is up and the loop has been iterating (we're
  // inside it now so trivially true), call mark_app_valid so the next OTA
  // can succeed. If the lamp crashes before the deadline, the bootloader
  // auto-rollbacks on the next reset.
  if (g_pendingVerify && static_cast<int32_t>(millis() - g_markValidDeadlineMs) >= 0) {
    if (ble_control::isRunning()) {
      const esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
      if (err == ESP_OK) {
#ifdef LAMP_DEBUG
        Serial.println("[ota] new partition marked valid (post-OTA boot)");
#endif
      } else {
#ifdef LAMP_DEBUG
        Serial.printf("[ota] mark_app_valid failed: 0x%X\n", (unsigned)err);
#endif
      }
      g_pendingVerify = false;  // one-shot — even on failure don't retry
    } else {
      // BLE isn't running; defer another tick. If BLE never comes up,
      // the lamp's broken anyway and rollback is the right move.
      g_markValidDeadlineMs = millis() + 5000;  // re-check in 5s
    }
  }
  // Rebuild + push any dirty section JSON to its NimBLE characteristic
  // so onRead callbacks (Core 0) hand back NimBLE's already-buffered
  // bytes without walking config vectors. Cheap when nothing's dirty.
  ble_control::tick();

  compositor.tick();

  // Reap transient one-shot expressions (created by triggerInvocation when
  // a remote cascade arrived) whose animations have finished. AFTER tick so
  // the final frame of the animation is drawn before removal.
  expressionManager.gcTransients();

  // Reap the active-test set (entries fired via dispatchLampAction
  // "test_expression"). When the last one transitions to STOPPED, flip
  // CHAR_STATE_NOTIFY previewActive=false so the app's Test button
  // re-enables. AFTER tick so the just-completed STOPPED state is the
  // one we read.
  if (expressionManager.reapCompletedTests()) {
    ble_control::notifyStateChange();
  }
}

// =============================================================================
// Per-slot tick() drain helpers — definitions live in core/lamp_drains.cpp.
// Method declarations are on the Lamp class in core/lamp.hpp. Shared file-
// scope state (pendingMux, pendingKnockout, configurator behaviors, etc.)
// is wired across the two TUs via core/lamp_internal.hpp.
// =============================================================================

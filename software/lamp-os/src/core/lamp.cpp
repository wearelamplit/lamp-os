// Framework heart: file-scope state (singletons, pending slots, mutexes),
// Lamp::setup() boot wiring, Lamp::tick() orchestrator.
//
// Sibling TUs share file-scope state via core/lamp_internal.hpp:
// lamp_drains.cpp: drain bodies; lamp_remote_op.cpp: cascade routing;
// lamp_brightness.cpp: brightness/home-mode; lamp_behaviors.cpp: initBehaviors;
// lamp_test_action.cpp: dispatchLampAction.

#include "lamp.hpp"
#include "lamp_internal.hpp"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>

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
#include <esp_mac.h>
#include <esp_system.h>
#endif
#include "components/network/ble/bluetooth.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "components/network/transport/wifi.hpp"
#if LAMP_WEBAPP_ENABLED
#include "components/webapp/webapp.hpp"
#endif
#include "core/personality_engine.hpp"
#include "components/transient_override/brightness_override.hpp"
#include "components/transient_override/color_override.hpp"
#include "expressions/expression_manager.hpp"
#include "behaviors/configurator.hpp"
#include "behaviors/fade_out.hpp"
#include "behaviors/knockout.hpp"
#include "behaviors/social.hpp"
#include "behaviors/social_echo.hpp"
#include "config/config.hpp"
#include "config/config_types.hpp"
#include "config/nvs_config_store.hpp"
#include "core/compositor.hpp"
#include "core/frame_buffer.hpp"
#include "core/power_governor.hpp"
#include <lampos/blended_identity.hpp>
#include <lampos/led_power.hpp>
#include "components/firmware/ota_quiet_mode.hpp"
#include "core/pending_json_slot.hpp"
#include "core/pending_typed_slot.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/override_aggregate.hpp"
#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"
#include "util/gradient.hpp"
#include "util/heap_probe.hpp"
#include "util/levels.hpp"

lamp::NvsConfigStore configStore;

// Zero-allocation pending slots. BLE callbacks on the NimBLE host task
// (Core 0) only do a fixed-size memcpy under portMUX into these slots. The
// loop task (Core 1) drains them and does ALL heap work (JSON parsing,
// vector building, state mutation).
//
// Each post-helper below is a one-line forwarder into a slot's `post()`;
// the matching drain body lives in core/lamp_drains.cpp.

volatile int8_t pendingBrightness = -1;
volatile int16_t pendingEditSession = -1;

// HwConfig-sourced brightness cap. Set in Lamp::setup() from hw_.maxBrightness
// so free functions across this TU + sibling TUs use the correct variant
// ceiling without importing a variant header.
uint8_t s_hwMaxBrightness = 180;

lamp::PowerGovernor s_powerGovernor;
// Sense inputs resolved once in Lamp::setup() after the FrameBuffers exist.
static uint8_t s_shadeChannels = 4;
static uint8_t s_baseChannels = 4;
static uint16_t s_govPixelCount = 0;
static void governFrame();

// Bring apply_brightness helpers into file scope so unqualified call sites
// (applyEffectiveBrightness, computeUserBrightnessNow) resolve.
using lamp::applyEffectiveBrightness;
using lamp::computeUserBrightnessNow;
// Flag set from Core 0 (BLE callbacks) when the home-mode preview state
// changes (the flag flipped, or homeMode.brightness updated via
// CHAR_HOME_PREVIEW cmd 0x02). The loop task on Core 1 drains it and calls
// applyEffectiveBrightness so the strip transitions cleanly.
volatile bool pendingApplyEffectiveBrightness = false;
// Flag set from Core 0 (BLE ServerCallbacks::onDisconnect) when the phone
// walks away. Forces a synchronous flush of debounced disposition-slider
// edits so the final values survive even if power is yanked before the idle
// window elapses. Core 1 drain calls config.flushDispositionsNow().
// See DispositionDebouncer in config.hpp for the NVS-wear rationale.
volatile bool pendingFlushDispositionsRequested = false;

PendingKnockoutUpdate pendingKnockout;

// Pending triggerExpression invocations whose delayMs > 0, drained from the
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
// discipline lives inside PendingJsonSlot::post; these helpers exist only so
// ble_control.cpp (and the ESP-NOW recv callback) stay unaware of which slot
// a particular char's payload lands in.
void postPendingShadeColorsJson(const char* data, size_t len) { lamp::pendingSlots.shadeColors.post(pendingMux, data, len); }
void postPendingBaseColorsJson(const char* data, size_t len)  { lamp::pendingSlots.baseColors.post(pendingMux, data, len); }
void postPendingBrightness(int8_t level) { pendingBrightness = level; }
void postPendingEditSession(uint8_t surfaceMask, bool open) {
  pendingEditSession = static_cast<int16_t>((surfaceMask << 1) | (open ? 1 : 0));
}
void postPendingApplyEffectiveBrightness() { pendingApplyEffectiveBrightness = true; }
// Single-bit post called from the NimBLE host task (Core 0) inside
// ServerCallbacks::onDisconnect. NVS is NOT Core-0-safe, so
// config.flushDispositionsNow() can't run there directly; the loop drain on
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
// Typed-payload posts called from MeshLink's WiFi recv task. Same
// Core 0 → Core 1 hand-off as the JSON slots above: memcpy under portMUX,
// no heap, no parsing on the recv task.
void postPendingOverrideColors(const PendingOverrideColors& src)         { pendingSlots.overrideColors.post(pendingMux, src); }
void postPendingRestoreColors(const PendingRestoreColors& src)           { pendingSlots.restoreColors.post(pendingMux, src); }
void postPendingOverrideBrightness(const PendingOverrideBrightness& src) { pendingSlots.overrideBrightness.post(pendingMux, src); }
void postPendingRestoreBrightness(const PendingRestoreBrightness& src)   { pendingSlots.restoreBrightness.post(pendingMux, src); }
void postPendingWispHello(const PendingWispHello& src)                   { pendingSlots.wispHello.post(pendingMux, src); }
void postPendingWispPalette(const PendingWispPalette& src)               { pendingSlots.wispPalette.post(pendingMux, src); }
void postPendingWispClaim(const PendingWispClaim& src)                   { pendingSlots.wispClaim.post(pendingMux, src); }
void postPendingWispPaint(const PendingWispPaint& src)                   { pendingSlots.wispPaint.post(pendingMux, src); }
void postPendingCommand(const PendingCommand& src)                       { pendingSlots.command.post(pendingMux, src); }
void postPendingEvent(const PendingEvent& src)                           { pendingSlots.event.post(pendingMux, src); }
void postPendingColorQuery(const PendingColorQuery& src)                 { pendingSlots.colorQuery.post(pendingMux, src); }
void postPendingColorInfo(const PendingColorInfo& src)                   { pendingSlots.colorInfo.post(pendingMux, src); }
void postPendingFirmwareControl(const PendingFirmwareControl& src)       { pendingSlots.firmwareControl.post(pendingMux, src); }

// Schedules a future triggerInvocation without each call site having to
// know the queue storage lives here. Runs on Core 1 (drain task);
// pendingTriggers is loop-task-only so no mutex needed. FIFO eviction on
// overflow: most-recent intent wins.
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
lamp::ExpressionObserverRegistry expressionObserverRegistry;
lamp::Config config;
lamp::MeshLink meshLink;
lamp::SocialEchoObserver socialEchoObserver(config, expressionManager, meshLink);
// Lamp-side OTA receiver. Bound to meshLink via
// setFirmwareReceiver() in setup() so the WiFi recv path can hand
// MSG_FW_CHUNK directly to handleChunkOnRecvTask (Core 0). OFFER/DONE
// arrive via pendingFirmwareControl and are drained on Core 1.
lamp::FirmwareReceiver    firmwareReceiver;
// firmwareDistributor's global instance lives in firmware_distributor.cpp
// (extern decl in firmware_distributor.hpp) so SocialBehavior can reference
// it as `lamp::firmwareDistributor` from any TU.

// Post-OTA "mark this app valid" state.
//
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=1 ships in pioarduino's prebuilt
// sdkconfig. After the first OTA reboot the new partition is in
// ESP_OTA_IMG_PENDING_VERIFY state; a second esp_ota_begin against it
// returns ESP_ERR_OTA_ROLLBACK_INVALID_STATE, so every subsequent OTA fails
// until the new firmware calls esp_ota_mark_app_valid_cancel_rollback().
//
// A 30-second timer is armed in setup() iff booting in pending-verify state.
// The loop tick checks it; on fire, re-confirm basic health (BLE running,
// loop has iterated) and call mark_app_valid. Otherwise do nothing and the
// bootloader reverts on the next reset. These globals are a framework
// concern (OTA self-health), not per-variant state.
bool g_pendingVerify = false;
uint32_t g_markValidDeadlineMs = 0;

// Local appliers for mesh-received ops, shared between the slot drains
// (lamp_drains.cpp) and the cascade-receive router (lamp_remote_op.cpp):
// apply a JSON colors array / expression op to configurator + advert +
// section cache in one shot.
//
// INVARIANT: loop task (Core 1) only. These touch compositor behaviors,
// bluetooth advert state, and config sections, none of which are
// Core-0-safe. The BLE host-task path stays on the post-to-pending-slot
// pattern (see ble_control.cpp).

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

static float roleAvgFullDutyMa(const std::vector<lamp::Color>& palette,
                               uint8_t channels) {
  return palette.empty() ? 0.0f
                         : lamp::fullDutyMa(palette, channels) / palette.size();
}

// ponytail: palette-average anchor — steady (palette changes only on edit),
// ballpark by design.
static void recomputeDrawAnchors() {
  const uint16_t totalPx =
      static_cast<uint16_t>(shade.pixelCount) + base.pixelCount;
  if (totalPx == 0) {
    config.setDrawAnchors(0, 0);
    return;
  }
  const float sum =
      roleAvgFullDutyMa(config.base.broadcastColors(), s_baseChannels) *
          base.pixelCount +
      roleAvgFullDutyMa(config.shade.broadcastColors(), s_shadeChannels) *
          shade.pixelCount;
  config.setDrawAnchors(
      static_cast<uint16_t>(lamp::demandMa(sum, 0, totalPx)),
      static_cast<uint16_t>(lamp::demandMa(sum, 255, totalPx)));
}

// renderShadeColors / renderBaseColors: the render-only core, exposed in
// namespace lamp so apply_shade_colors.hpp and apply_base_colors.hpp can
// call them without pulling in the rest of this file. Callers handle the
// bookkeeping (configurator timestamps + section invalidate).
namespace lamp {
void renderShadeColors(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<lamp::Color> colors = jsonArrayToColors(arr);
  std::vector<lamp::Color> gradient =
      lamp::buildGradientWithStops(shade.pixelCount, colors);
  // beginFade keeps the color-picker's ~250ms ease. On a rapid write mid-fade
  // it re-anchors fade-from to the in-progress lerp value, so drags
  // rubber-band smoothly instead of snapping.
  shadeConfiguratorBehavior.beginFade(gradient, lamp::kDefaultFadeMs);
  lamp::overrides.shade.rebaseline(gradient);
  // Reflect the new shade in the BLE adv so phones and v1 neighbours see it
  // without connecting. Base carries its blended identity color; shade is a
  // single stop.
  const auto& baseStops = config.base.broadcastColors();
  bt.setAdvertisedColors(
      lampos::led::blendedIdentity(baseStops.data(), baseStops.size()),
      colors[0]);
  ::recomputeDrawAnchors();
}

// renderBaseColors: counterpart to renderShadeColors. As above, callers
// handle timestamps + invalidate.
void renderBaseColors(JsonArray arr) {
  if (arr.isNull() || arr.size() == 0) return;
  std::vector<lamp::Color> colors = jsonArrayToColors(arr);
  std::vector<lamp::Color> gradient =
      lamp::buildGradientWithStops(base.pixelCount, colors);
  // See renderShadeColors: the fade-snapshot-from-buffer flicker on knockout
  // pixels is handled inside ConfiguratorBehavior::beginFade (it snapshots
  // from `colors` / in-progress lerp, not fb->buffer). No mid-fade guard
  // needed here.
  baseConfiguratorBehavior.beginFade(gradient, lamp::kDefaultFadeMs);
  lamp::overrides.base.rebaseline(gradient);
  // Reflect the new base in the BLE adv as its blended identity color.
  bt.setAdvertisedColors(
      lampos::led::blendedIdentity(colors.data(), colors.size()),
      config.shade.broadcastColors()[0]);
  ::recomputeDrawAnchors();
}
}  // namespace lamp

namespace lamp {
// Internal helper for the apply_expressions.hpp split. mutateConfig=true is
// the user-source path (BLE expressionOp drain): drives expressionManager
// AND mirrors into config.expressions so persistConfig() captures it.
// mutateConfig=false is the remote-source path (applyRemoteOpLocal for a
// cascade-relayed CONTROL_OP): drives expressionManager only so the
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
      // but ignored: it's a pure type-property on the Expression subclass,
      // not config-driven.
      if (key == "type" || key == "enabled" ||
          key == "disabledDuringWispOverride" ||
          key == "intervalMin" || key == "intervalMax" ||
          key == "target" || key == "colors") continue;
      JsonVariant v = kv.value();
      if (v.is<uint32_t>()) cfg.setParameter(key, v.as<uint32_t>());
      else if (v.is<int>()) cfg.setParameter(key, static_cast<uint32_t>(v.as<int>()));
    }
    // Store the JsonArray in a local so iteration doesn't reference a
    // temporary destroyed at the end of the full expression (ArduinoJson
    // 7.4.x tightened lifetime semantics on chained calls).
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
  // Callers invalidate the expressions section themselves so the drain block
  // always marks dirty, even on a parse failure where nothing changed.
}
}  // namespace lamp


extern void lamp_register_panic_handler();



#if defined(ARDUINO) || defined(ESP_PLATFORM)
namespace lamp {

void updateAdvertisedDeviceName(const char* newName) {
  // Update the GAP device name AND rebuild the active advertisement payload
  // so the new name surfaces to mid-scan phones without a reboot. NimBLE
  // caches the advertisement frame; the stop/start pair forces it to re-emit
  // with the freshly-set device name on the next advertising tick. Existing
  // GATT connections are unaffected (advertising and connections are
  // separate state machines in NimBLE). The brief gap (~50ms, well under the
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
  if (pixel < ::config.base.sumPx() && brightness <= 100) {
    ::baseKnockoutBehavior.knockoutPixels[pixel] = brightness;
    ::config.base.knockoutPixels[pixel] = brightness;
    // Live per-pixel knockout; does NOT invalidate the base section cache.
    // See drainBrightness in lamp_drains.cpp: CHAR_COMMIT invalidates the
    // cache, not per-pixel knockout writes.
  }
}

}  // namespace lamp


static void onWifiStateChanged() {
  // This callback fires from Arduino-ESP32's WiFi event task, NOT Core 1.
  // Calling into compositor.setHomeMode / setAllStripsBrightness from
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

lamp::FrameBuffer* lamp::Lamp::shadeFb() { return &::shade; }
lamp::FrameBuffer* lamp::Lamp::baseFb()  { return &::base; }

void lamp::Lamp::recomputeEffectiveCeiling() {
  s_hwMaxBrightness =
      lamp::effectiveCeiling(hw_.maxBrightness, ::config.lamp.brightnessCeiling);
}

// Per-lamp seed for the first-boot color roll. The efuse base MAC is unique
// per unit and readable this early in boot, before wifi::begin() brings RF up
// and makes esp_random() actually random; seeding from esp_random() here
// would hand every lamp the same value and roll every lamp the same color.
static uint32_t seedFromMac() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  return (uint32_t(mac[2]) << 24) | (uint32_t(mac[3]) << 16) |
         (uint32_t(mac[4]) << 8) | uint32_t(mac[5]);
}

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
  // Seed the file-scope brightness cap from the variant's HwConfig narrowed by
  // the user ceiling so free functions (applyEffectiveBrightness,
  // applyRemoteOpLocal) use the correct ceiling without importing a variant header.
  recomputeEffectiveCeiling();

  // `config` as an unqualified name inside a Lamp member function resolves to
  // Lamp::config() (the accessor method). Bring the file-scope global into
  // scope as a local reference so the method body can use `config` unqualified
  // the same way all the free functions and apply-helpers do.
  lamp::Config& config = ::config;
#ifdef LAMP_DEBUG
  Serial.begin(115200);
#endif
  if (esp_reset_reason() == ESP_RST_BROWNOUT) {
    Serial.println(
        "[power] brownout reset — supply budget constants may be mis-sized");
  }
  lamp_register_panic_handler();

  config = lamp::Config(&configStore);
  // Re-populate the in-memory lampType from NVS. Config::Config loads the
  // JSON blob, but lampType lives under its own NVS key.
  config.loadLampType();

  // Apply subclass defaults AFTER NVS load. Fields that NVS left at their
  // factory value (e.g. name="stray", colors empty) get the subclass's
  // preferred first-boot value. Fields the user has already saved in NVS are
  // NOT touched; the NVS value is authoritative.
  config.applyDefaults(defaults());

  // First-ever-boot housekeeping, persisted once so it sticks across reboots.
  // Migrate a lamp configured under firmware that predated the `setup` flag
  // (custom name OR a control password) onto the flag first, so the roll below
  // sees it as adopted. Then, only while still unadopted, give it a random
  // per-surface hue so units aren't visually identical out of the box. Shade
  // hue is offset from base by [60,300]° so a lamp never boots base≈shade;
  // seeded from the MAC so each unit lands on a distinct hue. Guarded on
  // `!setup` and the single-color variant default, so it fires once and never
  // on an adopted lamp; a variant that forces `setup=true` (curated colors)
  // skips it entirely.
  bool persistFirstBoot = false;
  if (!config.lamp.setup &&
      (config.lamp.name != defaults().name || !config.lamp.password.empty())) {
    config.lamp.setup = true;
    persistFirstBoot = true;
  }
  if (!config.lamp.setup) {
    const Color baseDefault = hexStringToColor(defaults().baseColor.c_str());
    const Color shadeDefault = hexStringToColor(defaults().shadeColor.c_str());
    const bool colorsUnconfigured =
        config.base.broadcastColors().size() == 1 &&
        config.base.broadcastColors()[0] == baseDefault &&
        config.shade.broadcastColors().size() == 1 &&
        config.shade.broadcastColors()[0] == shadeDefault;
    if (colorsUnconfigured) {
      FastRng rng(seedFromMac());
      int baseHue = rng.range(0, 359);
      int shadeHue = (baseHue + rng.range(60, 300)) % 360;
      config.base.broadcastColors() = {colorFromHue(baseHue)};
      config.shade.broadcastColors() = {colorFromHue(shadeHue)};
      config.base.ac = 0;
      persistFirstBoot = true;
    }
  }
  if (persistFirstBoot) config.persistConfig("first-boot-init");

  // Space the radio bring-ups so their init inrush spikes don't stack into one
  // brownout-tripping surge on marginal USB power. Sub-100ms total, invisible
  // against a multi-second boot.
  constexpr uint32_t kRadioStaggerMs = 40;
  delay(kRadioStaggerMs);
  wifi::begin();
  wifi::setStateChangeCallback(onWifiStateChanged);
  wifi::setHomeModeEnabledGetter([]() {
    return config.homeMode.enabled
        && config.homeMode.networkBound
        && !config.homeMode.ssid.empty();
  });
  // Suspend WiFi background scans while OTA is in flight: the scan hops the
  // radio across 11+ channels for ~5s and silently drops ESP-NOW unicast
  // (both OFFER→lamp AND lamp→wisp ACCEPT/REQ) during that window.
  wifi::setOtaInProgressGetter([]() { return firmwareReceiver.isInProgress(); });

  delay(kRadioStaggerMs);
  const auto& baseStops = config.base.broadcastColors();
  bt.begin(config.lamp.name,
           lampos::led::blendedIdentity(baseStops.data(), baseStops.size()),
           config.shade.broadcastColors()[0], config.lamp.setup);

#if LAMP_WEBAPP_ENABLED
  if (lamp::any(featuresEnabled(), lamp::Features::WebApp) && config.lamp.webappEnabled) {
    webapp::begin(config);
    lamp::logHeap("webapp");
  }
#endif

  // Build role FrameBuffers. Each role groups its hw_.strips in declaration
  // order into one composite buffer: per strip a NeoPixel driver (pin +
  // byte order from HwConfig) and a StripSegment at the running offset. A
  // strip pixelCount of 0 resolves from the role's Config px (core
  // single-strip roles, seeded first-boot from defaults()::{base,shade}Px);
  // a custom variant sets an explicit per-strip count.
  auto buildRole = [&](lamp::Surface role, uint8_t configPx,
                       const std::vector<lamp::Color>& colors,
                       const std::string& configByteOrder,
                       lamp::FrameBuffer& fb) {
    // Config byteOrder overrides the StripSpec default for every strip in the
    // role; empty or unrecognized falls back to the compile-time default.
    std::vector<lamp::StripSegment> segments;
    uint16_t offset = 0;
    for (const auto& s : hw_.strips) {
      if (s.role != role) continue;
      const uint16_t px = s.pixelCount ? s.pixelCount : configPx;
      lamp::ByteOrder order = s.byteOrder;
      byteOrderFromString(configByteOrder.c_str(), order);
      auto* driver =
          new Adafruit_NeoPixel(px, s.pin, lampos::led::neoPixelFormat(order) + NEO_KHZ800);
      segments.push_back({driver, s.name, offset, px, s.reversed});
      offset += px;
    }
    fb.begin(lamp::buildGradientWithStops(offset, colors), std::move(segments));
  };
  buildRole(lamp::Surface::Shade, config.shade.sumPx(), config.shade.broadcastColors(), config.shade.byteOrder, shade);
  buildRole(lamp::Surface::Base,  config.base.sumPx(),  config.base.broadcastColors(),  config.base.byteOrder,  base);

  // Governor sense inputs: per-role transmitted channel count resolved with
  // the same config-over-StripSpec byteOrder fallback buildRole uses.
  auto roleChannels = [&](lamp::Surface role,
                          const std::string& configByteOrder) -> uint8_t {
    for (const auto& s : hw_.strips) {
      if (s.role != role) continue;
      lamp::ByteOrder order = s.byteOrder;
      byteOrderFromString(configByteOrder.c_str(), order);
      return lampos::led::activeChannelCount(order);
    }
    return 3;
  };
  s_shadeChannels = roleChannels(lamp::Surface::Shade, config.shade.byteOrder);
  s_baseChannels = roleChannels(lamp::Surface::Base, config.base.byteOrder);
  s_govPixelCount = static_cast<uint16_t>(shade.pixelCount) + base.pixelCount;
  s_powerGovernor.begin(hw_.supplyBudgetMa, millis());
  compositor.preFlushHook = &governFrame;
  ::recomputeDrawAnchors();

  // Cap every segment at maxBrightness before the first content flush, else a
  // strip runs at the NeoPixel full-bright default. Runs after the governor's
  // begin() so the boot-hold ceiling reaches the drivers here.
  applyEffectiveBrightness();

  initBehaviors(featuresEnabled(), *this);

  // Give the subclass a chance to register any extra behaviors not covered
  // by the Feature flags. StandardLamp's createBehaviors is empty since all
  // its behaviors are Feature-flag driven.
  {
    BehaviorStackBuilder builder;
    createBehaviors(builder);
    for (auto* b : builder.behaviors()) {
      compositor.addBehavior(b);
    }
  }
  bt.activateGattServices(&config);

  // Route inbound CONTROL_OP payloads (addressed to this lamp or broadcast)
  // into a pending slot. WiFi-task safe: pure memcpy under portMUX, no heap
  // work. MUST be installed BEFORE meshLink.begin(), or any CONTROL_OP
  // arriving in the gap is dropped because controlOpHandler_ is null.
  meshLink.setControlOpHandler(
      [](const uint8_t* payload, size_t len, const uint8_t srcMac[6]) {
        lamp::pendingSlots.inboundOp.post(
            pendingMux, reinterpret_cast<const char*>(payload), len, srcMac);
      });
  // Install the OTA receiver BEFORE meshLink.begin() so the chunk
  // fast-path is wired by the time MSG_FW_* frames start arriving.
  // FirmwareReceiver::begin captures myMac via meshLink.getMyMac(),
  // populated after the EspNowLink::begin() inside meshLink.begin(), so
  // receiver-side init has to run AFTER meshLink.begin(). The
  // setFirmwareReceiver() call can happen any time before the first
  // MSG_FW_OFFER arrives; installing it here keeps the wiring co-located.
  meshLink.setFirmwareReceiver(&firmwareReceiver);
  // Bring up ESP-NOW grid presence (HELLO + COLORS). Independent of home
  // WiFi; runs on whatever channel the radio is on. See lamp_protocol.hpp.
  meshLink.begin(&config);
  uint8_t ownMac[6];
  meshLink.getMyMac(ownMac);
  char ownMacStr[18];
  lamp::formatBdAddr(ownMac, ownMacStr);
  config.setLampId(ownMacStr);
  lamp::logHeap("mesh");
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

  // Gossip OTA: the lamp can ALSO originate offers to peers it meets via the
  // social system. Wire the distributor through the same
  // EspNowFirmwareTransport (it emits MSG_FW_OFFER/CHUNK/DONE and listens for
  // ACCEPT/REQ/RESULT via mesh_link's dispatch ladder).
  lamp::firmwareDistributor.begin(&meshFwTransport);
  meshLink.setFirmwareDistributor(&lamp::firmwareDistributor);
  // FS-image OTA: a second receiver/distributor pair targeting the spiffs
  // partition (shares the mesh transport; cross-OTA guard prevents overlap).
  fs_ota::begin(&meshFwTransport, &firmwareReceiver, &lamp::firmwareDistributor);

  // Launch the one streaming task shared by both distributors, now that both
  // have registered.
  lamp::FirmwareDistributor::startSharedStreaming();

  // Arm the post-OTA self-health timer. esp_ota_get_state_partition returns
  // the ota-image-state of the running partition. A freshly-OTA'd image
  // boots in ESP_OTA_IMG_PENDING_VERIFY, and the bootloader auto-rollbacks on
  // the next reset unless the partition is explicitly marked valid. The lamp
  // gets 30 seconds of steady-state runtime before being declared healthy,
  // long enough to surface boot-time crashes that would justify a rollback.
  //
  // Without this, the SECOND OTA permanently fails (esp_ota_begin returns
  // ESP_ERR_OTA_ROLLBACK_INVALID_STATE).
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
  // remote cascade arrives for an expression type not configured on this
  // lamp, so receivers don't need to pre-configure every type.
  expressionManager.setCompositor(&compositor);
  // Expression mirror: replay a warm peer's triggered expression locally.
  expressionObserverRegistry.registerObserver(&socialEchoObserver);
  // One-shot reservation so the loop-task drain never reallocates mid-frame.
  pendingTriggers.reserve(MAX_PENDING_TRIGGERS);
  lamp::logHeap("boot");
}

// Drain order matters: the per-slot drains below run in a fixed sequence
// each loop tick on Core 1. Invariants to preserve when reordering:
//
//   * drainExpressionOp must run BEFORE drainSettingsBlob: settings_blob
//     re-applies the canonical config snapshot, and the just-arrived
//     expression edits must already be mirrored into config.expressions
//     before that dispatch.
//
//   * drainCommit runs AFTER all live-preview drains (brightness,
//     shadeColors, baseColors, knockout, expressionOp): those mutate
//     config in RAM and the commit hash-dedups against the resulting
//     snapshot. It runs BEFORE the section-cache push at the tail
//     (ble_control::tick) so a successful commit's invalidateAllSections
//     lands in the same tick the app gets notified.
//
//   * drainSocialDispositions runs AFTER drainSettingsBlob so both NVS
//     writers serialise against the shared config store on this core.
//
//   * Transient-override drains (overrideColors / restoreColors /
//     overrideBrightness / restoreBrightness) run BEFORE the override
//     state-machine tick at the tail so any newly-armed fade animates
//     starting on the same tick it arrived.
//
// See each drain helper's body for the per-slot detail.

// Compositor pre-flush hook, Core 1. Prices the frame both surfaces are
// about to push; a clamp re-mins the drivers here so the over-budget frame
// never reaches the strip.
static void governFrame() {
  const uint32_t nowMs = millis();
  const float fullDuty = lamp::fullDutyMa(shade.buffer, s_shadeChannels) +
                         lamp::fullDutyMa(base.buffer, s_baseChannels);
  bool radioBusy = firmwareReceiver.isInProgress() ||
                   lamp::firmwareDistributor.isInProgress() ||
                   lamp::ota_quiet_mode::isQuiet() ||
                   ble_control::isClientConnected();
#if LAMP_WEBAPP_ENABLED
  radioBusy = radioBusy || webapp::hasClient();
#endif
#ifdef LAMP_DEBUG
  const auto prevGovState = s_powerGovernor.state();
#endif
  const bool clamped =
      s_powerGovernor.senseFrame(nowMs, fullDuty, lamp::requestedStripLevel(),
                                 s_govPixelCount, radioBusy);
  if (clamped) {
    lamp::setAllStripsBrightness(lamp::requestedStripLevel());
  }
#ifdef LAMP_DEBUG
  if (clamped && prevGovState != lamp::PowerGovernor::State::Clamped) {
    Serial.printf("[gov] clamp demand=%.0f budget=%u ceiling=%u\n",
                  s_powerGovernor.lastDemandMa(),
                  (unsigned)s_powerGovernor.pixelBudgetMa(),
                  (unsigned)s_powerGovernor.ceiling(nowMs));
  }
#endif
}

void lamp::Lamp::tick() {
  // `config` local reference so the bt.tickAdvertising / maybeFlushDispositions
  // / flushDispositionsNow calls below can use it unqualified. Drain helpers
  // each re-bind locally where they need it.
  lamp::Config& config = ::config;

#ifdef LAMP_DEBUG
  pollSerialCommands();
  {
    static uint32_t s_nextStackLogMs = 0;
    const uint32_t nowMs = millis();
    if (static_cast<int32_t>(nowMs - s_nextStackLogMs) >= 0) {
      s_nextStackLogMs = nowMs + 10000;
      Serial.printf("[loop] stack HWM: %u bytes free\n",
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    }
  }
#endif

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
    // (cmd 0x02) all funnel here: refresh the compositor homeMode gate and
    // the strip brightness together.
    // Held off during OTA quiet-mode so the crowd-dim micro-fade math
    // doesn't snap-apply 5 minutes of accumulated wall-clock drift in one
    // frame when quiet-mode exits. The pending flag stays set; it drains on
    // the next non-quiet tick.
    reapplyHomeModeState();
  }

  // Debounced disposition commit: runs the actual NVS write once the user
  // has been idle for kDispositionFlushIdleMs (5s). Cheap when nothing is
  // dirty (single bool check + uint32_t subtraction).
  config.maybeFlushDispositions(millis());
  if (pendingFlushDispositionsRequested) {
    // Phone disconnected (set on Core 0 in ble_control's onDisconnect).
    // Force-commit so the user's final slider value survives even if
    // power is yanked before the next 5s idle window would fire.
    pendingFlushDispositionsRequested = false;
    config.flushDispositionsNow();
  }

  drainBrightness();
  drainEditSession();

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
  drainWispClaim();
  drainWispPaint();
  drainWispOp();
  drainWispStatus();
  drainCommand();
  drainColorQuery();
  drainColorInfo();
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
    // brightness override is active (applyEffectiveBrightness's
    // change-callback path owns the strip while a wisp override is
    // animating; without this gate the two would race every frame).
    //
    // During the 80ms fade window this path writes `level` directly and
    // intentionally bypasses both the crowd-dim factor and the OTA pulse
    // multiplier. Slider drags need to land instantly on the commanded
    // value; layering crowd-dim or OTA pulse mid-drag would make the slider
    // feel laggy and disconnected. Once the window elapses the next
    // applyEffectiveBrightness call reapplies both multipliers cleanly.
    if (!lamp::overrides.brightness.isActive() && lamp::brightnessFadeSeeded() &&
        lamp::brightnessFadeSource() != lamp::brightnessFadeTarget()) {
      const uint8_t level = computeUserBrightnessNow(now);
      lamp::setAllStripsBrightness(
          lamp::calculateBrightnessLevel(s_hwMaxBrightness, level));
      if (level == lamp::brightnessFadeTarget()) {
        lamp::settleBrightnessFade();
      }
    }

    // Personality engine. Runs after the transient-override block so it
    // reads a consistent post-fade view this tick. Cheap when nothing's
    // changed (1 Hz internal cadence). consumePendingApply() trips the
    // applyEffectiveBrightness pump when the crowd-dim factor crosses the
    // deadband, or when SocialMode changes the dim regime.
    lamp::personalityEngine.tick(now);
    if (lamp::personalityEngine.consumePendingApply()) {
      pendingApplyEffectiveBrightness = true;
    }
  }

  // Fire any delayed triggerExpression invocations whose deadline has passed.
  // Bounded queue (MAX_PENDING_TRIGGERS) keeps this O(1) amortised; ordering
  // is INSERTION-order, not deadline-order: if a short-delayMs invocation
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

  socialEchoObserver.tick(millis());

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
  // Post-OTA self-health check. After 30 seconds of steady-state loop
  // iteration, if BLE is up and the loop has been iterating (trivially true
  // inside it), call mark_app_valid so the next OTA can succeed. If the lamp
  // crashes before the deadline, the bootloader auto-rollbacks on the next
  // reset.
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
      g_pendingVerify = false;  // one-shot: even on failure don't retry
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

  // Demand sensing is per frame in governFrame (compositor pre-flush hook);
  // this block only advances the boot ramp and the paced release, plus the
  // pump check every loop tick so the 400 ms ceiling glide isn't quantized
  // to the 1 s pace.
  {
    const uint32_t nowMs = millis();
#ifdef LAMP_DEBUG
    const auto prevGovState = s_powerGovernor.state();
#endif
    s_powerGovernor.tick(nowMs);
#ifdef LAMP_DEBUG
    if (prevGovState == lamp::PowerGovernor::State::Clamped &&
        s_powerGovernor.state() != lamp::PowerGovernor::State::Clamped) {
      Serial.printf("[gov] release demand=%.0f\n",
                    s_powerGovernor.lastDemandMa());
    }
#endif
    if (s_powerGovernor.consumePendingApply()) {
      if (ota_quiet_mode::isQuiet()) {
        // pendingApplyEffectiveBrightness is held off during quiet mode (see
        // the drain at the top of tick); a direct re-min applies the new
        // ceiling without a baseline recompute or crowd-dim snap, so the
        // governor still protects the flash-write window.
        lamp::setAllStripsBrightness(lamp::requestedStripLevel());
      } else {
        pendingApplyEffectiveBrightness = true;
      }
    }
  }

  // Reap transient one-shot expressions (created by triggerInvocation when
  // a remote cascade arrived) whose animations have finished. AFTER tick so
  // the final frame of the animation is drawn before removal.
  expressionManager.gcTransients();

  // Reap the active-test set (entries fired via dispatchLampAction
  // "test_expression"). When the last one transitions to STOPPED, flip
  // CHAR_STATE_NOTIFY previewActive=false so the app's Test button
  // re-enables. AFTER tick so the just-completed STOPPED state is read.
  if (expressionManager.reapCompletedTests()) {
    ble_control::notifyStateChange();
  }
}

// Per-slot tick() drain helpers: definitions live in core/lamp_drains.cpp.
// Method declarations are on the Lamp class in core/lamp.hpp. Shared
// file-scope state (pendingMux, pendingKnockout, configurator behaviors,
// etc.) is wired across the two TUs via core/lamp_internal.hpp.

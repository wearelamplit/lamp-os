// core/lamp_drains.cpp — Lamp class drain-method definitions.
//
// Split out from core/lamp.cpp purely for readability — lamp.cpp was
// over 2000 lines mostly because of these. The drain bodies are
// otherwise unchanged from their inline form; the call site
// (Lamp::tick) lives in lamp.cpp.
//
// Each drain reads one pending slot and applies the payload to lamp
// state. Drain order + invariants are documented above Lamp::tick().

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>
#include <string>

#include "behaviors/configurator.hpp"
#include "behaviors/fade_out.hpp"
#include "behaviors/social.hpp"
#include "core/personality_engine.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_base_knockout.hpp"
#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_settings_blob.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/fs_ota.hpp"
#include "components/network/ble_control.hpp"
#include "components/network/nearby_lamps.hpp"
#include "components/network/show_receiver.hpp"
#include "components/network/wifi.hpp"
#include "config/config.hpp"
#include "core/ota_quiet_mode.hpp"
#include "core/override_aggregate.hpp"
#include "core/pending_slot_aggregate.hpp"

#ifdef LAMP_DEBUG
// shadeSocialBehavior lives at file scope in lamp.cpp (not the lamp
// namespace). drainWispOp reaches into it on the LAMP_DEBUG testGreet
// branch to force a greeting waveform from the bench without waiting for
// a peer to walk in/out of BLE range.
extern lamp::SocialBehavior shadeSocialBehavior;
#endif

namespace lamp {

// Forward declaration of the helper that posts a wispStatus payload into
// the pending slot. Defined in lamp.cpp at file scope as `static` — see
// the corresponding call site (`applyRemoteOpLocal`) for the rationale.
// Drains don't call it directly; it's referenced indirectly via the
// applyRemoteOpRouted helper that's declared in lamp_internal.hpp.

namespace {

// CHAR_COMMIT drain TU-local state — only drainCommit reads or writes
// these. Moved here with the drain so lamp.cpp's anonymous namespace
// stays focused on the rest of its file-scope helpers.
bool      commitDirty = false;
uint32_t  lastCommitSignalMs = 0;
uint32_t  lastPersistedHash = 0;  // FNV-1a of last successfully persisted serialized JSON
constexpr uint32_t kCommitFlushIdleMs = 1500;

uint32_t fnv1aHash(const String& s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.length(); ++i) {
    h ^= static_cast<uint8_t>(s[i]);
    h *= 16777619u;
  }
  return h;
}

}  // namespace

// =============================================================================
// Per-slot tick() drain helpers
// =============================================================================
//
// One method per pending slot drained on Core 1. Bodies were extracted
// verbatim from the original inline tick() blocks — see the invariant
// comment above tick() for the ordering contract between them.

// Drain pendingBrightness (CHAR_BRIGHTNESS live-preview write). Updates the
// in-memory effective brightness ONLY — never invalidates the section cache.
// See the architectural-invariant block inside for the section-cache
// rationale.
void Lamp::drainBrightness() {
  if (pendingBrightness >= 0) {
    uint8_t level = static_cast<uint8_t>(pendingBrightness);
    pendingBrightness = -1;
#ifdef LAMP_DEBUG
    Serial.printf("[drain] brightness=%u t_us=%lu home_focus=%d\n",
                  (unsigned)level, (unsigned long)micros(),
                  (int)ble_control::isHomeModePageActive());
#endif
    lamp::apply::brightnessToConfig(level, ble_control::isHomeModePageActive(), hw_.maxBrightness);
    // Invariant: live-preview drains update in-memory config but DO NOT
    // invalidate the section cache. Section JSON represents the persisted
    // snapshot; only the commit / settings_blob persist paths invalidate.
    // Mirror this in drainShadeColors, drainBaseColors, drainKnockout.
  }
}

// Drain pendingSlots.shadeColors (CHAR_SHADE_COLORS live-preview write).
// User-source mutation of config.shade.colors — preview only, no NVS write.
void Lamp::drainShadeColors() {
  if (lamp::pendingSlots.shadeColors.valid) {
    char buf[lamp::kPendingJsonBase + 1];
    uint16_t len = lamp::pendingSlots.shadeColors.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[drain] shadeColors len=%u t_us=%lu\n",
                  (unsigned)len, (unsigned long)micros());
#else
    (void)len;
#endif

    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
      // User-source: ToConfig mutates config.shade.colors so a subsequent
      // CHAR_COMMIT can persist the user's color choice.
      lamp::apply::shadeColorsToConfig(doc.as<JsonArray>());
    }
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    // Live preview only — does NOT invalidate the shade section cache.
    // See the architectural invariant comment at the brightness drain.
  }
}

// Drain pendingSlots.baseColors (CHAR_BASE_COLORS live-preview write).
// User-source mutation of config.base.colors — preview only, no NVS write.
void Lamp::drainBaseColors() {
  if (lamp::pendingSlots.baseColors.valid) {
    char buf[lamp::kPendingJsonBase + 1];
    uint16_t len = lamp::pendingSlots.baseColors.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[drain] baseColors len=%u t_us=%lu\n",
                  (unsigned)len, (unsigned long)micros());
#else
    (void)len;
#endif

    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
      // User-source: ToConfig mutates config.base.colors so a subsequent
      // CHAR_COMMIT can persist the user's color choice.
      lamp::apply::baseColorsToConfig(doc.as<JsonArray>());
    }
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    // Live preview only — does NOT invalidate the base section cache.
    // See the architectural invariant comment at the brightness drain.
  }
}

// Drain the single-pixel knockout slot (debug/test path that dims one LED).
// Preview only — does not touch NVS.
void Lamp::drainKnockout() {
  if (pendingKnockout.valid) {
    uint8_t pixel, brightness;
    portENTER_CRITICAL(&pendingMux);
    pixel = pendingKnockout.pixel;
    brightness = pendingKnockout.brightness;
    pendingKnockout.valid = false;
    portEXIT_CRITICAL(&pendingMux);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain knockout pixel=%u brightness=%u\n", pixel, brightness);
#endif
    lamp::applyKnockoutPixel(pixel, brightness);
  }
}

// Drain pendingSlots.expressionOp (Update/Add/Delete from the expression
// editor). Mirrors the change into config.expressions AND persists to NVS
// immediately — must run BEFORE drainSettingsBlob and BEFORE drainCommit so
// the commit-time hash sees the new snapshot.
void Lamp::drainExpressionOp() {
  lamp::Config& config = ::config;
  if (lamp::pendingSlots.expressionOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.expressionOp.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain expressionOp len=%u\n", (unsigned)len);
#else
    (void)len;
#endif

    JsonDocument doc;
    bool applied = false;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
      lamp::apply::expressionOpToConfig(doc.as<JsonObject>());
      applied = true;
    }
    config.invalidateExpressionsSection();

    // Immediate NVS persist after the runtime mutation. Diverges from the
    // architectural invariant documented at the brightness drain above
    // ("live-preview drains do NOT write NVS — only settings_blob does")
    // because the expression editor UX has no global Save step: the user
    // taps Update/Add/Delete and expects the change to survive a reboot,
    // matching the setup-service UX. expressionOpToConfig has already
    // mirrored the change into config.expressions, so config.asJsonDocument()
    // serializes the new authoritative state — no merge needed, no reboot
    // needed. (If we later extend this pattern to baseColors/shadeColors/
    // brightness, those will need a debounce because slider drags can fire
    // ~4 BLE writes/sec and we don't want one NVS write per drag tick.)
    if (applied) {
      config.persistConfig("expressionOp");
    }
  }
}

// CHAR_COMMIT drain. Debounced 1500 ms after the last commit signal, with
// hash-dedup against the last persisted snapshot and a force-flush path for
// BLE disconnect. Must run AFTER all live-preview drains so the snapshot it
// hashes is current; runs BEFORE the section-cache push at the tail.
void Lamp::drainCommit() {
  lamp::Config& config = ::config;
  if (lamp::pendingSlots.pendingCommit) {
    lamp::pendingSlots.pendingCommit = false;
    commitDirty = true;
    lastCommitSignalMs = millis();
  }
  // If a disconnect set forceCommitFlush but no commit was pending,
  // discard the stale flag so the next commit's debounce window isn't
  // bypassed by leftover state from a previous BLE session.
  if (!commitDirty) {
    lamp::pendingSlots.forceCommitFlush = false;
  }
  if (commitDirty &&
      (lamp::pendingSlots.forceCommitFlush ||
       (millis() - lastCommitSignalMs) >= kCommitFlushIdleMs)) {
    lamp::pendingSlots.forceCommitFlush = false;
    if (firmwareReceiver.isInProgress()) {
      // Defer — recheck next tick. Don't clear commitDirty.
#ifdef LAMP_DEBUG
      Serial.println("[loop] commit drain: OTA in progress, deferred");
#endif
    } else {
      JsonDocument doc = config.asJsonDocument();
      String serialized;
      serializeJson(doc, serialized);
      uint32_t hash = fnv1aHash(serialized);
      if (hash == lastPersistedHash) {
#ifdef LAMP_DEBUG
        Serial.println("[loop] commit drain: hash-dedup skip");
#endif
        commitDirty = false;
      } else {
        bool persisted = config.persistConfig("commit");
        if (persisted) {
          lastPersistedHash = hash;
          config.invalidateAllSections();
          commitDirty = false;
        }
        // On persist failure, commitDirty stays set so the next tick
        // retries. No app-side surfacing — the previous notify path was
        // never plumbed past a debugPrint.
      }
    }
  }
}

// settings_blob drain — bulk apply of a JSON config blob (factory reset,
// per-section dispatch, opt-in reboot). Runs AFTER drainExpressionOp so any
// just-arrived expression edits are already mirrored before dispatch. OTA-
// in-progress guard discards the write rather than racing chunk-writes.
void Lamp::drainSettingsBlob() {
  lamp::Config& config = ::config;
  if (lamp::pendingSlots.settingsBlob.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.settingsBlob.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain settingsBlob len=%u\n", (unsigned)len);
#endif

    JsonDocument incomingDoc;
    if (deserializeJson(incomingDoc, buf, len) != DeserializationError::Ok) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] settingsBlob: incoming JSON parse failed\n");
#endif
    } else if (incomingDoc["factoryReset"].as<bool>()) {
      // Factory reset sentinel — wipe NVS + reboot, bypass the apply
      // orchestrator entirely.
      // The co-shipping warning is UNCONDITIONAL (not LAMP_DEBUG-gated):
      // this is forward defense against an app that accidentally bundles
      // factoryReset with other fields. The app guard makes it impossible
      // in normal flow, but fleet logs must surface the unexpected case.
      if (incomingDoc.as<JsonObject>().size() > 1) {
        Serial.println(
            "[loop] settingsBlob WARNING: factoryReset co-shipped with "
            "other keys — those keys will be silently dropped.");
      }
#ifdef LAMP_DEBUG
      Serial.println("[loop] settingsBlob: factoryReset sentinel, wiping NVS");
#endif
      if (!prefs.begin("lamp", false)) {
#ifdef LAMP_DEBUG
        Serial.println("[nvs] prefs.begin failed (factory reset)");
#endif
      } else {
        bool cleared = prefs.clear();
        prefs.end();
        if (cleared) {
          ble_control::notifyStateChange();
          lamp::fadeOutRebootRequested = true;
        }
      }
    } else if (firmwareReceiver.isInProgress()) {
      // OTA in progress — a NVS write here would compete with the OTA
      // chunk-write subsystem. Discard the blob; app will re-issue
      // when OTA finishes.
#ifdef LAMP_DEBUG
      Serial.println(
          "[loop] settingsBlob: OTA in progress, discarding write");
#endif
    } else {
      bool wantsReboot = lamp::apply::settingsBlobLocal(incomingDoc.as<JsonObject>(), hw_.maxBrightness);
      bool persisted = config.persistConfig("settings_blob");
      config.invalidateAllSections();
      ble_control::notifyStateChange();
      if (wantsReboot && persisted) {
        lamp::fadeOutRebootRequested = true;
      } else if (wantsReboot && !persisted) {
#ifdef LAMP_DEBUG
        Serial.println(
            "[loop] settingsBlob: persist failed; skipping reboot to avoid "
            "rebooting into a half-applied config");
#endif
      }
    }
  }
}

// Disposition map writes — drained AFTER settings_blob so both writers
// serialise against the shared `prefs` instance on this core. The BLE
// callback only memcpys into the pending slot (Core 0); persistence +
// map rebuild happen here on Core 1. No reboot, no auth re-check — the
// BLE callback already verified isAuthed before posting.
void Lamp::drainSocialDispositions() {
  lamp::Config& config = ::config;
  if (lamp::pendingSlots.socialDispositions.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.socialDispositions.drain(pendingMux, buf);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain socialDispositions len=%u\n", (unsigned)len);
#endif
    config.setDispositionsFromJson(buf, len);
  }
}

// Drain pendingSlots.testAction — manual triggers from the test panel.
// Translates either a JSON action or a raw expression-type string into the
// dispatchLampAction router.
void Lamp::drainTestAction() {
  if (lamp::pendingSlots.testAction.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.testAction.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain testAction len=%u\n", (unsigned)len);
#endif

    JsonDocument doc;
    if (len == 0) {
      doc["a"] = "test_expression_complete";
      dispatchLampAction(doc, millis());
    } else {
      DeserializationError err = deserializeJson(doc, buf, len);
      const char* action = err ? nullptr : doc["a"].as<const char*>();
      if (action && *action) {
        dispatchLampAction(doc, millis());
      } else {
        doc.clear();
        std::string value(buf, len);
        if (value == "complete") {
          doc["a"] = "test_expression_complete";
        } else {
          doc["a"] = "test_expression";
          doc["type"] = value;
        }
        dispatchLampAction(doc, millis());
      }
    }
  }
}

// Drain pendingSlots.wifiOp — currently scan-only (setHomeSsid/forget moved
// to the unified settings_blob path).
void Lamp::drainWifiOp() {
  if (lamp::pendingSlots.wifiOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.wifiOp.drain(pendingMux, buf);

#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain wifiOp len=%u\n", (unsigned)len);
#endif

    JsonDocument doc;
    if (deserializeJson(doc, buf) == DeserializationError::Ok) {
      const char* op = doc["op"].as<const char*>();
      if (op && strcmp(op, "scan") == 0) {
        wifi::startScan();
      }
      // setHomeSsid + forget moved to the unified draft + settings_blob
      // path. The app holds the SSID locally and persists it via the blob
      // along with everything else — wifiOp is now scan-only.
    }
  }
}

// Drain inbound ESP-NOW CONTROL_OP (deferred from ShowReceiver's WiFi task)
// — JSON parse + local dispatch via the unified cascade router.
void Lamp::drainInboundOp() {
  if (lamp::pendingSlots.inboundOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint8_t srcMac[6];
    uint16_t len = lamp::pendingSlots.inboundOp.drain(pendingMux, buf, srcMac);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain inboundOp len=%u\n", (unsigned)len);
#endif
    applyRemoteOpRouted(buf, len, srcMac, RemoteOpTransport::EspNow);
  }
}

// Drain BLE CHAR_REMOTE_OP writes through the same router — it decides
// apply-locally / forward / both from the payload's targetMac field. BLE
// has no real network source MAC; pass selfMac so app-driven rapid triggers
// coalesce against each other on the receive side.
void Lamp::drainRemoteOp() {
  if (lamp::pendingSlots.remoteOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.remoteOp.drain(pendingMux, buf);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain remoteOp len=%u\n", (unsigned)len);
#endif
    uint8_t selfMac[6];
    showReceiver.getMyMac(selfMac);
    applyRemoteOpRouted(buf, len, selfMac, RemoteOpTransport::BLE);
  }
}

// Drain pendingSlots.overrideColors — transient color override from wisp
// paint or app preview. Pins the wisp identity BEFORE apply() so the very
// first paint-triggered notifyWispStatus carries the truth instead of "{}".
void Lamp::drainOverrideColors() {
  {
    lamp::PendingOverrideColors cmd;
    if (lamp::pendingSlots.overrideColors.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain overrideColors surface=0x%02X n=%u fadeMs=%u\n",
                    (unsigned)cmd.surface, (unsigned)cmd.numColors,
                    (unsigned)cmd.fadeDurationMs);
#endif
      // OTA quiet mode wins: drop the override on the floor (drain
      // consumed the slot above so it doesn't pile up). The compositor
      // is exclusively painting the OTA indicator while quiet, so
      // applying a wisp/app paint here just queues a fade that we
      // can't render and would compete with the indicator the moment
      // quiet exits. Slot was a single-mailbox newest-wins anyway, so
      // dropping one frame of paint is the existing semantic.
      if (lamp::ota_quiet_mode::isQuiet()) {
#ifdef LAMP_DEBUG
        Serial.printf("[loop] drop overrideColors (ota quiet)\n");
#endif
        return;
      }
      // Pin the wisp identity BEFORE the apply() calls. apply() can
      // edge-trigger the wisp-active transition callback, which calls
      // notifyWispStatus → reads getWispStatusReadJson. If the cache is
      // still "{}" at that moment, the notify carries an empty payload
      // and the app shows "No wisp detected" until the next wisp hello
      // (≤30s) populates the cache. Setting mac/present here first
      // makes the very first paint-triggered notify carry the truth.
      //
      // Without this fix: on a freshly-joined lamp that hears
      // OVERRIDE_COLORS (unicast paint, no relay) but hasn't yet heard
      // a WISP_HELLO (gossip-broadcast, 30s heartbeat), the app sees
      // "No wisp detected" even though the lamp is actively wisp-
      // painted — the "bytes=0 puzzle" observed in the field.
      if (cmd.sourceKind == lamp_protocol::OverrideSource::Wisp) {
        lamp::nearbyLamps.cacheWispMacFromPaint(cmd.sourceMac);
      }
      // Surface routing.
      // - Base: colors → base only.
      // - Shade: colors → shade only.
      // - BaseAndShade: colors[0] → base, colors[1] → shade (paired wisp
      //   paint, halves ESP-NOW traffic vs the prior two-frame design).
      //   Must have numColors >= 2; if only 1 color arrived, treat it as
      //   the base colour and skip shade (defensive, shouldn't happen).
      if (cmd.surface == lamp_protocol::OverrideSurface::BaseAndShade) {
        lamp::overrides.base.apply(cmd.sourceMac, cmd.sourceKind,
                                &cmd.colors[0], /*numColors=*/1,
                                cmd.fadeDurationMs);
        if (cmd.numColors >= 2) {
          lamp::overrides.shade.apply(cmd.sourceMac, cmd.sourceKind,
                                   &cmd.colors[1], /*numColors=*/1,
                                   cmd.fadeDurationMs);
        }
      } else {
        if (cmd.surface == lamp_protocol::OverrideSurface::Base) {
          lamp::overrides.base.apply(cmd.sourceMac, cmd.sourceKind,
                                  cmd.colors, cmd.numColors,
                                  cmd.fadeDurationMs);
        }
        if (cmd.surface == lamp_protocol::OverrideSurface::Shade) {
          lamp::overrides.shade.apply(cmd.sourceMac, cmd.sourceKind,
                                   cmd.colors, cmd.numColors,
                                   cmd.fadeDurationMs);
        }
      }
      // Wisp paint ships Base + Shade 10 ms apart per peer but both land
      // in a single-slot mailbox — newest-writer-wins. When Core 1 lags
      // past 10 ms (BLE scan + render contention is enough), the Shade
      // frame drops the Base frame on the floor and Base's watchdog
      // expires after 60 s. Treat the surviving frame as proof of mesh
      // liveness on both surfaces. Only Wisp paints both surfaces in
      // a pair, so guard on Wisp.
      if (cmd.sourceKind == lamp_protocol::OverrideSource::Wisp) {
        const uint32_t now = millis();
        lamp::overrides.base.touchApply(now);
        lamp::overrides.shade.touchApply(now);
      }
    }
  }
}

// Drain pendingSlots.restoreColors — paired with drainOverrideColors;
// releases the transient override on the matching surface(s).
void Lamp::drainRestoreColors() {
  {
    lamp::PendingRestoreColors cmd;
    if (lamp::pendingSlots.restoreColors.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain restoreColors surface=0x%02X fadeMs=%u\n",
                    (unsigned)cmd.surface, (unsigned)cmd.fadeDurationMs);
#endif
      // BaseAndShade restores both surfaces in one frame (mirrors paint).
      if (cmd.surface == lamp_protocol::OverrideSurface::Base ||
          cmd.surface == lamp_protocol::OverrideSurface::BaseAndShade) {
        lamp::overrides.base.restore(cmd.sourceMac, cmd.sourceKind,
                                  cmd.fadeDurationMs);
      }
      if (cmd.surface == lamp_protocol::OverrideSurface::Shade ||
          cmd.surface == lamp_protocol::OverrideSurface::BaseAndShade) {
        lamp::overrides.shade.restore(cmd.sourceMac, cmd.sourceKind,
                                   cmd.fadeDurationMs);
      }
    }
  }
}

// Drain pendingSlots.overrideBrightness — transient brightness override
// (single global instance in v1; surface byte carried for forward compat).
void Lamp::drainOverrideBrightness() {
  {
    lamp::PendingOverrideBrightness cmd;
    if (lamp::pendingSlots.overrideBrightness.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain overrideBrightness b=%u fadeMs=%u\n",
                    (unsigned)cmd.brightness, (unsigned)cmd.fadeDurationMs);
#endif
      lamp::overrides.brightness.apply(cmd.sourceMac, cmd.sourceKind,
                               cmd.brightness, cmd.fadeDurationMs);
    }
  }
}

// Drain pendingSlots.restoreBrightness — paired with drainOverrideBrightness.
void Lamp::drainRestoreBrightness() {
  {
    lamp::PendingRestoreBrightness cmd;
    if (lamp::pendingSlots.restoreBrightness.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain restoreBrightness fadeMs=%u\n",
                    (unsigned)cmd.fadeDurationMs);
#endif
      lamp::overrides.brightness.restore(cmd.sourceMac, cmd.sourceKind,
                                 cmd.fadeDurationMs);
    }
  }
}

// Drain pendingSlots.wispHello — gossip-broadcast WISP_HELLO beacons. Caches
// version/flags/palette-id for the nearby-lamps view.
void Lamp::drainWispHello() {
  {
    lamp::PendingWispHello cmd;
    if (lamp::pendingSlots.wispHello.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain wispHello flags=0x%02X v=0x%08X\n",
                    (unsigned)cmd.flags, (unsigned)cmd.wispVersion);
#endif
      lamp::nearbyLamps.cacheWispHello(cmd.sourceMac, cmd.wispVersion, cmd.flags,
                                       cmd.paletteIdPrefix, cmd.carriedFwChannel,
                                       cmd.carriedFwVersion);
    }
  }
}

// Drain pendingSlots.wispPalette — manualPalette frames from a wisp. Updates
// the NearbyLamps cache and pushes a BLE notify so subscribed apps see the
// new palette without round-tripping a read of CHAR_WISP_STATUS.
void Lamp::drainWispPalette() {
  {
    // Wisp manualPalette drain. ShowReceiver's recv branch parsed +
    // deduped + posted; here on Core 1 we update the NearbyLamps cache
    // and push a BLE notify so subscribed apps see the new palette
    // without round-tripping a read of CHAR_WISP_STATUS.
    lamp::PendingWispPalette cmd;
    if (lamp::pendingSlots.wispPalette.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain wispPalette count=%u src=%02X:%02X:%02X:%02X:%02X:%02X\n",
                    (unsigned)cmd.count,
                    cmd.sourceMac[0], cmd.sourceMac[1], cmd.sourceMac[2],
                    cmd.sourceMac[3], cmd.sourceMac[4], cmd.sourceMac[5]);
#endif
      lamp::nearbyLamps.cacheWispPalette(cmd.sourceMac, cmd.rgb, cmd.count);
      ble_control::notifyWispStatus();
    }
  }
}

// Drain pendingSlots.wispOp — app wrote a wispOp via CHAR_WISP_OP; we
// broadcast it as MSG_CONTROL_OP so the wisp(s) on the mesh pick it up.
// NEVER applied locally — wispOp is wisp-only (lamps don't have a zone
// state). ShowReceiver::sendControlOp records its own seq into
// controlOpDedup_ before the broadcast, so the reflected copy we get back
// from the gossip rebroadcast doesn't try to apply locally.
void Lamp::drainWispOp() {
  if (lamp::pendingSlots.wispOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.wispOp.drain(pendingMux, buf);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain wispOp len=%u\n", (unsigned)len);
#endif
    if (len > 0) {
#ifdef LAMP_DEBUG
      // testGreet — bench-only handler. The app triple-taps the proximity
      // label and we force a greeting waveform without waiting on a peer
      // to physically arrive on BLE adv. Intercepted here so the wispOp
      // does NOT get broadcast onto the mesh; it's purely local.
      {
        JsonDocument peek;
        auto err = deserializeJson(peek, buf, len);
        if (!err) {
          const char* op = peek["op"] | "";
          if (std::strcmp(op, "testGreet") == 0) {
            const char* bdAddrC = peek["bdAddr"] | "";
            std::string bdAddr(bdAddrC);
            NearbyLamp peer;
            if (!nearbyLamps.findByBdAddr(bdAddr, peer)) {
              Serial.printf("[testGreet] peer not in nearbyLamps (bdAddr=%s) — dropped\n",
                            bdAddr.c_str());
              return;
            }
            const GreetingTuning tuning = personalityEngine.greetingFor(peer.bdAddr);
            shadeSocialBehavior.foundLampColor = peer.baseColor;
            shadeSocialBehavior.applyTuning(tuning);
            shadeSocialBehavior.playOnce();
            nearbyLamps.acknowledge(peer.name);
            shadeSocialBehavior.markGreeted(peer.name, millis());
            Serial.printf("[testGreet] forced greeting for %s (%s)\n",
                          peer.name.c_str(), bdAddr.c_str());
            return;
          }
        }
      }
#endif
      // Removed the !isOtaInProgress() gate on 2026-06-13 after a sub-agent
      // audit confirmed it silently dropped user-driven wispOps (e.g.
      // `setSource off`) whenever a background gossip-OTA was in flight,
      // and the wisp never received the broadcast. Config ops are low-rate
      // and small; a few extra mesh bytes during OTA is acceptable in
      // exchange for the ops actually reaching the wisp every time the
      // user taps Save. The applyRemoteOpRouted() quiesce stays in place
      // because that forward path is gossip-relay (much higher volume).
      static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      showReceiver.sendControlOp(kBroadcastMac,
                                 reinterpret_cast<const uint8_t*>(buf),
                                 len);
    }
  }
}

// Drain pendingSlots.wispStatus — applyRemoteOpLocal saw a wispStatus
// payload and posted it here with the wisp's source MAC. Cache into
// NearbyLamps and push a BLE notification so any app subscribed to
// CHAR_WISP_STATUS picks up the change without round-tripping a read.
void Lamp::drainWispStatus() {
  if (lamp::pendingSlots.wispStatus.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint8_t srcMac[6];
    uint16_t len = lamp::pendingSlots.wispStatus.drain(pendingMux, buf, srcMac);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain wispStatus len=%u src=%02X:%02X:%02X:%02X:%02X:%02X\n",
                  (unsigned)len, srcMac[0], srcMac[1], srcMac[2],
                  srcMac[3], srcMac[4], srcMac[5]);
#endif
    if (len > 0) {
      lamp::nearbyLamps.cacheWispStatus(srcMac, buf, len);
      ble_control::notifyWispStatus();
    }
  }
}

// MSG_EVENT drain. The recv side already resolved our delayMs from the
// stagger entries list and copied the payload bytes into the slot;
// tryHandleExpressionEvent does the (cheap-peek → RecentCascade dedup →
// full parse → trigger) dance on Core 1. No local-config consult —
// cascade is sender-authoritative.
void Lamp::drainEvent() {
  {
    lamp::PendingEvent cmd;
    if (lamp::pendingSlots.event.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain event src=%02X:%02X:%02X:%02X:%02X:%02X "
                    "delayMs=%u payloadLen=%u\n",
                    cmd.sourceMac[0], cmd.sourceMac[1], cmd.sourceMac[2],
                    cmd.sourceMac[3], cmd.sourceMac[4], cmd.sourceMac[5],
                    (unsigned)cmd.delayMs, (unsigned)cmd.payloadLen);
#endif
      expressionManager.tryHandleExpressionEvent(cmd.sourceMac, cmd.delayMs,
                                                 cmd.payload, cmd.payloadLen);
    }
  }
}

// MSG_FW_OFFER / MSG_FW_DONE drain. ShowReceiver's WiFi recv path populates
// the slot; FirmwareReceiver does the heavy work on Core 1 (esp_ota_begin,
// signature verify, set_boot_partition). The chunk fast-path bypasses this
// slot — see show_receiver.cpp MSG_FW_CHUNK branch.
void Lamp::drainFirmwareControl() {
  {
    lamp::PendingFirmwareControl cmd;
    if (lamp::pendingSlots.firmwareControl.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain fwControl msgType=0x%02X seq=%u\n",
                    (unsigned)cmd.msgType, (unsigned)cmd.seq);
#endif
      if (cmd.msgType == lamp_protocol::MSG_FS_OFFER ||
          cmd.msgType == lamp_protocol::MSG_FS_DONE) {
        fs_ota::handleControl(cmd);
      } else {
        firmwareReceiver.handleControlOnLoop(cmd);
      }
    }
  }
}

}  // namespace lamp

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
extern lamp::SocialBehavior shadeSocialBehavior;
#endif

namespace lamp {

namespace {

// Local state for drainCommit only.
bool      commitDirty = false;
uint32_t  lastCommitSignalMs = 0;
uint32_t  lastPersistedHash = 0;  // FNV-1a of last successfully persisted serialized JSON
constexpr uint32_t kCommitFlushIdleMs = 2500;

uint32_t fnv1aHash(const String& s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.length(); ++i) {
    h ^= static_cast<uint8_t>(s[i]);
    h *= 16777619u;
  }
  return h;
}

}  // namespace

// Live-preview only; does not invalidate the section cache.
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
    // Live-preview drains update in-memory config but not the section cache.
    // The cache reflects the persisted snapshot; only commit/settings_blob
    // paths invalidate it.
  }
}

// Live-preview; updates config.shade.colors without NVS persist.
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
  }
}

// Live-preview; updates config.base.colors without NVS persist.
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
  }
}

// Preview only; does not touch NVS.
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

// Persists immediately. Must run before drainSettingsBlob and drainCommit
// so the commit-time hash sees the updated snapshot.
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

    // Persist immediately: expression edits have no global Save step,
    // so each op must survive a reboot on its own.
    if (applied) {
      config.persistConfig("expressionOp");
    }
  }
}

// Debounced 2500 ms; hash-dedup skips redundant NVS writes.
// Force-flush path on BLE disconnect. Runs after live-preview drains.
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
      // OTA in progress; defer. commitDirty stays set.
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
        // Persist failure leaves commitDirty set; next tick retries.
      }
    }
  }
}

// Bulk JSON config apply (factory reset, per-section dispatch, opt-in reboot).
// Runs after drainExpressionOp. OTA-in-progress guard discards the write.
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
      // Warning is unconditional: factoryReset co-shipped with other keys
      // must surface in fleet logs regardless of debug mode.
      if (incomingDoc.as<JsonObject>().size() > 1) {
        Serial.println(
            "[loop] settingsBlob WARNING: factoryReset co-shipped with "
            "other keys — those keys will be silently dropped.");
      }
#ifdef LAMP_DEBUG
      Serial.println("[loop] settingsBlob: factoryReset sentinel, wiping NVS");
#endif
      if (config.factoryReset()) {
        ble_control::notifyStateChange();
        lamp::fadeOutRebootRequested = true;
      } else {
#ifdef LAMP_DEBUG
        Serial.println("[nvs] factory reset failed");
#endif
      }
    } else if (firmwareReceiver.isInProgress()) {
      // NVS write during OTA risks chunk-write contention; discard the blob.
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

// Runs after drainSettingsBlob; BLE callback already verified isAuthed.
// Persistence and map rebuild run here on Core 1.
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

// Manual test triggers; routes to dispatchLampAction.
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

// WiFi op drain; scan-only.
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
    }
  }
}

// JSON parse and dispatch via the cascade router.
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

// BLE has no network source MAC; selfMac lets app-driven triggers coalesce.
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

// Pin wisp identity before apply() so the first paint-triggered
// notifyWispStatus carries non-empty status.
void Lamp::drainOverrideColors() {
  {
    lamp::PendingOverrideColors cmd;
    if (lamp::pendingSlots.overrideColors.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain overrideColors surface=0x%02X n=%u fadeMs=%u\n",
                    (unsigned)cmd.surface, (unsigned)cmd.numColors,
                    (unsigned)cmd.fadeDurationMs);
#endif
      // OTA quiet mode: compositor owns the display for the progress indicator;
      // drop paint frames to avoid competing with it.
      if (lamp::ota_quiet_mode::isQuiet()) {
#ifdef LAMP_DEBUG
        Serial.printf("[loop] drop overrideColors (ota quiet)\n");
#endif
        return;
      }
      // Cache the wisp MAC before apply() so the transition callback's
      // notifyWispStatus sees non-empty status immediately.
      if (cmd.sourceKind == lamp_protocol::OverrideSource::Wisp) {
        lamp::nearbyLamps.cacheWispMacFromPaint(cmd.sourceMac);
      }
      // BaseAndShade: colors[0] to base, colors[1] to shade.
      // If numColors < 2 only base is updated.
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
      // Base + Shade arrive 10 ms apart but share a single-slot mailbox.
      // If Core 1 lags, the second frame drops the first and only one
      // surface's watchdog is refreshed. Touch both so the watchdog
      // doesn't expire on the dropped surface.
      if (cmd.sourceKind == lamp_protocol::OverrideSource::Wisp) {
        const uint32_t now = millis();
        lamp::overrides.base.touchApply(now);
        lamp::overrides.shade.touchApply(now);
      }
    }
  }
}

// Releases the transient override on the matching surface(s).
void Lamp::drainRestoreColors() {
  {
    lamp::PendingRestoreColors cmd;
    if (lamp::pendingSlots.restoreColors.drain(pendingMux, cmd)) {
#ifdef LAMP_DEBUG
      Serial.printf("[loop] drain restoreColors surface=0x%02X fadeMs=%u\n",
                    (unsigned)cmd.surface, (unsigned)cmd.fadeDurationMs);
#endif
      // BaseAndShade: restore both surfaces.
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

// Transient brightness override.
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

// Cache wisp hello fields (version, flags, palette-id prefix).
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

// Cache the wisp palette and push a BLE notify so apps see it immediately.
void Lamp::drainWispPalette() {
  {
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

// Cache the wisp-claimed lamp MACs for CHAR_WISP_CLAIMS.
void Lamp::drainWispClaim() {
  {
    lamp::PendingWispClaim cmd;
    if (lamp::pendingSlots.wispClaim.drain(pendingMux, cmd)) {
      lamp::nearbyLamps.cacheWispClaim(cmd.sourceMac, cmd.lampMacs, cmd.count,
                                       millis());
    }
  }
}

// Broadcasts wispOp as MSG_CONTROL_OP to reach the wisp. Not applied
// locally; sendControlOp pre-records the seq so the echoed relay is deduped.
void Lamp::drainWispOp() {
  if (lamp::pendingSlots.wispOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.wispOp.drain(pendingMux, buf);
#ifdef LAMP_DEBUG
    Serial.printf("[loop] drain wispOp len=%u\n", (unsigned)len);
#endif
    if (len > 0) {
#ifdef LAMP_DEBUG
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
      // No OTA gate here: wispOps are low-rate config; they reach the wisp
      // even during OTA. applyRemoteOpRouted() still quiesces (gossip-relay,
      // much higher volume).
      static const uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      showReceiver.sendControlOp(kBroadcastMac,
                                 reinterpret_cast<const uint8_t*>(buf),
                                 len);
    }
  }
}

// Cache wispStatus and notify CHAR_WISP_STATUS subscribers.
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

// delayMs already resolved on the recv side.
// Cascade is sender-authoritative; no local-config consult.
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

// FW_OFFER/DONE: heavy work (esp_ota_begin, sig verify) runs on Core 1.
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

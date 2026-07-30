#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "behaviors/configurator.hpp"
#include "behaviors/fade_out.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_base_knockout.hpp"
#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_settings_blob.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/firmware/firmware_receiver.hpp"
#include "components/firmware/fs_ota.hpp"
#include "components/network/ble/ble_control.hpp"
#include "components/network/mesh/lamp_roster.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "components/network/transport/wifi.hpp"
#include "config/config.hpp"
#include "components/firmware/ota_quiet_mode.hpp"
#include "core/compositor.hpp"
#include "core/override_aggregate.hpp"
#include "core/pending_slot_aggregate.hpp"


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
    lamp::apply::brightnessToConfig(level, ble_control::isHomeModePageActive(), s_hwMaxBrightness);
    // Live-preview drains update in-memory config but not the section cache.
    // The cache reflects the persisted snapshot; only commit/settings_blob
    // paths invalidate it.
  }
}

// Routes the web edit_session signal to the same operatorEditing setters
// the BLE CHAR_EDIT_SESSION path uses, so an open base/shade color editor
// pauses expressions (and wisp paint) on that surface.
void Lamp::drainEditSession() {
  if (pendingEditSession < 0) return;
  const int16_t packed = pendingEditSession;
  pendingEditSession = -1;
  const uint8_t surface = static_cast<uint8_t>(packed >> 1);
  const bool open = (packed & 0x01) != 0;
  lamp::overrides.setSurfacePreview(surface, open);
#ifdef LAMP_DEBUG
  Serial.printf("[drain] editSession surface=0x%02x %s\n",
                surface, open ? "open" : "closed");
#endif
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
      // Object payload {"seg":k,"colors":[...]} targets one segment (per-segment
      // live preview); a bare array targets the broadcast segment (seg 0).
      if (doc.is<JsonObject>()) {
        lamp::apply::shadeSegmentColorsToConfig(
            doc["seg"] | 0, doc["colors"].as<JsonArray>());
      } else {
        lamp::apply::shadeColorsToConfig(doc.as<JsonArray>());
      }
    }
    lamp::stampConfiguratorActivity(millis());
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
    lamp::stampConfiguratorActivity(millis());
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
      // Reserve up front; growing by doubling would momentarily need ~2x the
      // final size contiguous on a heap this fragmented, tipping into
      // bad_alloc.
      serialized.reserve(measureJson(doc));
      try {
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
      } catch (const std::bad_alloc&) {
#ifdef LAMP_DEBUG
        Serial.println("[loop] commit drain: OOM during dedup hash, deferred");
#endif
        // commitDirty stays set; next tick retries.
      }
    }
  }
}

// Bulk JSON config apply (factory reset, per-section dispatch, opt-in reboot).
// Runs after drainExpressionOp. OTA-in-progress guard discards the write.
void Lamp::drainSettingsBlob() {
  lamp::Config& config = ::config;
  if (lamp::pendingSlots.settingsBlob.valid) {
    // static: 2 KB is too big for the loop-task stack; this drain is the
    // slot's only consumer and runs only on Core 1.
    static char buf[lamp::kPendingSettingsBlob + 1];
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
      bool wantsReboot = lamp::apply::settingsBlobLocal(incomingDoc.as<JsonObject>(), s_hwMaxBrightness);
      recomputeEffectiveCeiling();  // new brightnessCeiling takes effect without reboot
      reapplyHomeModeState();
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
    meshLink.getMyMac(selfMac);
    applyRemoteOpRouted(buf, len, selfMac, RemoteOpTransport::BLE);
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
      const bool presenceEdge = lamp::lampRoster.cacheWispHello(
          cmd.sourceMac, cmd.wispVersion, cmd.flags, cmd.paletteIdPrefix,
          cmd.carriedFwChannel, cmd.carriedFwVersion);
      if (presenceEdge) ble_control::notifyWispStatus();
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
      lamp::lampRoster.cacheWispPalette(cmd.sourceMac, cmd.rgbw, cmd.count);
      ble_control::notifyWispStatus();
    }
  }
}

// Cache the wisp-claimed lamp MACs for CHAR_WISP_CLAIMS.
void Lamp::drainWispClaim() {
  {
    lamp::PendingWispClaim cmd;
    if (lamp::pendingSlots.wispClaim.drain(pendingMux, cmd)) {
      uint8_t selfMac[6];
      meshLink.getMyMac(selfMac);
      lamp::lampRoster.cacheWispClaim(cmd.sourceMac, cmd.lampMacs, cmd.count,
                                       selfMac, millis());
    }
  }
}

// Drive the wisp render + app indicator from MSG_WISP_STATE, the sole wisp
// paint path. Gated to this lamp's display wisp so a rival's frame can't ease
// it home. This lamp's own entry present -> eased base/shade targets + active;
// a fresh frame that omits it (Off, or unclaimed) -> ease home now. Every
// entry feeds the app's painted-lamps preview.
void Lamp::drainWispState() {
  // static: the 1440 B entry array is too big for the loop-task stack; this
  // drain is the slot's only consumer and runs only on Core 1.
  static lamp::PendingWispState cmd;
  if (!lamp::pendingSlots.wispState.drain(pendingMux, cmd)) return;
  if (lamp::ota_quiet_mode::isQuiet()) return;
  const uint32_t now = millis();

  uint8_t dispMac[6];
  if (lamp::lampRoster.copyDisplayWispMac(dispMac, now) &&
      std::memcmp(dispMac, cmd.sourceMac, 6) != 0) {
    return;
  }
  lamp::lampRoster.cacheWispPaint(cmd.sourceMac, cmd.entries, cmd.count, now);

  // Yield the render to an open operator color edit; the preview above still
  // updates.
  if (lamp::overrides.base.operatorEditing() ||
      lamp::overrides.shade.operatorEditing()) {
    return;
  }

  const bool wasActive = compositor.wispActive();
  const Color prevBase = compositor.wispStateBaseColor();
  uint8_t selfMac[6];
  meshLink.getMyMac(selfMac);
  const uint8_t* e =
      lamp_protocol::findWispStateEntry(cmd.entries, cmd.count, selfMac);
  if (e) {
    compositor.applyWispState(Color(e[6], e[7], e[8], 0),
                              Color(e[9], e[10], e[11], 0), now);
  } else {
    compositor.clearWispState(now);
  }
  const bool active = compositor.wispActive();
  if (active != wasActive ||
      (active && !(compositor.wispStateBaseColor() == prevBase))) {
    ble_control::notifyWispStatus();
  }
#ifdef LAMP_DEBUG
  if (e) {
    Serial.printf("[loop] drain wispState base=%u,%u,%u shade=%u,%u,%u\n",
                  e[6], e[7], e[8], e[9], e[10], e[11]);
  } else {
    Serial.printf("[loop] drain wispState: not painted (ease home)\n");
  }
#endif
}

// Unicasts wispOp as MSG_CONTROL_OP to the in-range display wisp's MAC.
// Single-hop scope: no display wisp in range means nothing to send (no
// broadcast fallback). Not applied locally; sendControlOpUnicast pre-records
// the seq so an echoed relay is deduped.
void Lamp::drainWispOp() {
  if (lamp::pendingSlots.wispOp.valid) {
    char buf[lamp::kPendingJsonOp + 1];
    uint16_t len = lamp::pendingSlots.wispOp.drain(pendingMux, buf);
    if (len > 0) {
      // No OTA gate here: wispOps are low-rate config; they reach the wisp
      // even during OTA. applyRemoteOpRouted() still quiesces (gossip-relay,
      // much higher volume).
      uint8_t mac[6];
      if (lamp::lampRoster.copyDisplayWispMac(mac, millis())) {
#ifdef LAMP_DEBUG
        Serial.printf("[loop] drain wispOp len=%u unicast->%02X:%02X:%02X:%02X:%02X:%02X\n",
                      (unsigned)len, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
#endif
        // ponytail: no per-peer ack tracker — app-layer confirm is the reachability surface; the send-cb FAIL log covers bench.
        meshLink.sendControlOpUnicast(mac,
                                      reinterpret_cast<const uint8_t*>(buf),
                                      len);
      } else {
#ifdef LAMP_DEBUG
        Serial.printf("[loop] drain wispOp len=%u: no display wisp in range\n",
                      (unsigned)len);
#endif
      }
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
      lamp::lampRoster.cacheWispStatus(srcMac, buf, len);
      ble_control::notifyWispStatus();
    }
  }
}

void Lamp::drainCommand() {
  {
    // static: the 1444 B payload is too big for the loop-task stack; this drain
    // is the slot's only consumer and runs only on Core 1.
    static lamp::PendingCommand cmd;
    if (lamp::pendingSlots.command.drain(pendingMux, cmd)) {
      JsonDocument doc;
      if (deserializeJson(doc, cmd.payload, cmd.payloadLen) != DeserializationError::Ok) return;
      lamp::ExpressionInvocation inv;
      if (!lamp::parseInvocation(doc.as<JsonObjectConst>(), inv)) return;
      if (inv.delayMs == 0) {
        expressionManager.triggerInvocation(inv, cmd.sourceMac);
      } else {
        lamp::enqueueDelayedInvocation(inv, cmd.sourceMac, inv.delayMs);
      }
    }
  }
}

void Lamp::drainColorQuery() {
  lamp::PendingColorQuery q;
  if (!lamp::pendingSlots.colorQuery.drain(pendingMux, q)) return;

  lamp::Config& config = ::config;

  uint8_t baseStops[lamp_protocol::COLOR_INFO_MAX_STOPS * 4];
  uint8_t shadeStops[lamp_protocol::COLOR_INFO_MAX_STOPS * 4];

  auto pack = [](const std::vector<Color>& colors, uint8_t* out) -> uint8_t {
    uint8_t n = 0;
    for (const Color& c : colors) {
      if (n >= lamp_protocol::COLOR_INFO_MAX_STOPS) break;
      out[n * 4 + 0] = c.r; out[n * 4 + 1] = c.g;
      out[n * 4 + 2] = c.b; out[n * 4 + 3] = c.w;
      ++n;
    }
    return n;
  };

  const uint8_t baseCount  = pack(config.base.broadcastColors(),  baseStops);
  const uint8_t shadeCount = pack(config.shade.broadcastColors(), shadeStops);

  meshLink.sendColorInfo(q.sourceMac, baseStops, baseCount,
                         shadeStops, shadeCount);
}

void Lamp::drainColorInfo() {
  lamp::PendingColorInfo info;
  if (!lamp::pendingSlots.colorInfo.drain(pendingMux, info)) return;
  std::vector<Color> baseStops;
  baseStops.reserve(info.baseCount);
  for (uint8_t i = 0; i < info.baseCount; ++i) {
    baseStops.emplace_back(info.baseStops[i * 4 + 0], info.baseStops[i * 4 + 1],
                           info.baseStops[i * 4 + 2], info.baseStops[i * 4 + 3]);
  }
  std::vector<Color> shadeStops;
  shadeStops.reserve(info.shadeCount);
  for (uint8_t i = 0; i < info.shadeCount; ++i) {
    shadeStops.emplace_back(info.shadeStops[i * 4 + 0], info.shadeStops[i * 4 + 1],
                            info.shadeStops[i * 4 + 2], info.shadeStops[i * 4 + 3]);
  }
  if (auto* g = compositor.behaviorContext().greeting) {
    g->onColorInfo(info.sourceMac, baseStops, shadeStops);
  }
}

void Lamp::drainEvent() {
  {
    lamp::PendingEvent ev;
    if (lamp::pendingSlots.event.drain(pendingMux, ev)) {
      JsonDocument doc;
      if (deserializeJson(doc, ev.payload, ev.payloadLen) != DeserializationError::Ok) return;
      lamp::ExpressionInvocation inv;
      if (!lamp::parseInvocation(doc.as<JsonObjectConst>(), inv)) return;
      expressionObserverRegistry.fanOut(ev.sourceMac, inv);
    }
  }
}

void Lamp::drainBid() {
  const uint32_t now = millis();

  // Fire a previously-armed honor whose jitter delay has elapsed. Re-resolve
  // the peer so a color update between arm and fire is picked up; drop if it
  // left the roster or a greeting is already playing.
  {
    uint8_t mac[6];
    uint8_t bidType;
    if (s_bidReceiver.takeDue(now, mac, bidType)) {
      auto* g = compositor.behaviorContext().greeting;
      lamp::RosterEntry peer;
      if (g && !g->greetingState().active && lampRoster.findByMac(mac, peer)) {
        g->triggerGreeting(lamp::PeerView::from(peer));
      }
    }
  }

  lamp::PendingBid bid;
  if (!lamp::pendingSlots.bid.drain(pendingMux, bid)) return;

  lamp::RosterEntry peer;
  if (!lampRoster.findByMac(bid.sourceMac, peer)) return;  // no name/color to greet
  const uint8_t disp = config.getDisposition(peer.macStr());
  s_bidReceiver.onBid(bid.sourceMac, bid.bidType, disp,
                      config.lamp.socialMode, now, s_bidRng);
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

// core/lamp_remote_op.cpp — cascade-receive routing.
//
// applyRemoteOpRouted is the single convergence point where BLE-originated
// remote-ops (CHAR_OP) and ESP-NOW-originated CONTROL_OPs merge. Both
// inbound transports drain into the pending slots; the drain in
// lamp_drains.cpp dispatches here for surface-routed application.
//
// Split out from core/lamp.cpp purely for readability — keeps the
// receive flow co-located with its drain callers. Bodies are
// line-for-line moves from lamp.cpp.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "behaviors/configurator.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "expressions/expression_manager.hpp"

// Forward-declared so the wispStatus branch below can post into the slot
// aggregate without dragging ble_control.cpp's header into this TU. Same
// pattern ble_control.cpp uses for the BLE-write side of the slot.
void postPendingKnockout(uint8_t pixel, uint8_t brightness);

// wispStatus: applyRemoteOpLocal sees char:"wispStatus" and
// memcpys here under portMUX along with the original wisp's MAC.
// The drain caches via NearbyLamps + emits a BLE notify.
static void postPendingWispStatusJson(const uint8_t srcMac[6],
                                      const char* data, size_t len) {
  lamp::pendingSlots.wispStatus.post(pendingMux, data, len, srcMac);
}

// Apply a remote-op payload locally (either from BLE remoteOp drain when
// targetMac==self/broadcast, or from an incoming ESP-NOW MSG_CONTROL_OP).
// Both call sites run on the loop task (Core 1) — the BLE remoteOp drain
// runs in loop() directly, and the ESP-NOW path goes via MeshLink's
// WiFi-task handler which only does memcpy into pendingInboundOpJson; the
// loop drain then calls this function. Because we're already on Core 1, we
// mutate state directly via the applyXxxLocal helpers above instead of
// re-serializing into a pending slot just so a drain can re-parse it.
//
// `payload` must be a NUL-terminated JSON string for ArduinoJson to parse.
// `srcMac` identifies the sender (used by triggerInvocation to coalesce
// rapid same-sender cascades). For BLE-initiated paths the caller passes
// `myMac_` so app-driven triggers coalesce against each other.
static void applyRemoteOpLocal(const char* payloadJson, size_t len,
                               const uint8_t srcMac[6]) {
  JsonDocument doc;
  if (deserializeJson(doc, payloadJson, len) != DeserializationError::Ok) return;
  const char* ch = doc["char"].as<const char*>();
  if (!ch || !*ch) return;

  if (strcmp(ch, "brightness") == 0) {
    int level = doc["value"] | -1;
    if (level >= 0 && level <= 100) {
      // Cascade-relayed brightness goes directly through ToRender —
      // skips the pendingBrightness slot (we're already on Core 1)
      // AND skips config mutation so a subsequent CHAR_COMMIT doesn't
      // persist the cascade transient.
      lamp::apply::brightnessToRender(static_cast<uint8_t>(level), false, s_hwMaxBrightness);
    }

  } else if (strcmp(ch, "shadeColors") == 0) {
    // Direct path: applyRemoteOpLocal runs on Core 1, so call the local
    // applier with the JsonArray we already have. Skips the slot round-trip
    // that would otherwise be serializeJson → std::string → memcpy → drain → re-parse.
    // Cascade-source: ToRender only — no config mutation; cascade transients
    // must not contaminate a subsequent CHAR_COMMIT persistence sweep.
    lamp::apply::shadeColorsToRender(doc["colors"].as<JsonArray>());
    // Match the drain's unconditional bookkeeping.
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    // DO NOT invalidate the shade section here. See the architectural
    // comment at the CHAR_BRIGHTNESS drain (~line 954) for why live
    // preview must not poison the persisted-snapshot section cache.

  } else if (strcmp(ch, "baseColors") == 0) {
    // Cascade-source: ToRender only — no config mutation.
    lamp::apply::baseColorsToRender(doc["colors"].as<JsonArray>());
    shadeConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    baseConfiguratorBehavior.lastWebSocketUpdateTimeMs = millis();
    // Same as shadeColors above — live preview must not poison the
    // persisted-snapshot section cache.

  } else if (strcmp(ch, "knockout") == 0) {
    int pixel = doc["pixel"] | -1;
    int brightness = doc["brightness"] | -1;
    if (pixel >= 0 && pixel < 256 && brightness >= 0 && brightness <= 100) {
      postPendingKnockout(static_cast<uint8_t>(pixel), static_cast<uint8_t>(brightness));
    }

  } else if (strcmp(ch, "expressionOp") == 0) {
    // Direct path: same Core 1 reasoning — call the applier with the parsed
    // JsonObject. The drain shape expects the `char` key gone, but the
    // applier just looks at `op`/`entry`/`type`/`target`, so leaving `char`
    // is harmless. Skips serialize → drain → re-parse.
    // Cascade-source: ToRender only — no config mutation; cascade transients
    // must not contaminate a subsequent CHAR_COMMIT persistence sweep.
    lamp::apply::expressionOpToRender(doc.as<JsonObject>());
    // Match the drain's unconditional invalidate semantics.
    config.invalidateExpressionsSection();

  } else if (strcmp(ch, "wispStatus") == 0) {
    // A wispStatus payload reached us — either directly from the
    // wisp's MSG_CONTROL_OP broadcast or via a peer's gossip relay. We
    // don't APPLY anything locally (lamps don't have a zone state to
    // mutate); we just cache + notify so the phone-paired lamp's
    // CHAR_WISP_STATUS read/notify surface stays current. srcMac is the
    // wisp's MAC (preserved through gossip relay by MeshLink, which
    // copies op.sourceMac into the inbound slot).
    postPendingWispStatusJson(srcMac, payloadJson, len);
  }
  // wispOp intentionally has NO branch here. The dedicated CHAR_WISP_OP
  // slot drain broadcasts a MSG_CONTROL_OP; the wisp(s) on the mesh
  // consume it. A gossip-relayed wispOp lands here, finds no matching
  // branch, and silently drops — which is exactly the desired behavior.
}

// `RemoteOpTransport` enum + applyRemoteOpRouted forward declaration both
// live in lamp_internal.hpp so lamp_drains.cpp's drainInboundOp /
// drainRemoteOp can see them. Definition follows below.

// Unified cascade-receive router. Single place where the BLE remoteOp drain
// and the ESP-NOW CONTROL_OP receive path converge — both used to duplicate
// the self-vs-broadcast logic, the ApplyX dispatch, and the forwarding
// policy.
//
// Inputs:
//   `payloadJson`  : NUL-terminated JSON. For BLE this is the raw GATT
//                    write payload; for ESP-NOW it's the CONTROL_OP payload
//                    that MeshLink already passed through MAC + dedup
//                    filtering on the WiFi task.
//   `srcMac`       : Sender's MAC. For ESP-NOW this is the original CONTROL_OP
//                    `sourceMac`. For BLE there is no real network source —
//                    the caller passes selfMac so app-driven rapid triggers
//                    coalesce against each other on the receive side.
//   `origin`       : Selects the small bit of asymmetry between the two
//                    transports (see comments below). Everything else —
//                    JSON dispatch (`applyRemoteOpLocal`), RecentCascade
//                    dedup + suppressCascade_ invariant (both internal to
//                    ExpressionManager and consulted via triggerInvocation),
//                    delayed-trigger queue, settings-not-forwarded policy —
//                    is shared.
//
// What still differs between BLE and EspNow, handled here:
//   - For BLE the payload arrives wrapped with a top-level `targetMac`
//     ("broadcast" | "AA:BB:..."). The router strips it, decides self /
//     broadcast / unicast, applies locally when self-or-broadcast, and
//     forwards over ESP-NOW (broadcast or unicast) when not-self.
//   - For EspNow the addressed-to-us check (`forUs`) and the once-only
//     grid rebroadcast already happened on the WiFi task inside
//     MeshLink::handleRecv (kept there for latency: rebroadcast doesn't
//     wait on the loop drain). By the time the slot is drained the payload
//     is unconditionally for-us, so the router just applies locally.
//
void applyRemoteOpRouted(const char* payloadJson, size_t len,
                         const uint8_t srcMac[6],
                         RemoteOpTransport origin) {
  if (origin == RemoteOpTransport::EspNow) {
    // MeshLink::handleRecv (WiFi task) already gated on targetMac == myMac
    // || broadcast, and already rebroadcast once for grid relay. Nothing for
    // the router to decide — just dispatch locally on Core 1.
    applyRemoteOpLocal(payloadJson, len, srcMac);
    return;
  }

  // BLE origin. The CHAR_REMOTE_OP drain handed us a payload that may carry a
  // top-level `targetMac` field selecting self / broadcast / a specific peer.
  JsonDocument doc;
  if (deserializeJson(doc, payloadJson, len) != DeserializationError::Ok) return;

  const char* tgtStr = doc["targetMac"].as<const char*>();
  uint8_t targetMac[6] = {0};
  bool isBroadcast = false;
  bool isSelf = false;
  if (tgtStr) {
    if (strcmp(tgtStr, "broadcast") == 0) {
      memset(targetMac, 0xFF, 6);
      isBroadcast = true;
    } else if (sscanf(tgtStr, "%02hhX:%02hhX:%02hhX:%02hhX:%02hhX:%02hhX",
                      &targetMac[0], &targetMac[1], &targetMac[2],
                      &targetMac[3], &targetMac[4], &targetMac[5]) == 6) {
      uint8_t myMac[6];
      meshLink.getMyMac(myMac);
      isSelf = (memcmp(targetMac, myMac, 6) == 0);
    }
  }

  // Strip targetMac before applying/forwarding — applyRemoteOpLocal and the
  // CONTROL_OP payload both expect the unwrapped op shape.
  doc.remove("targetMac");
  std::string payload;
  serializeJson(doc, payload);

  if (isSelf || isBroadcast) {
    // applyRemoteOpLocal's srcMac is the *original* sender for the cascade
    // coalesce. For BLE-initiated ops the "sender" is this lamp's own app
    // session — `srcMac` was already populated with selfMac by the caller.
    applyRemoteOpLocal(payload.data(), payload.size(), srcMac);
  }
  if (!isSelf && !meshLink.isOtaInProgress()) {
    // Forward over ESP-NOW. broadcast => fan out to all peers; unicast =>
    // targets the specific MAC. MeshLink::sendControlOp also records
    // its own seq into controlOpDedup_ so the rebroadcast we'll get back
    // doesn't loop in as an "apply locally".
    //
    // Suppressed during gossip OTA: control-op forwards compete with the
    // chunk stream for ESP-NOW airtime under BLE coex. Dropped forwards
    // are recoverable — the app retries on its next loop drain — but a
    // dropped chunk needs a stall-watchdog REQ round trip to recover.
    meshLink.sendControlOp(
        targetMac,
        reinterpret_cast<const uint8_t*>(payload.data()),
        payload.size());
  }
}

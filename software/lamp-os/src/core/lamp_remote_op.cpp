// Remote-op receive.
//
// applyRemoteOp handles the app's CHAR_REMOTE_OP write on this lamp: an op
// addressed to this lamp or "broadcast" is applied here, and one naming any
// other lamp is ignored. applyInboundControlOp handles a CONTROL_OP the mesh
// delivered: the only ops a lamp takes from the mesh are the wisp's status and
// config, so every other tag is dropped after MeshLink has relayed it.

#include "core/lamp.hpp"
#include "core/lamp_internal.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>

#include <cstdint>
#include <cstring>

#include "behaviors/configurator.hpp"
#include "components/apply/apply_base_colors.hpp"
#include "components/apply/apply_brightness.hpp"
#include "components/apply/apply_expressions.hpp"
#include "components/apply/apply_shade_colors.hpp"
#include "components/network/mesh/mesh_link.hpp"
#include "config/config.hpp"
#include "core/pending_slot_aggregate.hpp"
#include "core/wisp_broadcast_peek.hpp"
#include "core/remote_op_target.hpp"
#include "util/dropped_write_log.hpp"

// Forward-declared so the knockout op can post into the slot aggregate
// without dragging ble_control.cpp's header into this TU.
void postPendingKnockout(uint8_t pixel, uint8_t brightness);

// Runs on the loop task (Core 1), so it mutates state directly through the
// apply helpers instead of re-serializing into a pending slot.
static void applyLampOp(JsonObject doc) {
  const char* ch = doc["char"].as<const char*>();
  if (!ch || !*ch) return;

  if (strcmp(ch, "brightness") == 0) {
    int level = doc["value"] | -1;
    if (level >= 0 && level <= 100) {
      // Remote-op brightness goes directly through ToRender.
      // Skips the pendingBrightness slot (already on Core 1)
      // AND skips config mutation so a subsequent CHAR_COMMIT doesn't
      // persist the preview.
      lamp::apply::brightnessToRender(static_cast<uint8_t>(level), false, s_hwMaxBrightness);
    }

  } else if (strcmp(ch, "shadeColors") == 0) {
    // Direct path: this runs on Core 1, so call the local
    // applier with the JsonArray already in hand. Skips the slot round-trip
    // that would otherwise be serializeJson → std::string → memcpy → drain → re-parse.
    // Preview: ToRender only, no config mutation; a preview
    // must not contaminate a subsequent CHAR_COMMIT persistence sweep.
    lamp::apply::shadeColorsToRender(doc["colors"].as<JsonArray>());
    // Match the drain's unconditional bookkeeping.
    lamp::stampConfiguratorActivity(millis());
    // DO NOT invalidate the shade section here. See drainBrightness in
    // lamp_drains.cpp for why live preview must not poison the persisted-snapshot section cache.

  } else if (strcmp(ch, "baseColors") == 0) {
    // Preview: ToRender only, no config mutation.
    lamp::apply::baseColorsToRender(doc["colors"].as<JsonArray>());
    lamp::stampConfiguratorActivity(millis());
    // Same as shadeColors above; live preview must not poison the
    // persisted-snapshot section cache.

  } else if (strcmp(ch, "knockout") == 0) {
    int pixel = doc["pixel"] | -1;
    int brightness = doc["brightness"] | -1;
    if (pixel >= 0 && pixel < 256 && brightness >= 0 && brightness <= 100) {
      postPendingKnockout(static_cast<uint8_t>(pixel), static_cast<uint8_t>(brightness));
    }

  } else if (strcmp(ch, "expressionOp") == 0) {
    // Direct path: same Core 1 reasoning. Call the applier with the parsed
    // JsonObject. The drain shape expects the `char` key gone, but the
    // applier just looks at `op`/`entry`/`type`/`target`, so leaving `char`
    // is harmless. Skips serialize → drain → re-parse.
    // Preview: ToRender only, no config mutation; a preview
    // must not contaminate a subsequent CHAR_COMMIT persistence sweep.
    lamp::apply::expressionOpToRender(doc);
    // Match the drain's unconditional invalidate semantics.
    config.invalidateExpressionsSection();
  }
}

void applyRemoteOp(const char* payloadJson, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, payloadJson, len) != DeserializationError::Ok) {
    lamp::logDroppedWrite("remoteOp", "parse failed");
    return;
  }

  const char* target = doc["targetMac"].as<const char*>();
  uint8_t myMac[6];
  meshLink.getMyMac(myMac);
  if (!lamp::remoteOpTargetsLamp(target, myMac)) {
    Serial.printf("[remoteOp] ignored: targetMac=%s is not this lamp\n", target ? target : "(none)");
    return;
  }
  applyLampOp(doc.as<JsonObject>());
}

void applyInboundControlOp(const char* payloadJson, size_t len, const uint8_t srcMac[6]) {
  // srcMac is the wisp's MAC, preserved through gossip relay.
  switch (lamp::inboundControlOpKind(payloadJson, len)) {
    case lamp::WispBroadcast::Status:
      lamp::pendingSlots.wispStatus.post(pendingMux, payloadJson, len, srcMac);
      return;
    case lamp::WispBroadcast::Config:
      lamp::pendingSlots.wispConfig.post(pendingMux, payloadJson, len, srcMac);
      return;
    case lamp::WispBroadcast::None:
      return;
  }
}

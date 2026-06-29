#include "WispOpDispatcher.h"

#include <Arduino.h>
#include <ArduinoJson.h>

#include "StageBeacon.h"
#include "WifiLink.h"
#include "WispConfig.h"

namespace wisp {

DispatchResult WispOpDispatcher::dispatch(const uint8_t* payload, size_t len) {
  if (!payload || len == 0) {
    return DispatchResult::Malformed;
  }

  // ArduinoJson v7: unified JsonDocument. Aurora-stack budget is comfortable
  // with a small doc for these short ops (well under 256 bytes typical).
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload, len);
  if (err) {
    Serial.printf("[wisp.op] malformed JSON: %s\n", err.c_str());
    return DispatchResult::Malformed;
  }

  const char* charField = doc["char"];
  if (!charField) {
    // No "char" field at all — could be a different envelope shape. Don't
    // warn loudly; this is the same "not for us" bucket as a foreign char.
    return DispatchResult::Ignored;
  }

  // Anything other than "wispOp" is something we don't speak. wispStatus
  // gossip-relay echoes land here and get silently dropped, per spec.
  if (strcmp(charField, "wispOp") != 0) {
    return DispatchResult::Ignored;
  }

  const char* op = doc["op"];
  if (!op) {
    Serial.println("[wisp.op] wispOp missing 'op' field");
    return DispatchResult::Malformed;
  }

  if (strcmp(op, "setZone") == 0) {
    // zoneId is required and must be an int. ArduinoJson's is<int>() check
    // covers numeric type compatibility (rejects strings/floats/missing).
    if (!doc["zoneId"].is<int>()) {
      Serial.println("[wisp.op] setZone missing/invalid 'zoneId'");
      return DispatchResult::Malformed;
    }
    const int zoneId = doc["zoneId"].as<int>();
    // Negative-zoneId rejection lives in WispConfig::setSelectedZone — the
    // storage layer owns that invariant, so the dispatcher doesn't duplicate
    // the check here.
    Serial.printf("[wisp.op] setZone zoneId=%d\n", zoneId);
    config_.setSelectedZone(zoneId);
    return DispatchResult::AppliedZoneChange;
  }

  if (strcmp(op, "clearZone") == 0) {
    Serial.println("[wisp.op] clearZone");
    config_.clearSelectedZone();
    return DispatchResult::AppliedZoneChange;
  }

  if (strcmp(op, "setSource") == 0) {
    // Accept either "mode":"off|manual|aurora" (preferred, app-emitted
    // shape) or "mode":0|1|2 (numeric fallback). Anything else is
    // malformed. Don't trust the wire to constrain values — coerce
    // server-side so a future app that emits something unexpected
    // doesn't strand the wisp in an unknown mode.
    WispSourceMode resolved = WispSourceMode::Aurora;
    bool resolvedOk = false;
    JsonVariant modeVar = doc["mode"];
    if (modeVar.is<const char*>()) {
      const char* s = modeVar.as<const char*>();
      if (strcmp(s, "off") == 0) {
        resolved = WispSourceMode::Off;
        resolvedOk = true;
      } else if (strcmp(s, "manual") == 0) {
        resolved = WispSourceMode::Manual;
        resolvedOk = true;
      } else if (strcmp(s, "aurora") == 0) {
        resolved = WispSourceMode::Aurora;
        resolvedOk = true;
      }
    } else if (modeVar.is<int>()) {
      const int v = modeVar.as<int>();
      if (v >= 0 && v <= 2) {
        resolved = static_cast<WispSourceMode>(v);
        resolvedOk = true;
      }
    }
    if (!resolvedOk) {
      Serial.println("[wisp.op] setSource missing/invalid 'mode'");
      return DispatchResult::Malformed;
    }
    Serial.printf("[wisp.op] setSource mode=%d\n", static_cast<int>(resolved));
    config_.setSourceMode(resolved);
    return DispatchResult::AppliedSourceChange;
  }

  if (strcmp(op, "setOffColor") == 0) {
    // Wisp-only color shown on the ring when sourceMode == Off. Does not
    // touch the manual palette and does not broadcast to lamps. Accepts
    // the tuple shape `"color":[r,g,b]` or the flat fields `"r"/"g"/"b"`.
    ManualPaletteColor c;
    bool ok = false;
    JsonVariantConst v = doc["color"];
    if (v.is<JsonArrayConst>()) {
      JsonArrayConst tup = v.as<JsonArrayConst>();
      if (tup.size() >= 3) {
        c.r = static_cast<uint8_t>(tup[0].as<int>() & 0xFF);
        c.g = static_cast<uint8_t>(tup[1].as<int>() & 0xFF);
        c.b = static_cast<uint8_t>(tup[2].as<int>() & 0xFF);
        ok = true;
      }
    } else if (doc["r"].is<int>() && doc["g"].is<int>() && doc["b"].is<int>()) {
      c.r = static_cast<uint8_t>(doc["r"].as<int>() & 0xFF);
      c.g = static_cast<uint8_t>(doc["g"].as<int>() & 0xFF);
      c.b = static_cast<uint8_t>(doc["b"].as<int>() & 0xFF);
      ok = true;
    }
    if (!ok) {
      Serial.println("[wisp.op] setOffColor missing/invalid 'color'");
      return DispatchResult::Malformed;
    }
    Serial.printf("[wisp.op] setOffColor %u,%u,%u\n", c.r, c.g, c.b);
    config_.setOffColor(c);
    return DispatchResult::AppliedOffColor;
  }

  if (strcmp(op, "setManualPalette") == 0) {
    // Accept "colors":[[r,g,b],[r,g,b],...] OR "colors":[{"r":..,"g":..,"b":..}]
    // The Flutter side emits the tuple shape; the object shape is
    // tolerated so a curl-test or future client doesn't have to remember
    // which one wins.
    JsonArrayConst arr = doc["colors"].as<JsonArrayConst>();
    if (arr.isNull()) {
      Serial.println("[wisp.op] setManualPalette missing 'colors'");
      return DispatchResult::Malformed;
    }
    std::vector<ManualPaletteColor> parsed;
    parsed.reserve(arr.size());
    for (JsonVariantConst v : arr) {
      ManualPaletteColor c;
      if (v.is<JsonArrayConst>()) {
        JsonArrayConst tup = v.as<JsonArrayConst>();
        if (tup.size() < 3) continue;  // skip malformed entry, keep the rest
        c.r = static_cast<uint8_t>(tup[0].as<int>() & 0xFF);
        c.g = static_cast<uint8_t>(tup[1].as<int>() & 0xFF);
        c.b = static_cast<uint8_t>(tup[2].as<int>() & 0xFF);
      } else if (v.is<JsonObjectConst>()) {
        JsonObjectConst obj = v.as<JsonObjectConst>();
        c.r = static_cast<uint8_t>(obj["r"].as<int>() & 0xFF);
        c.g = static_cast<uint8_t>(obj["g"].as<int>() & 0xFF);
        c.b = static_cast<uint8_t>(obj["b"].as<int>() & 0xFF);
      } else {
        continue;
      }
      parsed.push_back(c);
      if (parsed.size() >= kManualPaletteMaxColors) break;
    }
    Serial.printf("[wisp.op] setManualPalette colors=%u\n",
                  (unsigned)parsed.size());
    config_.setManualPalette(parsed);
    return DispatchResult::AppliedManualPalette;
  }

  if (strcmp(op, "setWifi") == 0) {
    const char* ssid = doc["ssid"];
    const char* pw   = doc["pw"];
    if (!ssid || !pw) {
      Serial.println("[wisp.op] setWifi missing 'ssid' or 'pw'");
      return DispatchResult::Malformed;
    }
    // End-to-end chain: persist into WispConfig (single source of truth),
    // then kick the two downstream consumers so they re-read.
    //   1. WifiLink::reconnect() — drops the current STA association and
    //      tries WiFi.begin() with the new creds.
    //   2. StageBeacon::refreshAdvert() — rebuilds the mfgData payload so
    //      pre-mesh lamps scanning for the magic-42007 advert see the
    //      updated SSID/password on their next scan cycle.
    // Either sink may be null in tests; we skip the kick and let the
    // persistence still happen so the next reboot (or wired-in main)
    // picks up the new creds.
    Serial.printf("[wisp.op] setWifi ssid='%s' pw=<%u chars>\n",
                  ssid, (unsigned)strlen(pw));
    config_.setWifi(String(ssid), String(pw));
    if (wifiLink_) {
      wifiLink_->reconnect();
    } else {
      Serial.println("[wisp.op] setWifi: no WifiLink bound; skipping reconnect");
    }
    if (stageBeacon_) {
      stageBeacon_->refreshAdvert();
    } else {
      Serial.println("[wisp.op] setWifi: no StageBeacon bound; skipping refresh");
    }
    return DispatchResult::AppliedWifiChange;
  }

  if (strcmp(op, "shuffle") == 0) {
    Serial.println("[wisp.op] shuffle");
    config_.bumpShuffleSeed();
    return DispatchResult::AppliedShuffle;
  }

  Serial.printf("[wisp.op] unknown wispOp op='%s'\n", op);
  return DispatchResult::Malformed;
}

}  // namespace wisp

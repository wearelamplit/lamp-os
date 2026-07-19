#include "config/wisp_op_dispatcher.hpp"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <lampos/led_types.hpp>

#include "net/stage_beacon.hpp"
#include "net/wifi_link.hpp"
#include "config/wisp_config.hpp"

namespace wisp {

// CHAR_WISP_OP UUID 5f64f4e1-… as a flat 16-byte array, last byte first.
// Must match uuidSaltLE16(CHAR_WISP_OP) in the app's lamp_crypto.dart.
static const uint8_t kWispOpSaltLE[16] = {
    0x40, 0x6b, 0x7e, 0x6f, 0x8d, 0x3a, 0x3f, 0x9b,
    0x44, 0x4a, 0xd9, 0xd6, 0xe1, 0xf4, 0x64, 0x5f
};

static bool isApplied(DispatchResult r) {
  switch (r) {
    case DispatchResult::AppliedZoneChange:
    case DispatchResult::AppliedWifiChange:
    case DispatchResult::AppliedSourceChange:
    case DispatchResult::AppliedManualPalette:
    case DispatchResult::AppliedOffColor:
    case DispatchResult::AppliedShuffle:
    case DispatchResult::AppliedDriftChange:
    case DispatchResult::AppliedNameChange:
    case DispatchResult::AppliedPasswordChange:
    case DispatchResult::AppliedLedStrip:
    case DispatchResult::AppliedRangeChange:
    case DispatchResult::AppliedBrightnessChange:
      return true;
    default:
      return false;
  }
}

DispatchResult WispOpDispatcher::dispatch(const uint8_t* payload, size_t len) {
  if (!payload || len == 0) {
    return DispatchResult::Malformed;
  }

  const uint8_t magic = crypto::magicByte(payload, len);

  if (magic == crypto::MAGIC_CIPHERTEXT) {
    // decryptOp rejects empty-password attempts.
    if (!nonces_) {
      Serial.println("[wisp.op] sealed op received but no nonce ring bound; rejecting");
      return DispatchResult::Rejected;
    }
    std::string plain;
    std::string pw(config_.password().c_str(), config_.password().length());
    if (!crypto::decryptOp(payload, len, kWispOpSaltLE, "wispOp", pw, *nonces_, plain)) {
      Serial.println("[wisp.op] sealed op: decrypt failed");
      return DispatchResult::Rejected;
    }
    const DispatchResult result = dispatchJson(plain.c_str(), plain.size());
    if (isApplied(result)) config_.bumpOpSeq();
    return result;
  }

  // Plaintext path: 0x01-prefixed or bare '{'.
  const char* json;
  size_t jsonLen;
  if (magic == crypto::MAGIC_PLAINTEXT) {
    json    = reinterpret_cast<const char*>(payload + 1);
    jsonLen = len - 1;
  } else {
    json    = reinterpret_cast<const char*>(payload);
    jsonLen = len;
  }

  // Gate plaintext behind the password, with two exceptions. setManualPalette
  // is an integrity exception: palette colors are unauthenticated by design
  // (an attacker in BLE proximity can vandalize colors regardless, so sealing
  // adds nothing under the proximity threat model). pollStatus is read-only:
  // it only triggers a re-broadcast of already-public state.
  if (config_.password().length() > 0) {
    // Peek the op name before deciding to reject.
    JsonDocument peekDoc;
    DeserializationError err = deserializeJson(peekDoc, json, jsonLen);
    if (!err) {
      const char* op = peekDoc["op"];
      if (op && (strcmp(op, "setManualPalette") == 0 ||
                 strcmp(op, "pollStatus") == 0)) {
        return dispatchJson(json, jsonLen);
      }
    }
    Serial.println("[wisp.op] plaintext rejected: password is set");
    return DispatchResult::Rejected;
  }

  return dispatchJson(json, jsonLen);
}

DispatchResult WispOpDispatcher::dispatchJson(const char* json, size_t len) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json, len);
  if (err) {
    Serial.printf("[wisp.op] malformed JSON: %s\n", err.c_str());
    return DispatchResult::Malformed;
  }

  const char* charField = doc["char"];
  if (!charField) {
    return DispatchResult::Ignored;
  }

  if (strcmp(charField, "wispOp") != 0) {
    return DispatchResult::Ignored;
  }

  const char* op = doc["op"];
  if (!op) {
    Serial.println("[wisp.op] wispOp missing 'op' field");
    return DispatchResult::Malformed;
  }

  if (strcmp(op, "setZone") == 0) {
    if (!doc["zoneId"].is<int>()) {
      Serial.println("[wisp.op] setZone missing/invalid 'zoneId'");
      return DispatchResult::Malformed;
    }
    const int zoneId = doc["zoneId"].as<int>();
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
    // Accept "mode":"off|manual|aurora" or numeric 0|1|2. Coerce server-side
    // so an unexpected wire value doesn't strand the wisp in an unknown mode.
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
    // Accepts "color":[r,g,b] or [r,g,b,w] tuple, or flat "r"/"g"/"b"
    // (+optional "w") fields.
    ManualPaletteColor c;
    bool ok = false;
    JsonVariantConst v = doc["color"];
    if (v.is<JsonArrayConst>()) {
      JsonArrayConst tup = v.as<JsonArrayConst>();
      if (tup.size() >= 3) {
        c.r = static_cast<uint8_t>(tup[0].as<int>() & 0xFF);
        c.g = static_cast<uint8_t>(tup[1].as<int>() & 0xFF);
        c.b = static_cast<uint8_t>(tup[2].as<int>() & 0xFF);
        c.w = tup.size() >= 4
            ? static_cast<uint8_t>(tup[3].as<int>() & 0xFF) : 0;
        ok = true;
      }
    } else if (doc["r"].is<int>() && doc["g"].is<int>() && doc["b"].is<int>()) {
      c.r = static_cast<uint8_t>(doc["r"].as<int>() & 0xFF);
      c.g = static_cast<uint8_t>(doc["g"].as<int>() & 0xFF);
      c.b = static_cast<uint8_t>(doc["b"].as<int>() & 0xFF);
      c.w = static_cast<uint8_t>(doc["w"].as<int>() & 0xFF);
      ok = true;
    }
    if (!ok) {
      Serial.println("[wisp.op] setOffColor missing/invalid 'color'");
      return DispatchResult::Malformed;
    }
    Serial.printf("[wisp.op] setOffColor %u,%u,%u,%u\n", c.r, c.g, c.b, c.w);
    config_.setOffColor(c);
    return DispatchResult::AppliedOffColor;
  }

  if (strcmp(op, "setManualPalette") == 0) {
    // Accepts "colors":[[r,g,b],...] / [[r,g,b,w],...] tuples or
    // [{"r":..,"g":..,"b":..,"w":..}] object shape (w optional).
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
        c.w = tup.size() >= 4
            ? static_cast<uint8_t>(tup[3].as<int>() & 0xFF) : 0;
      } else if (v.is<JsonObjectConst>()) {
        JsonObjectConst obj = v.as<JsonObjectConst>();
        c.r = static_cast<uint8_t>(obj["r"].as<int>() & 0xFF);
        c.g = static_cast<uint8_t>(obj["g"].as<int>() & 0xFF);
        c.b = static_cast<uint8_t>(obj["b"].as<int>() & 0xFF);
        c.w = static_cast<uint8_t>(obj["w"].as<int>() & 0xFF);
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

  if (strcmp(op, "setRange") == 0) {
    if (!doc["range"].is<int>()) {
      Serial.println("[wisp.op] setRange missing/invalid 'range'");
      return DispatchResult::Malformed;
    }
    const int step = doc["range"].as<int>();
    if (step < 0 || step > static_cast<int>(kRangeStepMax)) {
      Serial.println("[wisp.op] setRange 'range' out of bounds");
      return DispatchResult::Malformed;
    }
    config_.setRangeStep(static_cast<uint8_t>(step));
    return DispatchResult::AppliedRangeChange;
  }

  if (strcmp(op, "setBrightness") == 0) {
    if (!doc["brightness"].is<int>()) {
      Serial.println("[wisp.op] setBrightness missing/invalid 'brightness'");
      return DispatchResult::Malformed;
    }
    const int pct = doc["brightness"].as<int>();
    if (pct < 0 || pct > 100) {
      Serial.println("[wisp.op] setBrightness 'brightness' out of bounds");
      return DispatchResult::Malformed;
    }
    config_.setBrightness(static_cast<uint8_t>(pct));
    return DispatchResult::AppliedBrightnessChange;
  }

  if (strcmp(op, "pollStatus") == 0) {
    return DispatchResult::PollStatus;
  }

  if (strcmp(op, "shuffle") == 0) {
    Serial.println("[wisp.op] shuffle");
    config_.bumpShuffleSeed();
    return DispatchResult::AppliedShuffle;
  }

  if (strcmp(op, "setDrift") == 0) {
    if (!doc["intervalMs"].is<int>() || !doc["fadePct"].is<int>()) return DispatchResult::Malformed;
    const int iv = doc["intervalMs"].as<int>();
    const int fp = doc["fadePct"].as<int>();
    if (iv < 30000 || iv > 3600000 || fp < 0 || fp > 100) return DispatchResult::Malformed;
    config_.setDrift(static_cast<uint32_t>(iv), static_cast<uint8_t>(fp));
    return DispatchResult::AppliedDriftChange;
  }

  if (strcmp(op, "setName") == 0) {
    const char* nameVal = doc["name"];
    if (!nameVal) {
      Serial.println("[wisp.op] setName missing 'name'");
      return DispatchResult::Malformed;
    }
    config_.setName(String(nameVal));
    return DispatchResult::AppliedNameChange;
  }

  if (strcmp(op, "setPassword") == 0) {
    // Plaintext only when no password is set; sealed ops authenticate via the old password.
    const char* newPw = doc["password"];
    if (!newPw) {
      Serial.println("[wisp.op] setPassword missing 'password'");
      return DispatchResult::Malformed;
    }
    config_.setPassword(String(newPw));
    Serial.printf("[wisp.op] setPassword applied (%u chars)\n", (unsigned)strlen(newPw));
    return DispatchResult::AppliedPasswordChange;
  }

  if (strcmp(op, "setLedStrip") == 0) {
    lampos::led::ByteOrder bo;
    const char* lt = doc["ledType"] | "";
    if (!lampos::led::byteOrderFromString(lt, bo)) {
      Serial.println("[wisp.op] setLedStrip missing/invalid 'ledType'");
      return DispatchResult::Malformed;
    }
    if (!doc["pixelCount"].is<int>()) {
      Serial.println("[wisp.op] setLedStrip missing/invalid 'pixelCount'");
      return DispatchResult::Malformed;
    }
    const int n = doc["pixelCount"].as<int>();
    if (n < 1) {
      Serial.println("[wisp.op] setLedStrip pixelCount < 1");
      return DispatchResult::Malformed;
    }
    config_.setLedFormat(bo);
    config_.setPixelCount(static_cast<uint16_t>(n));
    return DispatchResult::AppliedLedStrip;
  }

  Serial.printf("[wisp.op] unknown wispOp op='%s'\n", op);
  return DispatchResult::Malformed;
}

}  // namespace wisp

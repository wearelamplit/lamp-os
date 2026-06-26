#include "config.hpp"

#include <ArduinoJson.h>
#include <Preferences.h>

#include <algorithm>

#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "version.hpp"

namespace lamp {

namespace {
// Comparator for std::lower_bound over the sorted dispositions vector.
// Compares an existing entry's name against the target lookup key.
// Used by getDisposition and setDisposition to find the insertion point
// in O(log N) while keeping the vector contiguous and cache-friendly.
inline bool dispositionEntryLess(
    const std::pair<std::string, uint8_t>& a, const std::string& b) {
  return a.first < b;
}
}  // namespace
Config::Config(Preferences* inPrefs) {
  JsonDocument doc;
  prefs = inPrefs;
  prefs->begin("lamp", true);
  String json = prefs->getString("cfg", "{}");
  DeserializationError error = deserializeJson(doc, json);
  prefs->end();

#ifdef LAMP_DEBUG
  // Print a compact, secret-free summary instead of the raw NVS JSON.
  // The raw payload would leak `lamp.password` to anyone on the serial
  // console (USB physical access). If you need the full shape while
  // debugging a parse bug, attach a temporary `Serial.println(json)`
  // for that session — don't roll back this redaction.
  {
    JsonDocument peek;
    if (deserializeJson(peek, json) == DeserializationError::Ok) {
      const char* loadedName = peek["lamp"]["name"] | "<unset>";
      const char* pwField = peek["lamp"]["password"] | "";
      const bool hasPassword = pwField != nullptr && pwField[0] != '\0';
      const int exprCount = peek["expressions"].is<JsonArray>()
                                ? (int)peek["expressions"].as<JsonArray>().size()
                                : 0;
      Serial.printf(
          "[cfg] loaded name=%s pw=%s expressions=%d nvs_bytes=%u\n",
          loadedName, hasPassword ? "set" : "unset", exprCount,
          (unsigned)json.length());
    } else {
      Serial.printf("[cfg] loaded nvs_bytes=%u (parse failed; full dump suppressed)\n",
                    (unsigned)json.length());
    }
  }
#endif

  if (error) {
#ifdef LAMP_DEBUG
    Serial.printf("ws deserializeJson() failed: %s\n", error.c_str());
#endif
    return;  // use class defaults
  }

  JsonObject lampNode = doc["lamp"];
  // lampType is firmware-owned (see Config::setLampType).
  // Inbound settings_blob writes intentionally do not update it — the
  // app can read but not write the variant identity.
  lamp.name = std::string(lampNode["name"] | "stray");
  lamp.brightness = lampNode["brightness"] | 100;
  std::string password = std::string(lampNode["password"] | "");
  if (!password.empty()) {
    lamp.password = password;
  }
  lamp.advancedEnabled = lampNode["advancedEnabled"] | false;
  lamp.devMode = lampNode["devMode"] | false;
  lamp.webappEnabled = lampNode["webappEnabled"] | true;
  // SocialMode persists as uint8_t (0=Introvert, 1=Ambivert, 2=Extrovert).
  // Out-of-range values fall back to Ambivert so a corrupt or future-versioned
  // payload doesn't strand the lamp in an unknown personality.
  {
    uint32_t modeRaw = lampNode["socialMode"] | 1;
    if (modeRaw > 2) modeRaw = 1;
    lamp.socialMode = static_cast<SocialMode>(modeRaw);
  }

  JsonObject baseNode = doc["base"];
  base.px = baseNode["px"] | 0;  // 0 = key absent → unset; applyDefaults fills from variant default
  if (base.px > 50) {
    base.px = 50;
  }
  // Keep knockoutPixels in sync with the active pixel count. Drops stale
  // entries when px shrinks (e.g. 35 → 20) and 100-fills ("no knockout")
  // any slots when px grows. The input loop below then overwrites slots
  // 0..base.px-1 from the JSON.
  base.knockoutPixels.resize(base.px, 100);
  base.ac = baseNode["ac"] | 0;
  base.bpp = baseNode["bpp"] | 4;
  if (base.bpp != 3 && base.bpp != 4) {
    base.bpp = 4;  // defensive: only 3 or 4 valid
  }
  // byteOrder is the source of truth for strip type. When absent (legacy
  // payloads), default-derive from bpp so behavior is unchanged for
  // existing lamps.
  const char* baseBoCstr = baseNode["byteOrder"] | "";
  base.byteOrder = baseBoCstr;
  if (base.byteOrder.empty()) {
    base.byteOrder = (base.bpp == 4) ? "GRBW" : "GRB";
  }

  JsonArray baseColors = baseNode["colors"];
  int colorCollectionSize = baseColors.size();
  if (base.ac > colorCollectionSize - 1) {
    base.ac = 0;
  }

  if (colorCollectionSize > 0) {
    base.colors.clear();
    for (JsonVariant baseColor : baseColors) {
      base.colors.push_back(hexStringToColor(baseColor));
    }
  }
  // Knockout: positional uint8 array, one entry per pixel. Index = pixel;
  // value = brightness % (0..100, default 100). Matches `asBaseJson` /
  // `asJsonDocument` emit shape.
  JsonArray baseKnockoutPixels = baseNode["knockout"];
  for (size_t i = 0;
       i < baseKnockoutPixels.size() && i < base.knockoutPixels.size();
       i++) {
    int value = baseKnockoutPixels[i] | 100;
    base.knockoutPixels[i] = (uint8_t)value;
  }

  JsonObject shadeNode = doc["shade"];
  shade.px = shadeNode["px"] | 0;  // 0 = key absent → unset; applyDefaults fills from variant default
  if (shade.px > 50) {
    shade.px = 50;
  }
  shade.bpp = shadeNode["bpp"] | 4;
  if (shade.bpp != 3 && shade.bpp != 4) {
    shade.bpp = 4;
  }
  const char* shadeBoCstr = shadeNode["byteOrder"] | "";
  shade.byteOrder = shadeBoCstr;
  if (shade.byteOrder.empty()) {
    shade.byteOrder = (shade.bpp == 4) ? "GRBW" : "GRB";
  }
  JsonArray shadeColors = shadeNode["colors"];
  if (shadeColors.size()) {
    shade.colors.clear();
    for (JsonVariant shadeColor : shadeColors) {
      shade.colors.push_back(hexStringToColor(shadeColor));
    }
  }
  // colorsEditable is firmware-owned (set by Lamp subclass defaults via
  // applyDefaults). Inbound writes intentionally do not update it.

  // Load expressions
  JsonArray expressionsNode = doc["expressions"];
  if (expressionsNode) {
    expressions.expressions.clear();
    for (JsonObject exprNode : expressionsNode) {
      ExpressionConfig expr;
      expr.type = std::string(exprNode["type"] | "");
      expr.enabled = exprNode["enabled"] | false;
      expr.intervalMin = exprNode["intervalMin"] | 60;
      expr.intervalMax = exprNode["intervalMax"] | 900;
      expr.target = exprNode["target"] | 3;
      // (Refactor 2026-06-13: disabledDuringWispOverride parse removed —
      // now a pure type-property on the Expression subclass; nothing to
      // load from NVS. Old NVS blobs with the key are tolerated; the
      // skip-list below drops it from the generic parameter loop so it
      // isn't accidentally captured as a parameter value.)
      // Load generic parameters
      for (JsonPair kv : exprNode) {
        const char* key = kv.key().c_str();
        std::string keyStr(key);

        // Skip common fields we've already handled
        if (keyStr == "type" || keyStr == "enabled" || keyStr == "intervalMin" ||
            keyStr == "intervalMax" || keyStr == "target" || keyStr == "colors" ||
            keyStr == "disabledDuringWispOverride") {
          continue;
        }

        // Store the parameter value
        JsonVariant value = kv.value();
        if (value.is<uint32_t>()) {
          expr.setParameter(keyStr, value.as<uint32_t>());
        } else if (value.is<int>()) {
          expr.setParameter(keyStr, static_cast<uint32_t>(value.as<int>()));
        }
      }

      JsonArray exprColors = exprNode["colors"];
      if (exprColors.size()) {
        for (JsonVariant color : exprColors) {
          expr.colors.push_back(hexStringToColor(color));
        }
      }

      expressions.expressions.push_back(expr);
    }
  }

  // Load home mode. Legacy NVS may still carry a "password" field — we
  // silently ignore it (presence-only home mode doesn't store passwords).
  JsonObject homeModeNode = doc["homeMode"];
  if (homeModeNode) {
    homeMode.ssid = std::string(homeModeNode["ssid"] | "");
    homeMode.brightness = homeModeNode["brightness"] | 60;
    // Migration: lamps configured before `enabled` existed have no
    // "enabled" key in their NVS-stored JSON. Treat "has SSID" as proxy
    // for "user wanted home mode on" so they keep working post-update.
    homeMode.enabled = homeModeNode["enabled"] | !homeMode.ssid.empty();
  }

  // (MQTT removed — legacy NVS "mqtt" block is silently ignored.)

  // Ensure both color vectors have at least one entry. Empty NVS (or NVS
  // erased / corrupted) returns "{}" with no colors arrays; downstream code
  // (e.g. bt.begin in lamp.cpp uses base.colors[ac] and shade.colors[0])
  // calls operator[] on a std::vector, which is UB on empty and crashes boot
  // with an invalid-PC fault. Default to a visible white so the lamp at least
  // boots and the user can adjust colors via the app.
  if (base.colors.empty()) {
    base.colors.push_back(Color{255, 255, 255, 0});
  }
  if (shade.colors.empty()) {
    shade.colors.push_back(Color{255, 255, 255, 0});
  }
  if (base.ac >= base.colors.size()) {
    base.ac = 0;
  }

  // Per-peer dispositions live in a separate NVS key — loaded last so the
  // main blob's prefs->begin/end pair above doesn't conflict.
  loadDispositionsFromPrefs_();
};

void Config::loadDispositionsFromPrefs_() {
  if (!prefs) return;
  prefs->begin("lamp", true);
  String json = prefs->getString("dispositions", "{}");
  prefs->end();

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) return;

  // Two-pass load: gather into a fresh vector, then sort once. This is
  // O(N log N) vs O(N^2) if we used setDisposition() in a loop (each
  // call would memmove the tail on insert). Reserve to skip reallocation
  // for typical loads.
  dispositions_.clear();
  dispositions_.reserve(kDispositionsMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (dispositions_.size() >= kDispositionsMax) break;
    const char* key = kv.key().c_str();
    // Dispositions are keyed by BD_ADDR. Legacy name-keyed entries
    // (from older firmware) fail this filter and are silently dropped.
    // The next disposition write naturally overwrites the NVS blob in
    // the new shape (no separate migration code path).
    if (!isValidBdAddr(key)) continue;
    uint32_t v = kv.value() | (uint32_t)kDispositionDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    dispositions_.emplace_back(std::string(key), static_cast<uint8_t>(v));
  }
  std::sort(dispositions_.begin(), dispositions_.end(),
            [](const std::pair<std::string, uint8_t>& a,
               const std::pair<std::string, uint8_t>& b) {
              return a.first < b.first;
            });
  // Dedupe defensively — JSON objects don't have duplicate keys per spec,
  // but ArduinoJson is permissive on bad input. Last write wins, matching
  // the prior std::map behaviour.
  auto last = std::unique(
      dispositions_.begin(), dispositions_.end(),
      [](const std::pair<std::string, uint8_t>& a,
         const std::pair<std::string, uint8_t>& b) { return a.first == b.first; });
  dispositions_.erase(last, dispositions_.end());
}

bool Config::persistConfig(const char* via) {
  if (!prefs) return false;
  JsonDocument doc = asJsonDocument();
  String out;
  serializeJson(doc, out);
  // Match persistDispositions_'s defensive pattern — prefs.begin can fail
  // when NVS is full or the partition is corrupt; a putString against an
  // unopened handle silently writes nothing. Skip the write and let the
  // caller decide (callers today just move on; expression edits stay in
  // RAM and the next persistConfig attempt may succeed).
  if (!prefs->begin("lamp", false)) {
#ifdef LAMP_DEBUG
    Serial.println("[nvs] prefs.begin failed (persistConfig)");
#endif
    return false;
  }
  size_t written = prefs->putString("cfg", out.c_str());
  prefs->end();
#ifdef LAMP_DEBUG
  if (written == 0) {
    Serial.println("[nvs] persistConfig putString wrote 0 bytes");
  } else {
    Serial.printf("[nvs] persistConfig via=%s wrote %u bytes\n",
                  via, (unsigned)written);
  }
#endif
  return written > 0;
}

void Config::setLampType(const std::string& type) {
  lamp.lampType = type;
  if (!prefs) return;
  if (!prefs->begin("lamp", false)) {
#ifdef LAMP_DEBUG
    Serial.println("[nvs] prefs.begin failed (setLampType)");
#endif
    return;
  }
  prefs->putString("lampType", String(type.c_str()));
  prefs->end();
}

std::string Config::loadLampType() {
  lamp.lampType = "";
  if (!prefs) return "";
  if (!prefs->begin("lamp", true)) {
#ifdef LAMP_DEBUG
    Serial.println("[nvs] prefs.begin failed (loadLampType)");
#endif
    return "";
  }
  String t = prefs->getString("lampType", "");
  prefs->end();
  lamp.lampType = std::string(t.c_str());
  return lamp.lampType;
}

bool Config::persistDispositions_() {
  if (!prefs) return false;
  String out = asDispositionsJson();
  // prefs.begin returns false when NVS is full or the partition
  // is corrupt. A subsequent putString against an unopened handle silently
  // writes to nothing. Skip the write and let the debouncer keep its dirty
  // flag so the next flush attempt retries.
  if (!prefs->begin("lamp", false)) {
#ifdef LAMP_DEBUG
    Serial.println("[nvs] prefs.begin failed (dispositions persist)");
#endif
    return false;
  }
  size_t written = prefs->putString("dispositions", out.c_str());
  prefs->end();
#ifdef LAMP_DEBUG
  if (written == 0) {
    Serial.println("[nvs] dispositions putString wrote 0 bytes");
  }
#endif
  return written > 0;
}

uint8_t Config::getDisposition(const std::string& bdAddr) const {
  // Binary search on the sorted vector. lower_bound returns the first
  // entry >= bdAddr; we must still compare keys because lower_bound
  // can land on a strictly-greater neighbour (e.g. looking up "BB..." in
  // {AA..., CC...} returns the iterator to CC...).
  auto it = std::lower_bound(dispositions_.begin(), dispositions_.end(),
                             bdAddr, dispositionEntryLess);
  if (it == dispositions_.end() || it->first != bdAddr) {
    return kDispositionDefault;
  }
  return it->second;
}

void Config::setDisposition(const std::string& bdAddr, uint8_t value) {
  if (bdAddr.empty()) return;
  if (value < 1) value = 1;
  if (value > 5) value = 5;
  auto it = std::lower_bound(dispositions_.begin(), dispositions_.end(),
                             bdAddr, dispositionEntryLess);
  if (it != dispositions_.end() && it->first == bdAddr) {
    // Update in place — no resize, no shift, no eviction. Preserves sort
    // order trivially.
    it->second = value;
  } else {
    if (dispositions_.size() >= kDispositionsMax) {
      // Evict the lowest-by-key entry to match the historical std::map
      // iteration-order eviction policy. Disposition tracking is
      // best-effort at the cap; users typically have <100 paired lamps.
      dispositions_.erase(dispositions_.begin());
      // The insertion point may have shifted by one after erase; recompute.
      it = std::lower_bound(dispositions_.begin(), dispositions_.end(),
                            bdAddr, dispositionEntryLess);
    }
    dispositions_.insert(it, std::make_pair(bdAddr, value));
  }
  // Do NOT persist here. The slider-drag UX produced ~20 writes per
  // peer per drag; multiplied across multiple peers and years of
  // ownership, the 100k-write-per-page NVS budget is reachable.
  // The loop drain on Core 1 polls maybeFlushDispositions and writes
  // once the user stops touching the slider for kDispositionFlushIdleMs.
  // BLE disconnect path force-flushes via flushDispositionsNow. Factory
  // reset doesn't need a flush — it erases NVS wholesale.
  dispositionsDebouncer_.markDirty(millis());
}

String Config::asDispositionsJson() const {
  // Sorted-vector iteration yields keys in lexicographic order — stable
  // round-trips across reads (the prior std::map also iterated in sorted
  // order, so on-disk byte shape is unchanged).
  JsonDocument doc;
  for (const auto& kv : dispositions_) {
    doc[kv.first.c_str()] = kv.second;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool Config::setDispositionsFromJson(const char* json, size_t len) {
  JsonDocument doc;
  if (deserializeJson(doc, json, len) != DeserializationError::Ok) {
    return false;
  }
  if (!doc.is<JsonObject>()) return false;
  // Bulk replace: stage into a fresh local vector, then sort once. Same
  // O(N log N) strategy as loadDispositionsFromPrefs_ — avoids repeated
  // O(N) shifts that an N-call sequence of setDisposition() would incur.
  std::vector<std::pair<std::string, uint8_t>> next;
  next.reserve(kDispositionsMax);
  for (JsonPair kv : doc.as<JsonObject>()) {
    if (next.size() >= kDispositionsMax) break;
    const char* key = kv.key().c_str();
    // BD_ADDR-only keys. Reject anything that doesn't look like
    // "AA:BB:CC:DD:EE:FF" — covers the edge case of a stale app writing
    // a name-keyed map, plus any malformed user input via reverse-engineered
    // BLE write paths.
    if (!isValidBdAddr(key)) continue;
    uint32_t v = kv.value() | (uint32_t)kDispositionDefault;
    if (v < 1) v = 1;
    if (v > 5) v = 5;
    next.emplace_back(std::string(key), static_cast<uint8_t>(v));
  }
  std::sort(next.begin(), next.end(),
            [](const std::pair<std::string, uint8_t>& a,
               const std::pair<std::string, uint8_t>& b) {
              return a.first < b.first;
            });
  auto last = std::unique(
      next.begin(), next.end(),
      [](const std::pair<std::string, uint8_t>& a,
         const std::pair<std::string, uint8_t>& b) { return a.first == b.first; });
  next.erase(last, next.end());
  dispositions_ = std::move(next);
  // Defer persistence. CHAR_SOCIAL_DISPOSITIONS bulk writes
  // arrive on every slider drag from the app, each re-serialising the
  // full blob — the worst case for NVS wear. The Core 1 loop drain
  // (maybeFlushDispositions) commits once idle; the BLE onDisconnect
  // post forces a synchronous commit when the phone walks away.
  dispositionsDebouncer_.markDirty(millis());
  return true;
}

void Config::maybeFlushDispositions(uint32_t nowMs) {
  if (!dispositionsDebouncer_.shouldFlush(nowMs)) return;
  // Only clear the debouncer on a successful write; on failure leave it
  // dirty so the next flush attempt retries (safer default — the user's
  // slider input has nowhere else to go).
  if (persistDispositions_()) {
    dispositionsDebouncer_.clear();
  }
}

void Config::flushDispositionsNow() {
  if (!dispositionsDebouncer_.dirty()) return;
  if (persistDispositions_()) {
    dispositionsDebouncer_.clear();
  }
}

JsonDocument Config::asJsonDocument() {
  JsonDocument doc;

  JsonObject lampNode = doc["lamp"].to<JsonObject>();
  lampNode["name"] = lamp.name;
  lampNode["brightness"] = lamp.brightness;
  if (!lamp.password.empty()) {
    lampNode["password"] = lamp.password;
  }
  lampNode["advancedEnabled"] = lamp.advancedEnabled;
  lampNode["devMode"] = lamp.devMode;
  lampNode["webappEnabled"] = lamp.webappEnabled;
  lampNode["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  JsonObject baseNode = doc["base"].to<JsonObject>();
  baseNode["px"] = base.px;
  baseNode["ac"] = base.ac;
  baseNode["bpp"] = base.bpp;
  baseNode["byteOrder"] = base.byteOrder;
  JsonArray baseColorsNode = baseNode["colors"].to<JsonArray>();
  for (int i = 0; i < base.colors.size(); i++) {
    baseColorsNode[i] = colorToHexString(base.colors[i]);
  }
  // Positional uint8 array, same shape as asBaseJson — keeps the on-disk
  // NVS format consistent with the BLE per-section read.
  JsonArray baseKnockoutNode = baseNode["knockout"].to<JsonArray>();
  for (int i = 0; i < base.knockoutPixels.size(); i++) {
    baseKnockoutNode.add((int)base.knockoutPixels[i]);
  }

  JsonObject shadeNode = doc["shade"].to<JsonObject>();
  shadeNode["px"] = shade.px;
  shadeNode["bpp"] = shade.bpp;
  shadeNode["byteOrder"] = shade.byteOrder;
  JsonArray shadeColorsNode = shadeNode["colors"].to<JsonArray>();
  for (int i = 0; i < shade.colors.size(); i++) {
    shadeColorsNode[i] = colorToHexString(shade.colors[i]);
  }

  // Serialize expressions
  JsonArray expressionsNode = doc["expressions"].to<JsonArray>();
  for (const auto& expr : expressions.expressions) {
    JsonObject exprNode = expressionsNode.add<JsonObject>();
    exprNode["type"] = expr.type;
    exprNode["enabled"] = expr.enabled;
    exprNode["intervalMin"] = expr.intervalMin;
    exprNode["intervalMax"] = expr.intervalMax;
    exprNode["target"] = expr.target;
    // disabledDuringWispOverride is no longer persisted — pure type-property.
    // Serialize generic parameters
    for (const auto& param : expr.parameters) {
      const std::string& key = param.first;
      const uint32_t& value = param.second;
      exprNode[key] = value;
    }

    JsonArray colorsNode = exprNode["colors"].to<JsonArray>();
    for (const auto& color : expr.colors) {
      colorsNode.add(colorToHexString(color));
    }
  }

  JsonObject homeModeNode = doc["homeMode"].to<JsonObject>();
  homeModeNode["ssid"] = homeMode.ssid;
  homeModeNode["brightness"] = homeMode.brightness;
  homeModeNode["enabled"] = homeMode.enabled;

  return doc;
};

String Config::asLampJson() {
  JsonDocument doc;
  doc["name"] = lamp.name;
  doc["brightness"] = lamp.brightness;
  if (!lamp.password.empty()) {
    doc["password"] = lamp.password;
  }
  // Authoritative password-set state. Lets the app detect divergence
  // (its cached controlPassword is non-empty but NVS pw was wiped) and
  // auto-clear its cache so subsequent settings_blob writes go through
  // the plaintext branch instead of failing decrypt silently.
  doc["hasPassword"] = !lamp.password.empty();
  doc["advancedEnabled"] = lamp.advancedEnabled;
  doc["devMode"] = lamp.devMode;
  doc["webappEnabled"] = lamp.webappEnabled;
  doc["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  // Firmware identity (packed semver + release channel string). Constant
  // at boot, so no extra invalidation hook is needed — the existing lamp
  // section cache picks these up the first time it's built.
  doc["fwVersion"] = FIRMWARE_VERSION;
  doc["fwChannel"] = FIRMWARE_CHANNEL_STR;
  // lampType is firmware-owned; the app can read it but settings_blob
  // writes do NOT update it (see parser block below).
  doc["lampType"] = lamp.lampType;
  String out;
  serializeJson(doc, out);
  return out;
}

String Config::asBaseJson() {
  JsonDocument doc;
  doc["px"] = base.px;
  doc["ac"] = base.ac;
  doc["bpp"] = base.bpp;
  doc["byteOrder"] = base.byteOrder;
  JsonArray colorsNode = doc["colors"].to<JsonArray>();
  for (size_t i = 0; i < base.colors.size(); i++) {
    colorsNode.add(colorToHexString(base.colors[i]));
  }
  // Knockout: positional uint8 array, one entry per pixel.
  //   wire shape: "knockout":[100,100,6,9,...] (length = knockoutPixels.size())
  // Index = pixel; value = brightness % (0..100). Caps the base section at
  // ~200 bytes regardless of pattern density so a dense knockout fits
  // under the per-characteristic ATT cap.
  JsonArray knockoutNode = doc["knockout"].to<JsonArray>();
  for (size_t i = 0; i < base.knockoutPixels.size(); i++) {
    knockoutNode.add((int)base.knockoutPixels[i]);
  }
  // colorsEditable is firmware-owned (set by Lamp subclass via applyDefaults).
  // Emitted so the app can hide the color picker for surfaces the subclass
  // has opted out of (e.g. SnafuLamp shade).
  doc["colorsEditable"] = base.colorsEditable;
  String out;
  serializeJson(doc, out);
  return out;
}

String Config::asShadeJson() {
  JsonDocument doc;
  doc["px"] = shade.px;
  doc["bpp"] = shade.bpp;
  doc["byteOrder"] = shade.byteOrder;
  JsonArray colorsNode = doc["colors"].to<JsonArray>();
  for (size_t i = 0; i < shade.colors.size(); i++) {
    colorsNode.add(colorToHexString(shade.colors[i]));
  }
  // colorsEditable is firmware-owned (set by Lamp subclass via applyDefaults).
  // Emitted so the app can hide the color picker for surfaces the subclass
  // has opted out of (e.g. SnafuLamp shade).
  doc["colorsEditable"] = shade.colorsEditable;
  String out;
  serializeJson(doc, out);
  return out;
}

String Config::asExpressionsJson() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& expr : expressions.expressions) {
    JsonObject exprNode = arr.add<JsonObject>();
    exprNode["type"] = expr.type;
    exprNode["enabled"] = expr.enabled;
    exprNode["intervalMin"] = expr.intervalMin;
    exprNode["intervalMax"] = expr.intervalMax;
    exprNode["target"] = expr.target;
    // disabledDuringWispOverride is no longer serialised — pure type-property.
    for (const auto& param : expr.parameters) {
      exprNode[param.first] = param.second;
    }
    JsonArray colorsNode = exprNode["colors"].to<JsonArray>();
    for (const auto& color : expr.colors) {
      colorsNode.add(colorToHexString(color));
    }
  }
  String out;
  serializeJson(doc, out);
  return out;
}

String Config::asHomeModeJson() {
  JsonDocument doc;
  doc["ssid"] = homeMode.ssid;
  doc["brightness"] = homeMode.brightness;
  doc["enabled"] = homeMode.enabled;
  String out;
  serializeJson(doc, out);
  return out;
}

// ── Per-section JSON cache ───────────────────────────────────────────────
//
// Each accessor:
//   - If the section is clean, returns the existing cached std::string ref
//     in O(1) — no JsonDocument allocation, no vector walk.
//   - If dirty, calls the existing asXJson() builder (which itself builds
//     a JsonDocument + walks colors/knockoutPixels/etc.), copies into the
//     member std::string, clears the dirty flag.
//
// Thread safety: the page-protocol path on Core 0 calls the cached
// accessors from `PageCtrlCallback::onWrite` (NimBLE host task). Core 1
// mutates source data via drains and calls `invalidateXSection()` /
// proactively rebuilds via `ble_control::tick`. The portMUX below
// serialises the rebuild itself so two cores can't `.assign()` the same
// string concurrently. Source-data reads inside the rebuild lambda are
// taken under the same mux so a torn read can't slip into the JSON.
static portMUX_TYPE s_cacheMux = portMUX_INITIALIZER_UNLOCKED;

#define LAMP_DEFINE_SECTION_CACHED(NAME, BUILDER)                          \
  const std::string& Config::NAME##SectionJsonCached() {                   \
    if (NAME##SectionDirty_) {                                             \
      portENTER_CRITICAL(&s_cacheMux);                                     \
      if (NAME##SectionDirty_) {                                           \
        String s = BUILDER();                                              \
        NAME##SectionJson_.assign(s.c_str(), s.length());                  \
        NAME##SectionDirty_ = false;                                       \
      }                                                                    \
      portEXIT_CRITICAL(&s_cacheMux);                                      \
    }                                                                      \
    return NAME##SectionJson_;                                             \
  }

LAMP_DEFINE_SECTION_CACHED(lamp, asLampJson)
LAMP_DEFINE_SECTION_CACHED(base, asBaseJson)
LAMP_DEFINE_SECTION_CACHED(shade, asShadeJson)
LAMP_DEFINE_SECTION_CACHED(expressions, asExpressionsJson)
LAMP_DEFINE_SECTION_CACHED(home, asHomeModeJson)

#undef LAMP_DEFINE_SECTION_CACHED

void Config::invalidateLampSection() {
  lampSectionDirty_ = true;
}

void Config::invalidateBaseSection() {
  baseSectionDirty_ = true;
}

void Config::invalidateShadeSection() {
  shadeSectionDirty_ = true;
}

void Config::invalidateExpressionsSection() {
  expressionsSectionDirty_ = true;
}

void Config::invalidateHomeSection() {
  homeSectionDirty_ = true;
}

void Config::invalidateAllSections() {
  lampSectionDirty_ = true;
  baseSectionDirty_ = true;
  shadeSectionDirty_ = true;
  expressionsSectionDirty_ = true;
  homeSectionDirty_ = true;
}

void Config::applyDefaults(const Defaults& d) {
  // Called AFTER Config::Config(Preferences*) loads from NVS so that NVS
  // values are authoritative and only truly-factory-default fields are
  // overwritten. Subclass defaults() returns values that are used as
  // first-boot baselines.
  //
  // Name: the Config class default is "stray". If NVS had no name (empty
  // NVS) the loaded value is still "stray". We replace it with the subclass
  // preferred name.
  if (!d.name.empty() && lamp.name == "stray") {
    lamp.name = d.name;
  }
  // Colors: apply subclass default when the vector contains only a single
  // entry that matches either the class-member default or the defensive
  // white fallback inserted by Config::Config(Preferences*) for unconfigured
  // lamps (empty NVS). A lamp that has had its colors configured by the user
  // will have a different color saved in NVS and won't match these guards.
  //
  // Class-member defaults:
  //   BaseSettings:  Color(0x30, 0x07, 0x83, 0x00)
  //   ShadeSettings: Color(0x00, 0x00, 0x00, 0xFF)
  // Defensive NVS fallback (after empty NVS load): Color{255, 255, 255, 0}
  static const Color kBaseClassDefault(0x30, 0x07, 0x83, 0x00);
  static const Color kShadeClassDefault(0x00, 0x00, 0x00, 0xFF);
  static const Color kNvsFallback(0xFF, 0xFF, 0xFF, 0x00);
  if (!d.baseColor.empty() && base.colors.size() == 1 &&
      (base.colors[0] == kBaseClassDefault || base.colors[0] == kNvsFallback)) {
    base.colors[0] = hexStringToColor(d.baseColor.c_str());
  }
  if (!d.shadeColor.empty() && shade.colors.size() == 1 &&
      (shade.colors[0] == kShadeClassDefault || shade.colors[0] == kNvsFallback)) {
    shade.colors[0] = hexStringToColor(d.shadeColor.c_str());
  }
  base.colorsEditable  = d.baseColorsEditable;
  shade.colorsEditable = d.shadeColorsEditable;

  // Per-surface pixel count. A loaded px of 0 means "unset" — a fresh lamp
  // (loader early-returned with the class-default 0) or a doc without the
  // key. Fill those from the variant default; any real stored value wins,
  // so a configured lamp's px is sacred. (The old guard inferred unset from
  // magic baseline numbers and clobbered a user who legitimately saved a
  // value equal to one — that was the "save didn't take" bug.)
  base.px = resolveConfiguredPx(base.px, d.basePx);
  base.knockoutPixels.resize(base.px, 100);
  shade.px = resolveConfiguredPx(shade.px, d.shadePx);
}

}  // namespace lamp
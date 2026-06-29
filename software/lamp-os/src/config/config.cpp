#include "config.hpp"

#include <ArduinoJson.h>

#include <algorithm>

#include "config/config_codec.hpp"

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
Config::Config(ConfigStore* inStore) {
  JsonDocument doc;
  store_ = inStore;
  std::string json = store_->read("cfg", "{}");
  DeserializationError error = deserializeJson(doc, json);

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

  config_codec::fromJson(doc.as<JsonObject>(), lamp, base, shade, expressions,
                         homeMode);

  // Per-peer dispositions live in a separate NVS key.
  loadDispositionsFromPrefs_();
};

void Config::loadDispositionsFromPrefs_() {
  if (!store_) return;
  std::string json = store_->read("dispositions", "{}");

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
  if (!store_) return false;
  JsonDocument doc = asJsonDocument();
  String out;
  serializeJson(doc, out);
  // A store write returns 0 bytes when NVS is full or the partition is
  // corrupt; the caller decides what to do (callers today just move on;
  // expression edits stay in RAM and the next attempt may succeed).
  size_t written = store_->write("cfg", out.c_str());
#ifdef LAMP_DEBUG
  if (written == 0) {
    Serial.println("[nvs] persistConfig wrote 0 bytes");
  } else {
    Serial.printf("[nvs] persistConfig via=%s wrote %u bytes\n",
                  via, (unsigned)written);
  }
#endif
  return written > 0;
}

bool Config::persistRawJson(const char* json) {
  if (!store_) return false;
  return store_->write("cfg", json) > 0;
}

bool Config::factoryReset() { return store_ ? store_->clear() : false; }

void Config::setLampType(const std::string& type) {
  lamp.lampType = type;
  if (!store_) return;
  store_->write("lampType", type.c_str());
}

std::string Config::loadLampType() {
  lamp.lampType = "";
  if (!store_) return "";
  lamp.lampType = store_->read("lampType", "");
  return lamp.lampType;
}

bool Config::persistDispositions_() {
  if (!store_) return false;
  String out = asDispositionsJson();
  // A 0-byte write means NVS is full or corrupt; leave the debouncer's dirty
  // flag set so the next flush attempt retries.
  size_t written = store_->write("dispositions", out.c_str());
#ifdef LAMP_DEBUG
  if (written == 0) {
    Serial.println("[nvs] dispositions wrote 0 bytes");
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
  config_codec::toJson(doc.to<JsonObject>(), lamp, base, shade, expressions,
                       homeMode);
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
  // Called AFTER Config::Config(ConfigStore*) loads from NVS so that NVS
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
  // Colors: apply the variant's injected default when the vector still holds
  // only the single class-default entry. A user-configured lamp has a
  // different saved color and won't match, so its choice is preserved.
  if (!d.baseColor.empty() && base.colors.size() == 1 &&
      base.colors[0] == kBaseDefaultColor) {
    base.colors[0] = hexStringToColor(d.baseColor.c_str());
  }
  if (!d.shadeColor.empty() && shade.colors.size() == 1 &&
      shade.colors[0] == kShadeDefaultColor) {
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
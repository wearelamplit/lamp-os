#include "config.hpp"

#include <ArduinoJson.h>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config/config_codec.hpp"
#include "core/lamp.hpp"
#include "util/color.hpp"
#include "version.hpp"

namespace lamp {

// Seed every segment of a role from the variant defaults, but only while NVS
// still holds the single class-default segment. A configured lamp (NVS parsed
// its own segment list) keeps its saved segments untouched.
static void seedRoleSegments(std::vector<SegmentSettings>& segs,
                             const std::vector<Config::SegmentDefault>& seeds,
                             const Color& factory) {
  const bool factoryUnconfigured =
      segs.size() == 1 && segs[0].colors.size() == 1 && segs[0].colors[0] == factory;
  if (!factoryUnconfigured) return;
  segs.clear();
  for (const auto& s : seeds) {
    segs.push_back({s.name, s.px, parseColorList(s.colors)});
  }
}

Config::Config(ConfigStore* inStore) {
  JsonDocument doc;
  store_ = inStore;
  dispositions_.attachStore(store_);
  std::string json = store_->read("cfg", "{}");
  DeserializationError error = deserializeJson(doc, json);

#ifdef LAMP_DEBUG
  // Print a compact, secret-free summary instead of the raw NVS JSON.
  // The raw payload would leak `lamp.password` to anyone on the serial
  // console (USB physical access). If you need the full shape while
  // debugging a parse bug, attach a temporary `Serial.println(json)`
  // for that session; don't roll back this redaction.
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
  dispositions_.load();
};

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

void Config::setLampId(const std::string& lampId) {
  if (lampId == lampId_) return;
  lampId_ = lampId;
  invalidateLampSection();
}

uint8_t Config::getDisposition(const std::string& lampId) const {
  return dispositions_.get(lampId);
}

void Config::setDisposition(const std::string& lampId, uint8_t value) {
  dispositions_.set(lampId, value, millis());
}

String Config::asDispositionsJson() const {
  return String(dispositions_.asJson().c_str());
}

bool Config::setDispositionsFromJson(const char* json, size_t len) {
  return dispositions_.setFromJson(json, len, millis());
}

void Config::maybeFlushDispositions(uint32_t nowMs) {
  dispositions_.maybeFlush(nowMs);
}

void Config::flushDispositionsNow() { dispositions_.flushNow(); }

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
  doc["webappEnabled"] = lamp.webappEnabled;
  doc["brightnessCeiling"] = lamp.brightnessCeiling;
  doc["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  // Firmware identity (packed semver + release channel string). Constant at
  // boot, so no extra invalidation hook is needed; the lamp section cache
  // picks these up the first time it's built.
  doc["fwVersion"] = FIRMWARE_VERSION;
  doc["fwChannel"] = FIRMWARE_CHANNEL_STR;
  // lampType is firmware-owned; the app can read it but settings_blob
  // writes do NOT update it (see parser block below).
  doc["lampType"] = lamp.lampType;
  if (!lampId_.empty()) {
    doc["lampId"] = lampId_;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

// Role sections advertise topology: a role-level `px` = Σ segment px (the
// app's scalar readers size off it) alongside the per-segment list. Both are
// emitted; the app reads whichever it needs.
static void emitRoleTopology(JsonDocument& doc,
                             const std::vector<SegmentSettings>& segs) {
  doc["px"] = segmentsSumPx(segs);
  JsonArray colorsNode = doc["colors"].to<JsonArray>();
  for (const auto& c : segs.front().colors) colorsNode.add(colorToHexString(c));
  JsonArray segsNode = doc["segments"].to<JsonArray>();
  for (const auto& s : segs) {
    JsonObject o = segsNode.add<JsonObject>();
    o["name"] = s.name;
    o["px"] = s.px;
    JsonArray sc = o["colors"].to<JsonArray>();
    for (const auto& c : s.colors) sc.add(colorToHexString(c));
  }
}

String Config::asBaseJson() {
  JsonDocument doc;
  emitRoleTopology(doc, base.segments);
  doc["ac"] = base.ac;
  doc["byteOrder"] = base.byteOrder;
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
  doc["drawIdleMa"] = drawIdleMa_;
  doc["drawFullMa"] = drawFullMa_;
  String out;
  serializeJson(doc, out);
  return out;
}

String Config::asShadeJson() {
  JsonDocument doc;
  emitRoleTopology(doc, shade.segments);
  doc["byteOrder"] = shade.byteOrder;
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
  doc["networkBound"] = homeMode.networkBound;
  doc["socialDisabled"] = homeMode.socialDisabled;
  JsonArray disArr = doc["disabledExpressionTypes"].to<JsonArray>();
  for (const auto& s : homeMode.disabledExpressionTypes) disArr.add(s);
  String out;
  serializeJson(doc, out);
  return out;
}

// Per-section JSON cache accessors. Rebuild-if-dirty and the reader's
// copy-out share one mutex; without it a reader on one core can copy the
// member string while a rebuild on the other core reallocates it.
static SemaphoreHandle_t cacheMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

// A field OOM must stay visible without spamming: warn on the degrade path
// on every channel, throttled to one line per LAMP_SECTION_OOM_MIN_MS.
static void logSectionOom(const char* msg) {
  static constexpr uint32_t LAMP_SECTION_OOM_MIN_MS = 5000;
  static uint32_t lastMs = 0;
  const uint32_t now = millis();
  if (now - lastMs < LAMP_SECTION_OOM_MIN_MS && lastMs != 0) return;
  lastMs = now;
  Serial.printf("[cfg] %s section OOM; serving stale\n", msg);
}

// A web/BLE section read must never panic the lamp: under a fragmented heap
// the rebuild's string alloc can throw bad_alloc. On failure keep the
// last-good cache, leave the dirty flag set so it retries when heap recovers,
// and serve the stale (or empty) bytes. Catch only bad_alloc so a genuine
// logic exception propagates instead of degrading to stale JSON.
#define LAMP_DEFINE_SECTION_CACHED(NAME, BUILDER)                          \
  void Config::NAME##SectionRebuildIfDirty() {                             \
    xSemaphoreTake(cacheMutex(), portMAX_DELAY);                           \
    if (NAME##SectionDirty_) {                                             \
      try {                                                                \
        String s = BUILDER();                                             \
        NAME##SectionJson_.assign(s.c_str(), s.length());                 \
        NAME##SectionDirty_ = false;                                       \
      } catch (const std::bad_alloc&) {                                    \
        logSectionOom(#NAME);                                              \
      }                                                                    \
    }                                                                      \
    xSemaphoreGive(cacheMutex());                                          \
  }                                                                        \
  void Config::NAME##SectionJsonCached(std::string& out) {                 \
    NAME##SectionRebuildIfDirty();                                         \
    xSemaphoreTake(cacheMutex(), portMAX_DELAY);                           \
    try {                                                                  \
      out = NAME##SectionJson_;                                            \
    } catch (const std::bad_alloc&) {                                      \
      out.clear();                                                         \
      logSectionOom(#NAME);                                                \
    }                                                                      \
    xSemaphoreGive(cacheMutex());                                          \
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

void Config::setDrawAnchors(uint16_t idleMa, uint16_t fullMa) {
  if (idleMa == drawIdleMa_ && fullMa == drawFullMa_) return;
  drawIdleMa_ = idleMa;
  drawFullMa_ = fullMa;
  invalidateBaseSection();
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
  // NVS) the loaded value is still "stray". Replace it with the subclass
  // preferred name.
  if (!d.name.empty() && lamp.name == "stray") {
    lamp.name = d.name;
  }
  // Colors: apply the variant's injected default when the vector still holds
  // only the single class-default entry. A user-configured lamp has a
  // different saved color and won't match, so its choice is preserved. A
  // multi-segment variant seeds its whole segment list instead of the scalar
  // single-color path.
  if (!d.baseSegments.empty()) {
    seedRoleSegments(base.segments, d.baseSegments, kBaseDefaultColor);
  } else if (!d.baseColor.empty() && base.broadcastColors().size() == 1 &&
             base.broadcastColors()[0] == kBaseDefaultColor) {
    base.broadcastColors() = parseColorList(d.baseColor);
  }
  if (!d.shadeSegments.empty()) {
    seedRoleSegments(shade.segments, d.shadeSegments, kShadeDefaultColor);
  } else if (!d.shadeColor.empty() && shade.broadcastColors().size() == 1 &&
             shade.broadcastColors()[0] == kShadeDefaultColor) {
    shade.broadcastColors() = parseColorList(d.shadeColor);
  }
  base.colorsEditable  = d.baseColorsEditable;
  shade.colorsEditable = d.shadeColorsEditable;
  if (d.setup) lamp.setup = true;

  // Per-segment pixel count. A loaded px of 0 means "unset" (a fresh lamp, or
  // a doc without the key). Fill those from the variant default; any real
  // stored value wins, so a configured lamp's px is sacred. The scalar
  // basePx/shadePx default seeds the role's first segment.
  base.segments.front().px = resolveConfiguredPx(base.segments.front().px, d.basePx);
  base.knockoutPixels.resize(base.sumPx(), 100);
  shade.segments.front().px = resolveConfiguredPx(shade.segments.front().px, d.shadePx);
}

}  // namespace lamp
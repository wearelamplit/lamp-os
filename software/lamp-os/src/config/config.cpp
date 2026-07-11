#include "config.hpp"

#include <ArduinoJson.h>

#include <sstream>

#include "config/config_codec.hpp"
#include "core/lamp.hpp"
#include "util/color.hpp"
#include "version.hpp"

namespace lamp {

// Split csv on commas, parse each trimmed token as a #RRGGBBWW hex color,
// return the non-empty results.
static std::vector<Color> parseColorList(const std::string& csv) {
  std::vector<Color> out;
  std::istringstream stream(csv);
  std::string token;
  while (std::getline(stream, token, ',')) {
    size_t start = token.find_first_not_of(' ');
    size_t end   = token.find_last_not_of(' ');
    if (start == std::string::npos) continue;
    out.push_back(hexStringToColor(token.substr(start, end - start + 1)));
  }
  return out;
}

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

uint8_t Config::getDisposition(const std::string& bdAddr) const {
  return dispositions_.get(bdAddr);
}

void Config::setDisposition(const std::string& bdAddr, uint8_t value) {
  dispositions_.set(bdAddr, value, millis());
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
  doc["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  // Firmware identity (packed semver + release channel string). Constant at
  // boot, so no extra invalidation hook is needed; the lamp section cache
  // picks these up the first time it's built.
  doc["fwVersion"] = FIRMWARE_VERSION;
  doc["fwChannel"] = FIRMWARE_CHANNEL_STR;
  // lampType is firmware-owned; the app can read it but settings_blob
  // writes do NOT update it (see parser block below).
  doc["lampType"] = lamp.lampType;
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
  emitRoleTopology(doc, shade.segments);
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
  String out;
  serializeJson(doc, out);
  return out;
}

// Per-section JSON cache accessors. Clean section: return the cached string
// in O(1). Dirty: rebuild via asXJson(), copy into the member string, clear
// the flag. The portMUX serialises the rebuild so two cores can't .assign()
// the same string concurrently. Torn source reads are avoided by discipline,
// not the mux: a section is invalidated only at commit/boot (quiescent), never
// mid-live-write, so the rebuild never reads a source another core is mutating.
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
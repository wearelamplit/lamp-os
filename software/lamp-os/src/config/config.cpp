#include "config.hpp"

#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "config/config_codec.hpp"
#include "core/crash_record.hpp"
#include "core/lamp.hpp"
#include "expr_catalog.h"
#include "util/bd_addr.hpp"
#include "util/color.hpp"
#include "util/heap_guard.hpp"
#include "util/json_writer.hpp"
#include "util/uptime.hpp"
#include "version.hpp"

#include <lampos/protocol/presence.hpp>

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

// The stored config is read and written through this reserved buffer, so a
// load or save never waits for a large free block. Loop task only: the load
// runs in setup, and every save, the webapp's included (webapp::tick drains
// it), runs there.
static uint8_t s_settingsBuf[config_codec::kMaxSettingsBlobLen];

Config::Config(ConfigStore* inStore) {
  store_ = inStore;
  dispositions_.attachStore(store_);
  const config_codec::LoadedConfig loaded = config_codec::loadConfig(
      *store_, lamp, base, shade, expressions, homeMode, s_settingsBuf, sizeof(s_settingsBuf));
  storeForm_ = loaded.form;

#ifdef LAMP_DEBUG
  // A compact summary with no secrets. The raw store would leak `lamp.password`
  // to anyone on the serial console (USB physical access).
  if (!loaded.parseFailed) {
    Serial.printf("[cfg] loaded name=%s pw=%s cfg_bytes=%u\n", lamp.name.c_str(),
                  lamp.password.empty() ? "unset" : "set", (unsigned)loaded.cfgBytes);
  }
#endif
  Serial.printf("[cfg] store %s\n", config_codec::storeFormName(loaded.form));

  if (loaded.parseFailed) {
    loadFailedWithData_ = true;
    Serial.printf(
        "[cfg] STORED CONFIG FAILED TO PARSE (%u bytes) — serving defaults "
        "WITHOUT overwriting NVS\n",
        (unsigned)loaded.cfgBytes);
    return;  // use class defaults
  }

  const config_codec::LoadedRecords& records = loaded.records;
  if (records.unreadable) {
    // A stored blob holding part of the config did not read. What loaded in
    // its place must not be saved over it.
    loadFailedWithData_ = true;
    Serial.println("[cfg] STORED CONFIG BLOB UNREADABLE; first-boot save held");
  }
  if (!loaded.withinCaps) Serial.println("[cfg] stored expressions over caps; truncated on load");
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    if (records.droppedMask & (1u << i)) {
      Serial.printf("[cfg] expression record x%u unreadable; dropped\n", (unsigned)i);
    }
  }
  if (records.duplicates) {
    Serial.printf("[cfg] %u repeated expression records skipped\n",
                  (unsigned)records.duplicates);
  }
  Serial.printf("[cfg] expressions=%u from %s\n",
                (unsigned)expressions.expressions.size(),
                records.present                                ? "records"
                : loaded.form == config_codec::StoreForm::Binary ? "no records"
                                                                 : "cfg json");
  namedKeyPresent_ = loaded.namedKeyPresent;

  // Per-peer dispositions live in a separate NVS key.
  dispositions_.load(uptimeMs());
};

static const char* saveFailure(config_codec::SaveResult result) {
  switch (result) {
    case config_codec::SaveResult::TooLarge: return "too large";
    case config_codec::SaveResult::Invalid: return "invalid";
    case config_codec::SaveResult::NoHeap: return "no heap";
    case config_codec::SaveResult::Held: return "held for web-save reboot";
    default: return "write";
  }
}

bool Config::persistConfig(const char* via) {
  if (!store_) return false;
  if (!saveGate_.allows()) {
    Serial.printf("[nvs] save via=%s held for web-save reboot\n", via);
    return false;
  }
  const config_codec::SaveResult result = config_codec::saveConfig(
      *store_, lamp, base, shade, expressions, homeMode, s_settingsBuf, sizeof(s_settingsBuf));
  if (result == config_codec::SaveResult::Ok) {
    saveGate_.saved();
    if (storeIsLegacy()) {
      Serial.printf("[cfg] store migrated from %s\n", config_codec::storeFormName(storeForm_));
      storeForm_ = config_codec::StoreForm::Binary;
    }
#ifdef LAMP_DEBUG
    Serial.printf("[nvs] config saved via=%s\n", via);
#endif
    return true;
  }
  saveGate_.failed(millis());
  Serial.printf("[nvs] save via=%s failed (%s), retry %lus\n", via, saveFailure(result),
                (unsigned long)(saveGate_.waitMs() / 1000));
  return false;
}

bool Config::persistDue(uint32_t nowMs) const { return saveGate_.due(nowMs); }

void Config::maybeRetryPersist(uint32_t nowMs) {
  if (saveGate_.pending() && saveGate_.due(nowMs)) persistConfig("retry");
}

config_codec::SaveResult Config::persistRawJson(const char* json) {
  if (!store_) return config_codec::SaveResult::WriteFailed;
  const config_codec::SaveResult result =
      saveGate_.allows()
          ? config_codec::saveRawJson(*store_, json, s_settingsBuf, sizeof(s_settingsBuf),
                                      lamp.password)
          : config_codec::SaveResult::Held;
  saveGate_.rawFinished(result, millis());
  if (result != config_codec::SaveResult::Ok) {
    Serial.printf("[nvs] web save failed (%s)\n", saveFailure(result));
  }
  return result;
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

void Config::setLampOtaState(uint8_t s, const uint8_t* targetMac) {
  const bool hasTarget = targetMac != nullptr;
  if (s == lampOtaState_ && hasTarget == lampHasOtaSendingTo_ &&
      (!hasTarget || std::memcmp(targetMac, lampOtaSendingTo_, 6) == 0)) {
    return;
  }
  lampOtaState_ = s;
  lampHasOtaSendingTo_ = hasTarget;
  if (hasTarget) std::memcpy(lampOtaSendingTo_, targetMac, 6);
  invalidateLampSection();
}

bool Config::isOtaDistributing() const {
  return lampOtaState_ == lamp_protocol::kOtaStateSending;
}

uint8_t Config::getDisposition(const uint8_t mac[6]) const {
  return dispositions_.peer(mac, uptimeMs()).disp;
}

uint8_t Config::getDisposition(const std::string& lampId) const {
  uint8_t mac[6];
  if (!parseBdAddr(lampId.c_str(), mac)) return DispositionStore::kDefault;
  return dispositions_.peer(mac, uptimeMs()).disp;
}

void Config::setDisposition(const std::string& lampId, uint8_t value) {
  uint8_t mac[6];
  if (!parseBdAddr(lampId.c_str(), mac)) return;
  dispositions_.set(mac, value, uptimeMs());
}

std::string Config::asDispositionsJson() const { return dispositions_.asJson(); }

bool Config::setDispositionsFromJson(const char* json, size_t len) {
  return dispositions_.setFromJson(json, len, uptimeMs());
}

void Config::maybeFlushDispositions(uint64_t nowMs, bool otaReceiving) {
  dispositions_.maybeFlush(nowMs, otaReceiving);
}

void Config::flushDispositionsNow(bool otaReceiving) {
  dispositions_.flushNow(uptimeMs(), otaReceiving);
}

JsonDocument Config::asJsonDocument() {
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), lamp, base, shade, expressions,
                       homeMode);
  return doc;
};

JsonDocument Config::asLampJson() {
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
  doc["apBootMinutes"] = lamp.apBootMinutes;
  doc["brightnessCeiling"] = lamp.brightnessCeiling;
  doc["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  // Firmware identity (packed semver + release channel string). Constant at
  // boot, so no extra invalidation hook is needed; the lamp section cache
  // picks these up the first time it's built.
  doc["fwVersion"] = FIRMWARE_VERSION;
  doc["fwChannel"] = FIRMWARE_CHANNEL_STR;
  // Content address for the flash expr catalog, so the app fetches a given
  // catalog at most once. Hash precomputed at build time over the exact bytes
  // served by the exprcat section.
  char catalogHash[9];
  std::snprintf(catalogHash, sizeof(catalogHash), "%08x", lamp::kExprCatalogHash);
  doc["catalogHash"] = catalogHash;
  // lampType is firmware-owned; the app can read it but settings_blob
  // writes do NOT update it (see parser block below).
  doc["lampType"] = lamp.lampType;
  if (!lampId_.empty()) {
    doc["lampId"] = lampId_;
  }
  if (lampOtaState_ != 0) doc["otaState"] = lampOtaState_;
  if (lampHasOtaSendingTo_) {
    char macBuf[18];
    formatBdAddr(lampOtaSendingTo_, macBuf);
    doc["otaSendingTo"] = macBuf;
  }
  doc["resetReason"] = static_cast<uint8_t>(esp_reset_reason());
  crash_record::emitLastCrash(doc, crash_record::last());
  return doc;
}

// Role sections advertise topology: a role-level `px` = Σ segment px (the
// app's scalar readers size off it) alongside the per-segment list. Both are
// emitted; the app reads whichever it needs.
static void emitRoleTopology(JsonDocument& doc,
                             const std::vector<SegmentSettings>& segs) {
  doc["px"] = segmentsSumPx(segs);
  JsonArray colorsNode = doc["colors"].to<JsonArray>();
  for (const auto& c : segs.front().colors) colorsNode.add(colorToHexString(c));
  config_codec::writeSectionSegments(doc.as<JsonObject>(), segs);
}

JsonDocument Config::asBaseJson() {
  JsonDocument doc;
  emitRoleTopology(doc, base.segments);
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
  config_codec::writeSectionRotate(doc.as<JsonObject>(), base);
  doc["drawIdleMa"] = drawIdleMa_;
  doc["drawFullMa"] = drawFullMa_;
  return doc;
}

JsonDocument Config::asShadeJson() {
  JsonDocument doc;
  emitRoleTopology(doc, shade.segments);
  doc["byteOrder"] = shade.byteOrder;
  // colorsEditable is firmware-owned (set by Lamp subclass via applyDefaults).
  // Emitted so the app can hide the color picker for surfaces the subclass
  // has opted out of (e.g. SnafuLamp shade).
  doc["colorsEditable"] = shade.colorsEditable;
  config_codec::writeSectionRotate(doc.as<JsonObject>(), shade);
  return doc;
}

JsonDocument Config::asHomeModeJson() {
  JsonDocument doc;
  doc["ssid"] = homeMode.ssid;
  doc["brightness"] = homeMode.brightness;
  doc["enabled"] = homeMode.enabled;
  doc["networkBound"] = homeMode.networkBound;
  doc["socialDisabled"] = homeMode.socialDisabled;
  JsonArray disArr = doc["disabledExpressionTypes"].to<JsonArray>();
  for (const auto& s : homeMode.disabledExpressionTypes) disArr.add(s);
  return doc;
}

// Per-section JSON caches. Invariant: a cache is touched only while this mutex
// is held, and a read rebuilds and copies out under one hold, so a rebuild or a
// release on the other core never lands between the two. invalidate*() runs
// without it.
static SemaphoreHandle_t cacheMutex() {
  static SemaphoreHandle_t m = xSemaphoreCreateMutex();
  return m;
}

// Holds the cache mutex for a scope, so a throw from a rebuild cannot leave it
// held and block every later section read.
class CacheLock {
 public:
  CacheLock() { xSemaphoreTake(cacheMutex(), portMAX_DELAY); }
  ~CacheLock() { xSemaphoreGive(cacheMutex()); }
  CacheLock(const CacheLock&) = delete;
  CacheLock& operator=(const CacheLock&) = delete;
};

#define LAMP_DEFINE_SECTION_CACHED(NAME, REBUILD)                                     \
  void Config::NAME##SectionRebuildIfDirty() {                                        \
    CacheLock lock;                                                                   \
    REBUILD;                                                                          \
  }                                                                                   \
  bool Config::NAME##SectionJsonCached(std::string& out) {                            \
    CacheLock lock;                                                                   \
    REBUILD;                                                                          \
    return NAME##Section_.copyOut(out, #NAME);                                        \
  }

// The largest section (about 4 KB at the caps) is written straight into a
// string measured first, so a rebuild takes one exactly-sized block and never
// a document's worth of small ones.
static void writeExpressionsSection(JsonWriter& w, const ExpressionSettings& s) {
  w.beginArray();
  for (const auto& expr : s.expressions) config_codec::writeExpressionJson(w, expr);
  w.endArray();
}

LAMP_DEFINE_SECTION_CACHED(lamp, lampSection_.rebuild("lamp", [this] { return asLampJson(); }))
LAMP_DEFINE_SECTION_CACHED(base, baseSection_.rebuild("base", [this] { return asBaseJson(); }))
LAMP_DEFINE_SECTION_CACHED(shade, shadeSection_.rebuild("shade", [this] { return asShadeJson(); }))
LAMP_DEFINE_SECTION_CACHED(home, homeSection_.rebuild("home", [this] { return asHomeModeJson(); }))
LAMP_DEFINE_SECTION_CACHED(expressions, expressionsSection_.rebuildExactly("expressions",
    [this](JsonWriter& w) { writeExpressionsSection(w, expressions); }))

void Config::releaseSectionCaches() {
  CacheLock lock;
  for (auto* section : {&lampSection_, &baseSection_, &shadeSection_,
                        &expressionsSection_, &homeSection_}) {
    section->release();
  }
}

#undef LAMP_DEFINE_SECTION_CACHED

void Config::invalidateLampSection() {
  lampSection_.invalidate();
}

void Config::invalidateBaseSection() {
  baseSection_.invalidate();
}

void Config::setDrawAnchors(uint16_t idleMa, uint16_t fullMa) {
  if (idleMa == drawIdleMa_ && fullMa == drawFullMa_) return;
  drawIdleMa_ = idleMa;
  drawFullMa_ = fullMa;
  invalidateBaseSection();
}

void Config::invalidateShadeSection() {
  shadeSection_.invalidate();
}

void Config::invalidateExpressionsSection() {
  expressionsSection_.invalidate();
}

void Config::invalidateHomeSection() {
  homeSection_.invalidate();
}

void Config::invalidateAllSections() {
  lampSection_.invalidate();
  baseSection_.invalidate();
  shadeSection_.invalidate();
  expressionsSection_.invalidate();
  homeSection_.invalidate();
}

void Config::applyDefaults(const Defaults& d) {
  // Called AFTER Config::Config(ConfigStore*) loads from NVS so that NVS
  // values are authoritative and only truly-factory-default fields are
  // overwritten. Subclass defaults() returns values that are used as
  // first-boot baselines.
  //
  // Name: resolve `named` (migrating pre-flag NVS from the stored name), then
  // substitute the variant default only for a lamp the user never named. A
  // named lamp keeps its stored name even when it equals a default word.
  lamp.named = resolveNamed(namedKeyPresent_, lamp.named, lamp.name, d.name);
  // A named lamp is already configured; never let the app re-adopt it (which
  // would reset its Web Config window) after an upgrade from firmware that
  // predates the setup flag.
  if (lamp.named) lamp.setup = true;
  if (!d.name.empty() && !lamp.named) {
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
  base.rotationSupported = d.rotationSupported;
  shade.rotationSupported = d.rotationSupported;
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
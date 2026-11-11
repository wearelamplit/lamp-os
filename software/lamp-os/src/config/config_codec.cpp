#include "config/config_codec.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

#include <lampos/led_types.hpp>

#include "core/hw_config.hpp"
#include "util/color.hpp"
#include "util/heap_guard.hpp"
#include "util/json_writer.hpp"
#include "util/str.hpp"

namespace lamp {
namespace config_codec {

// Replace a role's segment list from `node["segments"]`. Absent/empty array
// keeps the current (variant-default) list. Every segment is left with ≥1
// color so broadcastColors() is always dereferenceable.
// Fallback: if `segments` is absent/empty but the old flat `colors` array
// is present (blobs from before the per-segment schema), migrate to a single
// segment named `roleName` so custom colors survive the upgrade and the app
// doesn't render a blank segment label.
static void parseSegments(JsonObject node, std::vector<SegmentSettings>& segs,
                          const Color& fallback, const std::string& roleName) {
  JsonArray arr = node["segments"];
  if (arr.isNull() || arr.size() == 0) {
    JsonArray flat = node["colors"];
    if (flat.isNull() || flat.size() == 0) return;
    SegmentSettings seg;
    seg.name = roleName;
    seg.px = node["px"] | 0;
    for (JsonVariant c : flat) {
      if (seg.colors.size() == kMaxSurfaceColors) break;
      seg.colors.push_back(hexStringToColor(c));
    }
    if (seg.colors.empty()) seg.colors.push_back(fallback);
    segs.clear();
    segs.push_back(std::move(seg));
    return;
  }
  segs.clear();
  for (JsonObject segNode : arr) {
    if (segs.size() == kMaxSegments) break;
    SegmentSettings seg;
    seg.name = std::string(segNode["name"] | "");
    truncateUtf8(seg.name, kMaxSegmentNameLen);
    seg.px = segNode["px"] | 0;
    const int pin = segNode["pin"] | 0;
    if (isStripDataPin(pin)) {
      seg.pin = static_cast<uint8_t>(pin);
    }
    for (JsonVariant c : segNode["colors"].as<JsonArray>()) {
      if (seg.colors.size() == kMaxSurfaceColors) break;
      seg.colors.push_back(hexStringToColor(c));
    }
    if (seg.colors.empty()) seg.colors.push_back(fallback);
    segs.push_back(std::move(seg));
  }
  if (segs.empty()) segs.push_back({roleName, 0, {fallback}});
}

// Σ segment px ≤ 255 (uint8 pixelCount + boot buffer sizing). This is the one
// boundary every NVS load crosses (settings-blob apply AND webapp raw-persist
// both re-parse here on boot), so clamp the running sum: a garbage oversized
// blob can't blow the boot buffer.
static void clampSumPx(std::vector<SegmentSettings>& segs) {
  unsigned budget = 255;
  for (auto& s : segs) {
    if (s.px > budget) s.px = static_cast<uint8_t>(budget);
    budget -= s.px;
  }
}

static void writeSegmentsArray(JsonObject node, const std::vector<SegmentSettings>& segs,
                               bool everyPin) {
  JsonArray arr = node["segments"].to<JsonArray>();
  for (const auto& s : segs) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = s.name;
    o["px"] = s.px;
    if (everyPin || s.pin) o["pin"] = s.pin;
    if (everyPin) o["builtinPin"] = s.builtinPin;
    JsonArray colors = o["colors"].to<JsonArray>();
    for (const auto& c : s.colors) colors.add(colorToHexString(c));
  }
}

static void writeSegments(JsonObject node, const std::vector<SegmentSettings>& segs) {
  writeSegmentsArray(node, segs, false);
}

void writeSectionSegments(JsonObject section, const std::vector<SegmentSettings>& segs) {
  writeSegmentsArray(section, segs, true);
}

static const char* rotationDirName(RotationDir dir) {
  return dir == RotationDir::Down ? "down" : "up";
}

// A role stores `rotate` only with one segment and a rotation off its default,
// so a lamp that never touches rotation writes what it always has.
template <class Role>
static bool storesRotation(const Role& role) {
  return role.rotatable() && role.rotation != Role::kDefaultRotation;
}

static void writeRotate(JsonObject node, const ColorRotation& rotation) {
  JsonObject o = node["rotate"].to<JsonObject>();
  o["on"] = rotation.on;
  o["periodS"] = rotation.periodS;
  o["dir"] = rotationDirName(rotation.dir);
}

static void writeRotateJson(JsonWriter& w, const ColorRotation& rotation) {
  w.key("rotate");
  w.beginObject();
  w.key("on");
  w.value(rotation.on);
  w.key("periodS");
  w.value(static_cast<uint32_t>(rotation.periodS));
  w.key("dir");
  w.value(rotationDirName(rotation.dir));
  w.endObject();
}

void parseRotation(JsonObjectConst node, ColorRotation& out) {
  JsonVariantConst on = node["on"];
  if (on.is<bool>()) out.on = on.as<bool>();
  JsonVariantConst period = node["periodS"];
  if (period.is<int32_t>()) out.periodS = clampRotationPeriodS(period.as<int32_t>());
  const char* dir = node["dir"].is<const char*>() ? node["dir"].as<const char*>() : "";
  if (!strcmp(dir, "up")) {
    out.dir = RotationDir::Up;
  } else if (!strcmp(dir, "down")) {
    out.dir = RotationDir::Down;
  }
}

template <class Role>
static void writeRoleSectionRotate(JsonObject section, const Role& role) {
  if (role.offersRotation()) writeRotate(section, role.rotation);
}

void writeSectionRotate(JsonObject section, const ShadeSettings& shade) {
  writeRoleSectionRotate(section, shade);
}

void writeSectionRotate(JsonObject section, const BaseSettings& base) {
  writeRoleSectionRotate(section, base);
}

const ExpressionParams* findExpressionParams(const ExpressionSettings& settings,
                                             const std::string& type, uint8_t target) {
  for (const auto& e : settings.expressions) {
    if (e.target == target && e.type == type) return &e.parameters;
  }
  return nullptr;
}

bool mirrorUpsert(ExpressionSettings& settings, const ExpressionConfig& cfg) {
  auto& exprs = settings.expressions;
  for (auto& e : exprs) {
    if (e.type == cfg.type && e.target == cfg.target) {
      e = cfg;
      return true;
    }
  }
  if (exprs.size() >= kMaxExpressions) return false;
  if (exprs.size() == exprs.capacity()) {
    exprs.reserve(std::min(kMaxExpressions, exprs.capacity() + 4));
  }
  exprs.push_back(cfg);
  return true;
}

std::string storableByteOrder(const char* s) {
  lampos::led::ByteOrder order;
  return s && lampos::led::byteOrderFromString(s, order) ? std::string(s) : std::string();
}

bool parseExpression(JsonObject node, ExpressionConfig& out, bool enabledDefault) {
  bool withinCaps = true;
  out.type = std::string(node["type"] | "");
  if (out.type.size() > kMaxExpressionTypeLen) {
    out.type.resize(kMaxExpressionTypeLen);
    withinCaps = false;
  }
  out.enabled = node["enabled"] | enabledDefault;
  out.intervalMin = node["intervalMin"] | 60;
  out.intervalMax = node["intervalMax"] | 900;
  out.target = node["target"] | 3;
  // disabledDuringWispOverride is a pure type-property, not config-driven.
  // Payloads carrying the key are tolerated; the skip-list drops it.
  for (JsonPair kv : node) {
    const char* key = kv.key().c_str();
    if (!strcmp(key, "type") || !strcmp(key, "enabled") ||
        !strcmp(key, "intervalMin") || !strcmp(key, "intervalMax") ||
        !strcmp(key, "target") || !strcmp(key, "colors") ||
        !strcmp(key, "disabledDuringWispOverride")) {
      continue;
    }
    JsonVariant value = kv.value();
    if (!value.is<uint32_t>() && !value.is<int>()) continue;
    if (expressionParamKeyIndex(key) < 0) continue;
    if (out.parameters.size() >= kMaxExpressionParams) {
      withinCaps = false;
      continue;
    }
    out.setParameter(key, value.is<uint32_t>()
                              ? value.as<uint32_t>()
                              : static_cast<uint32_t>(value.as<int>()));
  }
  JsonArray colors = node["colors"];
  const size_t keep = std::min(colors.size(), kMaxExpressionColors);
  if (colors.size() > keep) withinCaps = false;
  out.colors.reserve(keep);
  for (JsonVariant color : colors) {
    if (out.colors.size() == keep) break;
    out.colors.push_back(hexStringToColor(color));
  }
  return withinCaps;
}

bool fromJson(JsonObject root, LampSettings& lamp, BaseSettings& base,
              ShadeSettings& shade, ExpressionSettings& expressions,
              HomeModeSettings& homeMode) {
  bool withinCaps = true;
  JsonObject lampNode = root["lamp"];
  // lampType is firmware-owned (see Config::setLampType).
  // Inbound settings_blob writes intentionally do not update it; the app
  // can read but not write the variant identity.
  lamp.name =
      normalizeLampName(std::string(lampNode["name"] | kDefaultLampName));
  lamp.brightness = lampNode["brightness"] | 100;
  std::string password = std::string(lampNode["password"] | "");
  if (!password.empty()) {
    // Every write path refuses one past kMaxPasswordLen, so only a store
    // written before the cap holds a longer one; holding the BOOT button at
    // power-up clears a password the owner can no longer type.
    truncateUtf8(password, kMaxPasswordLen);
    lamp.password = password;
  }
  lamp.setup = lampNode["setup"] | false;
  lamp.named = lampNode["named"] | false;
  lamp.advancedEnabled = lampNode["advancedEnabled"] | false;
  lamp.webappEnabled = lampNode["webappEnabled"] | true;
  lamp.apBootMinutes = lampNode["apBootMinutes"] | 0;
  lamp.brightnessCeiling = lampNode["brightnessCeiling"] | 170;
  // SocialMode persists as uint8_t (0=Introvert, 1=Ambivert, 2=Extrovert).
  // Out-of-range values fall back to Ambivert so a corrupt or future-versioned
  // payload doesn't strand the lamp in an unknown personality.
  {
    uint32_t modeRaw = lampNode["socialMode"] | 1;
    if (modeRaw > 2) modeRaw = 1;
    lamp.socialMode = static_cast<SocialMode>(modeRaw);
  }

  JsonObject baseNode = root["base"];
  base.byteOrder = storableByteOrder(baseNode["byteOrder"] | "");
  parseSegments(baseNode, base.segments, kBaseDefaultColor, "Base");
  clampSumPx(base.segments);
  base.rotation = BaseSettings::kDefaultRotation;
  if (base.rotatable()) parseRotation(baseNode["rotate"], base.rotation);
  // Keep knockoutPixels in sync with the active pixel count. Drops stale
  // entries when px shrinks and 100-fills ("no knockout") any slots when px
  // grows. The input loop below then overwrites slots 0..sumPx-1 from JSON.
  base.knockoutPixels.resize(base.sumPx(), 100);
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

  JsonObject shadeNode = root["shade"];
  shade.byteOrder = storableByteOrder(shadeNode["byteOrder"] | "");
  parseSegments(shadeNode, shade.segments, kShadeDefaultColor, "Shade");
  clampSumPx(shade.segments);
  shade.rotation = ShadeSettings::kDefaultRotation;
  if (shade.rotatable()) parseRotation(shadeNode["rotate"], shade.rotation);
  // colorsEditable is firmware-owned (set by Lamp subclass defaults via
  // applyDefaults). Inbound writes intentionally do not update it.

  // Load expressions
  JsonArray expressionsNode = root["expressions"];
  if (expressionsNode) {
    expressions.expressions.clear();
    expressions.expressions.reserve(
        std::min(expressionsNode.size(), kMaxExpressions));
    for (JsonObject exprNode : expressionsNode) {
      if (expressions.expressions.size() == kMaxExpressions) {
        withinCaps = false;
        break;
      }
      ExpressionConfig expr;
      if (!parseExpression(exprNode, expr, false)) withinCaps = false;
      // One entry per (type, target), the first kept, as the records load.
      if (findExpressionParams(expressions, expr.type, expr.target)) continue;
      expressions.expressions.push_back(std::move(expr));
    }
  }

  // Load home mode. A legacy "password" field in NVS is silently ignored
  // (presence-only home mode stores no password).
  JsonObject homeModeNode = root["homeMode"];
  if (homeModeNode) {
    homeMode.ssid = std::string(homeModeNode["ssid"] | "");
    truncateUtf8(homeMode.ssid, kMaxSsidLen);
    homeMode.brightness = homeModeNode["brightness"] | 60;
    // Migration: lamps configured before `enabled` existed have no
    // "enabled" key in their NVS-stored JSON. Treat "has SSID" as proxy
    // for "user wanted home mode on" so they keep working post-update.
    homeMode.enabled = homeModeNode["enabled"] | !homeMode.ssid.empty();
    homeMode.networkBound = homeModeNode["networkBound"] | !homeMode.ssid.empty();
    homeMode.socialDisabled = homeModeNode["socialDisabled"] | true;
    if (homeModeNode["disabledExpressionTypes"].is<JsonArray>()) {
      homeMode.disabledExpressionTypes.clear();
      for (JsonVariant v : homeModeNode["disabledExpressionTypes"].as<JsonArray>()) {
        if (homeMode.disabledExpressionTypes.size() == kMaxDisabledExpressionTypes) break;
        const char* s = v.as<const char*>();
        if (isExpressionTypeId(s)) homeMode.disabledExpressionTypes.push_back(s);
      }
    } else {
      homeMode.disabledExpressionTypes = {"glitchy"};
    }
  }
  return withinCaps;
}

void toJson(JsonObject root, const LampSettings& lamp, const BaseSettings& base,
            const ShadeSettings& shade, const ExpressionSettings& expressions,
            const HomeModeSettings& homeMode) {
  toStoredJson(root, lamp, base, shade, homeMode);
  // Positional uint8 array, same shape as asBaseJson.
  JsonArray knockoutNode = root["base"]["knockout"].to<JsonArray>();
  for (uint8_t level : base.knockoutPixels) knockoutNode.add(level);
  JsonArray expressionsNode = root["expressions"].to<JsonArray>();
  for (const auto& expr : expressions.expressions) {
    expressionToJson(expressionsNode.add<JsonObject>(), expr);
  }
}

void writeExpressionJson(JsonWriter& w, const ExpressionConfig& expr) {
  w.beginObject();
  w.key("type");
  w.value(expr.type.c_str());
  w.key("enabled");
  w.value(expr.enabled);
  w.key("intervalMin");
  w.value(expr.intervalMin);
  w.key("intervalMax");
  w.value(expr.intervalMax);
  w.key("target");
  w.value(static_cast<uint32_t>(expr.target));
  for (const auto& param : expr.parameters) {
    w.key(param.first);
    w.value(param.second);
  }
  w.key("colors");
  w.beginArray();
  for (const auto& color : expr.colors) w.value(colorToHexString(color).c_str());
  w.endArray();
  w.endObject();
}

static void writeSegmentsJson(JsonWriter& w, const std::vector<SegmentSettings>& segs) {
  w.key("segments");
  w.beginArray();
  for (const auto& seg : segs) {
    w.beginObject();
    w.key("name");
    w.value(seg.name.c_str());
    w.key("px");
    w.value(static_cast<uint32_t>(seg.px));
    if (seg.pin) {
      w.key("pin");
      w.value(static_cast<uint32_t>(seg.pin));
    }
    w.key("colors");
    w.beginArray();
    for (const auto& c : seg.colors) w.value(colorToHexString(c).c_str());
    w.endArray();
    w.endObject();
  }
  w.endArray();
}

static void writeLampJson(JsonWriter& w, const LampSettings& lamp) {
  w.key("lamp");
  w.beginObject();
  w.key("name");
  w.value(lamp.name.c_str());
  w.key("brightness");
  w.value(static_cast<uint32_t>(lamp.brightness));
  if (!lamp.password.empty()) {
    w.key("password");
    w.value(lamp.password.c_str());
  }
  w.key("setup");
  w.value(lamp.setup);
  w.key("named");
  w.value(lamp.named);
  w.key("advancedEnabled");
  w.value(lamp.advancedEnabled);
  w.key("webappEnabled");
  w.value(lamp.webappEnabled);
  w.key("apBootMinutes");
  w.value(static_cast<uint32_t>(lamp.apBootMinutes));
  w.key("brightnessCeiling");
  w.value(static_cast<uint32_t>(lamp.brightnessCeiling));
  w.key("socialMode");
  w.value(static_cast<uint32_t>(lamp.socialMode));
  w.endObject();
}

void writeLampOnlyJson(JsonWriter& w, const LampSettings& lamp) {
  w.beginObject();
  writeLampJson(w, lamp);
  w.endObject();
}

void writeStoredJson(JsonWriter& w, const LampSettings* lamp, const BaseSettings& base,
                     const ShadeSettings& shade, const HomeModeSettings& homeMode,
                     const ExpressionSettings* whole) {
  w.beginObject();
  if (lamp) writeLampJson(w, *lamp);
  w.key("base");
  w.beginObject();
  if (!base.byteOrder.empty()) {
    w.key("byteOrder");
    w.value(base.byteOrder.c_str());
  }
  w.key("colorsEditable");
  w.value(base.colorsEditable);
  writeSegmentsJson(w, base.segments);
  if (storesRotation(base)) writeRotateJson(w, base.rotation);
  if (whole) {
    w.key("knockout");
    w.beginArray();
    for (uint8_t level : base.knockoutPixels) w.value(static_cast<uint32_t>(level));
    w.endArray();
  }
  w.endObject();
  w.key("shade");
  w.beginObject();
  if (!shade.byteOrder.empty()) {
    w.key("byteOrder");
    w.value(shade.byteOrder.c_str());
  }
  w.key("colorsEditable");
  w.value(shade.colorsEditable);
  writeSegmentsJson(w, shade.segments);
  if (storesRotation(shade)) writeRotateJson(w, shade.rotation);
  w.endObject();
  w.key("homeMode");
  w.beginObject();
  w.key("ssid");
  w.value(homeMode.ssid.c_str());
  w.key("brightness");
  w.value(static_cast<uint32_t>(homeMode.brightness));
  w.key("enabled");
  w.value(homeMode.enabled);
  w.key("networkBound");
  w.value(homeMode.networkBound);
  w.key("socialDisabled");
  w.value(homeMode.socialDisabled);
  w.key("disabledExpressionTypes");
  w.beginArray();
  for (const auto& t : homeMode.disabledExpressionTypes) w.value(t.c_str());
  w.endArray();
  w.endObject();
  if (whole) {
    w.key("expressions");
    w.beginArray();
    for (const auto& expr : whole->expressions) writeExpressionJson(w, expr);
    w.endArray();
  }
  w.endObject();
}

void expressionToJson(JsonObject node, const ExpressionConfig& expr) {
  node["type"] = expr.type;
  node["enabled"] = expr.enabled;
  node["intervalMin"] = expr.intervalMin;
  node["intervalMax"] = expr.intervalMax;
  node["target"] = expr.target;
  for (const auto& param : expr.parameters) node[param.first] = param.second;
  JsonArray colorsNode = node["colors"].to<JsonArray>();
  for (const auto& color : expr.colors) colorsNode.add(colorToHexString(color));
}

void toStoredJson(JsonObject root, const LampSettings& lamp,
                  const BaseSettings& base, const ShadeSettings& shade,
                  const HomeModeSettings& homeMode) {
  JsonObject lampNode = root["lamp"].to<JsonObject>();
  lampNode["name"] = lamp.name;
  lampNode["brightness"] = lamp.brightness;
  if (!lamp.password.empty()) {
    lampNode["password"] = lamp.password;
  }
  lampNode["setup"] = lamp.setup;
  lampNode["named"] = lamp.named;
  lampNode["advancedEnabled"] = lamp.advancedEnabled;
  lampNode["webappEnabled"] = lamp.webappEnabled;
  lampNode["apBootMinutes"] = lamp.apBootMinutes;
  lampNode["brightnessCeiling"] = lamp.brightnessCeiling;
  lampNode["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  JsonObject baseNode = root["base"].to<JsonObject>();
  if (!base.byteOrder.empty()) baseNode["byteOrder"] = base.byteOrder;
  baseNode["colorsEditable"] = base.colorsEditable;
  writeSegments(baseNode, base.segments);
  if (storesRotation(base)) writeRotate(baseNode, base.rotation);

  JsonObject shadeNode = root["shade"].to<JsonObject>();
  if (!shade.byteOrder.empty()) shadeNode["byteOrder"] = shade.byteOrder;
  shadeNode["colorsEditable"] = shade.colorsEditable;
  writeSegments(shadeNode, shade.segments);
  if (storesRotation(shade)) writeRotate(shadeNode, shade.rotation);

  JsonObject homeModeNode = root["homeMode"].to<JsonObject>();
  homeModeNode["ssid"] = homeMode.ssid;
  homeModeNode["brightness"] = homeMode.brightness;
  homeModeNode["enabled"] = homeMode.enabled;
  homeModeNode["networkBound"] = homeMode.networkBound;
  homeModeNode["socialDisabled"] = homeMode.socialDisabled;
  JsonArray disArr = homeModeNode["disabledExpressionTypes"].to<JsonArray>();
  for (const auto& s : homeMode.disabledExpressionTypes) disArr.add(s);
}

namespace {

constexpr uint8_t kRecordFormat = 1;
constexpr uint8_t kRecordEnabled = 0x01;
constexpr const char* kCountKey = "xn";
constexpr const char* kKnockoutKey = "ko";
// The rest (or a staged whole config) as a JSON blob, in two keys by turns so
// a rewrite never touches the live copy. With both present (a save cut short
// between writing one and erasing the other) the first wins: either is a
// whole copy, and saves and loads pick the same one.
constexpr const char* kRestKeys[2] = {"cfgb", "cfgc"};
constexpr const char* kLampCfgKey = "cfgl";
// Record slots every save sweeps ("x0".."x31"), past kMaxExpressions, so a
// record no count names is erased wherever it sits.
constexpr size_t kRecordKeySlots = 32;

void putU32(uint8_t*& p, uint32_t v) {
  for (int i = 0; i < 4; ++i) *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint32_t getU32(const uint8_t*& p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(*p++) << (8 * i);
  return v;
}

void recordKey(size_t i, char (&key)[5]) {
  std::snprintf(key, sizeof(key), "x%u", static_cast<unsigned>(i));
}

}  // namespace

size_t encodeExpression(const ExpressionConfig& expr, uint8_t* out) {
  uint8_t* p = out;
  *p++ = kRecordFormat;
  *p++ = expr.enabled ? kRecordEnabled : 0;
  *p++ = expr.target;
  const size_t typeLen = std::min(expr.type.size(), kMaxExpressionTypeLen);
  *p++ = static_cast<uint8_t>(typeLen);
  std::memcpy(p, expr.type.data(), typeLen);
  p += typeLen;
  putU32(p, expr.intervalMin);
  putU32(p, expr.intervalMax);
  const size_t colors = std::min(expr.colors.size(), kMaxExpressionColors);
  *p++ = static_cast<uint8_t>(colors);
  for (size_t i = 0; i < colors; ++i) {
    const Color& c = expr.colors[i];
    *p++ = c.r;
    *p++ = c.g;
    *p++ = c.b;
    *p++ = c.w;
  }
  uint8_t* paramCount = p++;
  *paramCount = 0;
  for (const auto& kv : expr.parameters) {
    if (*paramCount == kMaxExpressionParams) break;
    const int idx = expressionParamKeyIndex(kv.first);
    if (idx < 0) continue;
    *p++ = static_cast<uint8_t>(idx);
    putU32(p, kv.second);
    ++*paramCount;
  }
  return static_cast<size_t>(p - out);
}

bool decodeExpression(const uint8_t* data, size_t len, ExpressionConfig& out) {
  const uint8_t* p = data;
  const uint8_t* end = data + len;
  auto have = [&](size_t n) { return static_cast<size_t>(end - p) >= n; };
  if (!have(4) || p[0] != kRecordFormat) return false;
  out.enabled = (p[1] & kRecordEnabled) != 0;
  out.target = p[2];
  const size_t typeLen = p[3];
  p += 4;
  if (typeLen > kMaxExpressionTypeLen || !have(typeLen + 8 + 1)) return false;
  out.type.assign(reinterpret_cast<const char*>(p), typeLen);
  p += typeLen;
  out.intervalMin = getU32(p);
  out.intervalMax = getU32(p);
  const size_t colors = *p++;
  if (colors > kMaxExpressionColors || !have(4 * colors + 1)) return false;
  out.colors.clear();
  out.colors.reserve(colors);
  for (size_t i = 0; i < colors; ++i, p += 4) {
    out.colors.push_back(Color(p[0], p[1], p[2], p[3]));
  }
  const size_t params = *p++;
  if (params > kMaxExpressionParams || !have(5 * params)) return false;
  out.parameters.clear();
  for (size_t i = 0; i < params; ++i) {
    const uint8_t idx = *p++;
    const uint32_t value = getU32(p);
    // An index past this build's table came from a newer one; skip it.
    if (idx < kExpressionParamKeyCount) {
      out.parameters.setIndex(idx, value);
    }
  }
  return true;
}

namespace {

constexpr uint8_t kSettingsFormat = 1;
constexpr uint8_t kLampSetup = 0x01;
constexpr uint8_t kLampNamed = 0x02;
constexpr uint8_t kLampAdvanced = 0x04;
constexpr uint8_t kLampWebapp = 0x08;
constexpr uint8_t kRoleRotation = 0x01;  // the role stores a rotation
constexpr uint8_t kRoleRotationOn = 0x02;
constexpr uint8_t kRoleRotationDown = 0x04;
constexpr uint8_t kHomeEnabled = 0x01;
constexpr uint8_t kHomeNetworkBound = 0x02;
constexpr uint8_t kHomeSocialDisabled = 0x04;
// A byte order is stored as 1 + its ByteOrder value, 0 for the variant's.
static_assert(static_cast<uint8_t>(lampos::led::ByteOrder::GRBW) == 0 &&
                  static_cast<uint8_t>(lampos::led::ByteOrder::GRB) == 1 &&
                  static_cast<uint8_t>(lampos::led::ByteOrder::BGR) == 2,
              "stored byte order codes");
constexpr uint8_t kByteOrderCodes = 4;

// Writes bytes to a buffer, or compares them with a stored blob.
class SettingsSink {
 public:
  explicit SettingsSink(uint8_t* out) : out_(out) {}
  SettingsSink(const uint8_t* expect, size_t len) : expect_(expect), expectLen_(len) {}
  void put(uint8_t b) {
    if (out_) {
      out_[n_] = b;
    } else {
      match_ = match_ && n_ < expectLen_ && expect_[n_] == b;
    }
    ++n_;
  }
  void bytes(const char* p, size_t n) {
    for (size_t i = 0; i < n; ++i) put(static_cast<uint8_t>(p[i]));
  }
  void str(const char* p, size_t n) {
    put(static_cast<uint8_t>(n));
    bytes(p, n);
  }
  void prefix(const std::string& s, size_t max) {
    str(s.data(), utf8PrefixLen(s.data(), s.size(), max));
  }
  size_t length() const { return n_; }
  bool matched() const { return match_ && n_ == expectLen_; }

 private:
  uint8_t* out_ = nullptr;
  const uint8_t* expect_ = nullptr;
  size_t expectLen_ = 0;
  size_t n_ = 0;
  bool match_ = true;
};

// The bytes normalizeLampName keeps of `name`, before it lowercases them,
// into `out`, taking no heap. The decode lowercases.
size_t spacelessName(const std::string& name, char (&out)[kMaxLampNameLen + 1]) {
  size_t n = 0;
  for (const char c : name) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    out[n++] = c;
    if (n == sizeof(out)) break;
  }
  return utf8PrefixLen(out, n, kMaxLampNameLen);
}

uint8_t byteOrderCode(const std::string& order) {
  lampos::led::ByteOrder o;
  return lampos::led::byteOrderFromString(order.c_str(), o) ? 1 + static_cast<uint8_t>(o) : 0;
}

template <class Role>
void writeRole(SettingsSink& s, const Role& role) {
  const bool rotation = storesRotation(role);
  s.put((rotation ? kRoleRotation : 0) | (rotation && role.rotation.on ? kRoleRotationOn : 0) |
        (rotation && role.rotation.dir == RotationDir::Down ? kRoleRotationDown : 0));
  s.put(byteOrderCode(role.byteOrder));
  s.put(rotation ? role.rotation.periodS : 0);
  const size_t segments = std::min(role.segments.size(), kMaxSegments);
  s.put(static_cast<uint8_t>(segments));
  for (size_t i = 0; i < segments; ++i) {
    const SegmentSettings& seg = role.segments[i];
    s.prefix(seg.name, kMaxSegmentNameLen);
    s.put(seg.px);
    s.put(seg.pin);
    const size_t colors = std::min(seg.colors.size(), kMaxSurfaceColors);
    s.put(static_cast<uint8_t>(colors));
    for (size_t c = 0; c < colors; ++c) {
      s.put(seg.colors[c].r);
      s.put(seg.colors[c].g);
      s.put(seg.colors[c].b);
      s.put(seg.colors[c].w);
    }
  }
}

void writeSettings(SettingsSink& s, const LampSettings& lamp, const BaseSettings& base,
                   const ShadeSettings& shade, const HomeModeSettings& homeMode) {
  s.put(kSettingsFormat);
  s.put((lamp.setup ? kLampSetup : 0) | (lamp.named ? kLampNamed : 0) |
        (lamp.advancedEnabled ? kLampAdvanced : 0) | (lamp.webappEnabled ? kLampWebapp : 0));
  s.put(lamp.brightness);
  s.put(lamp.apBootMinutes);
  s.put(lamp.brightnessCeiling);
  s.put(static_cast<uint8_t>(lamp.socialMode));
  char name[kMaxLampNameLen + 1];
  s.str(name, spacelessName(lamp.name, name));
  s.prefix(lamp.password, kMaxPasswordLen);
  writeRole(s, base);
  writeRole(s, shade);
  const size_t knockout = std::min(base.knockoutPixels.size(), kMaxKnockoutLevels);
  s.put(static_cast<uint8_t>(knockout));
  for (size_t i = 0; i < knockout; ++i) s.put(base.knockoutPixels[i]);
  s.put((homeMode.enabled ? kHomeEnabled : 0) | (homeMode.networkBound ? kHomeNetworkBound : 0) |
        (homeMode.socialDisabled ? kHomeSocialDisabled : 0));
  s.put(homeMode.brightness);
  s.prefix(homeMode.ssid, kMaxSsidLen);
  // The first type ids that are valid, as fromJson keeps them.
  size_t types = 0;
  for (const auto& t : homeMode.disabledExpressionTypes) {
    if (types < kMaxDisabledExpressionTypes && isExpressionTypeId(t.c_str())) ++types;
  }
  s.put(static_cast<uint8_t>(types));
  for (const auto& t : homeMode.disabledExpressionTypes) {
    if (types == 0) break;
    if (!isExpressionTypeId(t.c_str())) continue;
    s.str(t.data(), t.size());
    --types;
  }
}

class SettingsReader {
 public:
  SettingsReader(const uint8_t* data, size_t len) : p_(data), end_(data + len) {}
  bool u8(uint8_t& out) {
    if (p_ == end_) return false;
    out = *p_++;
    return true;
  }
  // A length byte, then that many bytes; false past `max` or the end.
  bool str(size_t max, std::string& out) {
    uint8_t n;
    if (!u8(n) || n > max || static_cast<size_t>(end_ - p_) < n) return false;
    out.assign(reinterpret_cast<const char*>(p_), n);
    p_ += n;
    return true;
  }
  bool atEnd() const { return p_ == end_; }

 private:
  const uint8_t* p_;
  const uint8_t* end_;
};

template <class Role>
bool readRole(SettingsReader& r, Role& role, const Color& fallback) {
  uint8_t flags, order, period, segments;
  if (!r.u8(flags) || (flags & ~(kRoleRotation | kRoleRotationOn | kRoleRotationDown)) ||
      !r.u8(order) || order >= kByteOrderCodes || !r.u8(period) || !r.u8(segments) ||
      segments > kMaxSegments) {
    return false;
  }
  std::vector<SegmentSettings> segs(segments);
  for (SegmentSettings& seg : segs) {
    uint8_t pin, colors;
    if (!r.str(kMaxSegmentNameLen, seg.name) || !r.u8(seg.px) || !r.u8(pin) || !r.u8(colors) ||
        colors > kMaxSurfaceColors) {
      return false;
    }
    seg.pin = isStripDataPin(pin) ? pin : 0;
    seg.colors.resize(colors);
    for (Color& c : seg.colors) {
      if (!r.u8(c.r) || !r.u8(c.g) || !r.u8(c.b) || !r.u8(c.w)) return false;
    }
    if (seg.colors.empty()) seg.colors.push_back(fallback);
  }
  if (!segs.empty()) role.segments = std::move(segs);
  clampSumPx(role.segments);
  role.byteOrder =
      order ? lampos::led::byteOrderToString(static_cast<lampos::led::ByteOrder>(order - 1)) : "";
  role.rotation = Role::kDefaultRotation;
  if ((flags & kRoleRotation) && role.rotatable()) {
    role.rotation = {(flags & kRoleRotationOn) != 0, clampRotationPeriodS(period),
                     (flags & kRoleRotationDown) ? RotationDir::Down : RotationDir::Up};
  }
  return true;
}

bool readSettings(SettingsReader& r, LampSettings& lamp, BaseSettings& base, ShadeSettings& shade,
                  HomeModeSettings& homeMode) {
  uint8_t format, flags, mode;
  std::string password;
  if (!r.u8(format) || format != kSettingsFormat || !r.u8(flags) ||
      (flags & ~(kLampSetup | kLampNamed | kLampAdvanced | kLampWebapp)) ||
      !r.u8(lamp.brightness) || !r.u8(lamp.apBootMinutes) || !r.u8(lamp.brightnessCeiling) ||
      !r.u8(mode) || !r.str(kMaxLampNameLen, lamp.name) || !r.str(kMaxPasswordLen, password)) {
    return false;
  }
  lamp.setup = flags & kLampSetup;
  lamp.named = flags & kLampNamed;
  lamp.advancedEnabled = flags & kLampAdvanced;
  lamp.webappEnabled = flags & kLampWebapp;
  lamp.socialMode = static_cast<SocialMode>(mode > 2 ? 1 : mode);
  lamp.name = normalizeLampName(std::move(lamp.name));
  if (!password.empty()) lamp.password = std::move(password);
  if (!readRole(r, base, kBaseDefaultColor) || !readRole(r, shade, kShadeDefaultColor)) {
    return false;
  }
  uint8_t knockout;
  if (!r.u8(knockout)) return false;
  base.knockoutPixels.resize(base.sumPx(), 100);
  for (size_t i = 0; i < knockout; ++i) {
    uint8_t level;
    if (!r.u8(level)) return false;
    if (i < base.knockoutPixels.size()) base.knockoutPixels[i] = level;
  }
  uint8_t types;
  if (!r.u8(flags) || (flags & ~(kHomeEnabled | kHomeNetworkBound | kHomeSocialDisabled)) ||
      !r.u8(homeMode.brightness) || !r.str(kMaxSsidLen, homeMode.ssid) || !r.u8(types) ||
      types > kMaxDisabledExpressionTypes) {
    return false;
  }
  homeMode.enabled = flags & kHomeEnabled;
  homeMode.networkBound = flags & kHomeNetworkBound;
  homeMode.socialDisabled = flags & kHomeSocialDisabled;
  homeMode.disabledExpressionTypes.clear();
  for (uint8_t i = 0; i < types; ++i) {
    std::string type;
    if (!r.str(kMaxExpressionTypeLen, type)) return false;
    if (isExpressionTypeId(type.c_str())) homeMode.disabledExpressionTypes.push_back(type);
  }
  return r.atEnd();
}

}  // namespace

size_t encodeSettings(const LampSettings& lamp, const BaseSettings& base,
                      const ShadeSettings& shade, const HomeModeSettings& homeMode,
                      uint8_t* out) {
  SettingsSink s(out);
  writeSettings(s, lamp, base, shade, homeMode);
  return s.length();
}

bool settingsMatch(const uint8_t* data, size_t len, const LampSettings& lamp,
                   const BaseSettings& base, const ShadeSettings& shade,
                   const HomeModeSettings& homeMode) {
  SettingsSink s(data, len);
  writeSettings(s, lamp, base, shade, homeMode);
  return s.matched();
}

bool decodeSettings(const uint8_t* data, size_t len, LampSettings& lamp, BaseSettings& base,
                    ShadeSettings& shade, HomeModeSettings& homeMode) {
  LampSettings l = lamp;
  BaseSettings b = base;
  ShadeSettings s = shade;
  HomeModeSettings h = homeMode;
  SettingsReader r(data, len);
  if (!readSettings(r, l, b, s, h)) return false;
  lamp = std::move(l);
  base = std::move(b);
  shade = std::move(s);
  homeMode = std::move(h);
  return true;
}

static void eraseSplitKeys(ConfigStore& store) {
  // Every record slot any build wrote, whatever the count says: a split save
  // cut short before its count landed leaves records the count never named.
  char key[5];
  for (size_t i = 0; i < kRecordKeySlots; ++i) {
    recordKey(i, key);
    store.erase(key);
  }
  store.erase(kCountKey);
  store.erase(kKnockoutKey);
}

static void eraseRest(ConfigStore& store) {
  for (const char* key : kRestKeys) store.erase(key);
  store.erase(kLampCfgKey);
}

struct RestBlob {
  int slot = -1;  // the key holding the live blob, -1 for none
  // A rest key holds a blob that did not read: over the buffer, or a chunk
  // that failed its CRC (IDF erases the chunk then, and the blob on the next
  // read or boot).
  bool unreadable = false;
};

// The live rest blob, read into `buf`, NUL-terminated.
static RestBlob liveRest(ConfigStore& store, char* buf, size_t cap) {
  RestBlob found;
  for (int i = 0; i < 2 && found.slot < 0; ++i) {
    const size_t len = store.readBlob(kRestKeys[i], buf, cap - 1);
    if (len > 0) {
      buf[len] = '\0';
      found.slot = i;
    } else if (store.has(kRestKeys[i])) {
      found.unreadable = true;
    }
  }
  return found;
}

// Writes the JSON `write` produces to the rest key that is not live, erased
// first, then erases the live one. A failed write touches only a key that
// held nothing live: IDF's cleanup of a failed blob write can take a chunk of
// the version it replaces.
template <class Write>
static bool writeRest(ConfigStore& store, char* buf, size_t cap, Write write) {
  const RestBlob live = liveRest(store, buf, cap);
  JsonWriter w(buf, cap);
  write(w);
  w.finish();
  if (!w.ok()) return false;
  const char* target = kRestKeys[live.slot == 0 ? 1 : 0];
  if (!store.erase(target) || store.writeBlob(target, buf, w.length()) == 0) return false;
  return live.slot < 0 || store.erase(kRestKeys[live.slot]);
}

template <class Write>
static size_t build(char* buf, size_t cap, Write write) {
  JsonWriter w(buf, cap);
  write(w);
  w.finish();
  return w.ok() ? w.length() : 0;
}

static bool writeSplitKeys(ConfigStore& store, const BaseSettings& base,
                           const ExpressionSettings& expressions) {
  char key[5];
  const size_t count = std::min(expressions.expressions.size(), kMaxExpressions);
  uint8_t record[kMaxExpressionRecordLen];
  for (size_t i = 0; i < count; ++i) {
    const size_t len = encodeExpression(expressions.expressions[i], record);
    recordKey(i, key);
    if (store.writeBlob(key, record, len) == 0) return false;
  }
  if (!base.knockoutPixels.empty() &&
      store.writeBlob(kKnockoutKey, base.knockoutPixels.data(), base.knockoutPixels.size()) ==
          0) {
    return false;
  }
  // A string, which a failed rewrite leaves as it was.
  char countText[4];
  std::snprintf(countText, sizeof(countText), "%u", static_cast<unsigned>(count));
  if (store.write(kCountKey, countText) == 0) return false;
  // Every slot past the count, so a record left by a save cut short is gone.
  for (size_t i = count; i < kRecordKeySlots; ++i) {
    recordKey(i, key);
    store.erase(key);
  }
  return true;
}

SaveResult saveConfig(ConfigStore& store, const LampSettings& lamp,
                      const BaseSettings& base, const ShadeSettings& shade,
                      const ExpressionSettings& expressions,
                      const HomeModeSettings& homeMode, char* cfgBuffer,
                      size_t cfgBufferLen) {
  const auto whole = [&](JsonWriter& w) {
    writeStoredJson(w, &lamp, base, shade, homeMode, &expressions);
  };
  const auto rest = [&](JsonWriter& w) {
    writeStoredJson(w, nullptr, base, shade, homeMode, nullptr);
  };
  const auto lampOnly = [&](JsonWriter& w) { writeLampOnlyJson(w, lamp); };

  // Whole: everything in the one "cfg" string, the form every firmware reads.
  // A cfg string carrying an expressions array is the authority from the
  // moment it lands, so every other key goes after it.
  if (build(cfgBuffer, std::min(cfgBufferLen, kMaxWholeCfgLen + 1), whole) > 0) {
    if (store.write("cfg", cfgBuffer) == 0) {
      // NVS could not place the string beside the split keys. Stage it as a
      // rest blob, which may span pages and is the authority once the cfg
      // string is gone, then clear the room and place the string.
      if (!writeRest(store, cfgBuffer, cfgBufferLen, whole) || !store.erase("cfg")) {
        return SaveResult::WriteFailed;
      }
      // "cfgl" too: a save cut short can leave it, and the whole string is
      // placed with only the staged blob beside the system keys.
      eraseSplitKeys(store);
      store.erase(kLampCfgKey);
      if (build(cfgBuffer, cfgBufferLen, whole) == 0 || store.write("cfg", cfgBuffer) == 0) {
        return SaveResult::WriteFailed;
      }
    }
    eraseSplitKeys(store);
    eraseRest(store);
    return SaveResult::Ok;
  }

  // Split: the expressions as records beside the rest. The rest must fit the
  // blob form, checked before any write so TooLarge writes nothing.
  if (build(cfgBuffer, cfgBufferLen, rest) == 0) {
    return SaveResult::TooLarge;
  }

  // The rest as a blob and the lamp section alone in "cfg". The lamp section
  // lives in one of three places: a staged whole rest blob when that is the
  // live one, else "cfg" when it holds the lamp section alone, else "cfgl"
  // (a save cut short with "cfg" erased). Unless "cfg" holds it, the new lamp
  // section is staged in "cfgl" first, since from when "cfg" is erased until
  // the new one lands it is the only copy; a "cfgl" that is the current copy
  // stays as it is. A stale lamp-only "cfg" (a staged whole is live) goes
  // before the rest blob replaces that whole; any other "cfg" (a whole
  // string) is the authority until the rest blob lands, and goes after.
  const bool stagedWhole = liveRest(store, cfgBuffer, cfgBufferLen).slot >= 0 &&
                           std::strstr(cfgBuffer, "\"expressions\":") != nullptr;
  const bool cfgPresent = store.has("cfg");
  const size_t cfgLen = store.readString("cfg", cfgBuffer, cfgBufferLen);
  const bool cfgIsLampOnly = cfgLen > 0 && std::strstr(cfgBuffer, "\"base\":") == nullptr &&
                             std::strstr(cfgBuffer, "\"expressions\":") == nullptr;
  const bool cfgHoldsLamp = cfgIsLampOnly && !stagedWhole;
  const bool cfgIsAuthority = cfgPresent && !cfgIsLampOnly;
  const bool lampBlobHoldsLamp = !cfgPresent && !stagedWhole && store.has(kLampCfgKey);
  if (!cfgHoldsLamp && !lampBlobHoldsLamp) {
    const size_t len = build(cfgBuffer, cfgBufferLen, lampOnly);
    if (len == 0 || store.writeBlob(kLampCfgKey, cfgBuffer, len) == 0) {
      return SaveResult::WriteFailed;
    }
  }
  if ((cfgIsLampOnly && !cfgHoldsLamp && !store.erase("cfg")) ||
      !writeSplitKeys(store, base, expressions) ||
      !writeRest(store, cfgBuffer, cfgBufferLen, rest) ||
      (cfgIsAuthority && !store.erase("cfg"))) {
    return SaveResult::WriteFailed;
  }
  if (build(cfgBuffer, cfgBufferLen, lampOnly) == 0 || store.write("cfg", cfgBuffer) == 0) {
    return SaveResult::WriteFailed;
  }
  store.erase(kLampCfgKey);
  return SaveResult::Ok;
}

LoadedRecords loadRecords(ConfigStore& store, BaseSettings& base,
                          ExpressionSettings& expressions, bool cfgHasKnockout,
                          bool cfgHasExpressions) {
  LoadedRecords result;
  if (!cfgHasKnockout) {
    uint8_t knockout[255];
    const size_t knockoutLen = store.readBlob(kKnockoutKey, knockout, sizeof(knockout));
    for (size_t i = 0; i < knockoutLen && i < base.knockoutPixels.size(); ++i) {
      base.knockoutPixels[i] = knockout[i];
    }
  }
  if (cfgHasExpressions) return result;
  size_t n = 0;
  char countText[4];
  if (store.readString(kCountKey, countText, sizeof(countText)) == 0) return result;
  n = std::strtoul(countText, nullptr, 10);
  result.present = true;
  expressions.expressions.clear();
  expressions.expressions.reserve(std::min<size_t>(n, kMaxExpressions));
  uint8_t record[kMaxExpressionRecordLen];
  char key[5];
  for (size_t i = 0; i < n && i < kMaxExpressions; ++i) {
    recordKey(i, key);
    const size_t len = store.readBlob(key, record, sizeof(record));
    ExpressionConfig expr;
    if (len == 0 || !decodeExpression(record, len, expr)) {
      result.droppedMask |= 1u << i;
      continue;
    }
    // A save interrupted mid-shift (a remove moves later records down one
    // slot) leaves the moved record in two slots; the first is the live one.
    const bool duplicate = std::any_of(
        expressions.expressions.begin(), expressions.expressions.end(),
        [&](const ExpressionConfig& e) {
          return e.type == expr.type && e.target == expr.target;
        });
    if (duplicate) {
      ++result.duplicates;
      continue;
    }
    expressions.expressions.push_back(std::move(expr));
  }
  return result;
}

LoadedRecords loadStoredConfig(ConfigStore& store, JsonObject root, LampSettings& lamp,
                               BaseSettings& base, ShadeSettings& shade,
                               ExpressionSettings& expressions,
                               HomeModeSettings& homeMode, bool& withinCaps) {
  // A cfg string with an expressions array (a whole save, or any older
  // release's) is the authority over every blob; one left beside it is a
  // save cut short or a downgrade's leftover, and goes.
  if (root["expressions"].is<JsonArray>()) {
    withinCaps = fromJson(root, lamp, base, shade, expressions, homeMode);
    eraseRest(store);
    return LoadedRecords{};
  }
  // Otherwise the live rest blob holds the config: a staged whole one when it
  // carries expressions, else the rest beside the records, with the lamp
  // section from the cfg string, or from "cfgl" while a save has it erased.
  bool unreadable = false;
  std::unique_ptr<char[]> buf(new (std::nothrow) char[kMaxStoredCfgJsonLen + 1]);
  const size_t cap = kMaxStoredCfgJsonLen + 1;
  JsonDocument doc;
  JsonObject from = root;
  if (!buf) {
    unreadable = true;
  } else {
    const RestBlob live = liveRest(store, buf.get(), cap);
    unreadable = live.unreadable;
    if (live.slot >= 0 &&
        deserializeJson(doc, buf.get(), std::strlen(buf.get())) == DeserializationError::Ok &&
        doc.is<JsonObject>()) {
      from = doc.as<JsonObject>();
      if (!from["expressions"].is<JsonArray>()) {
        if (root["lamp"].is<JsonObject>()) {
          from["lamp"] = root["lamp"];
        } else {
          const size_t lampLen = store.readBlob(kLampCfgKey, buf.get(), cap);
          JsonDocument lampDoc;
          if (lampLen > 0 &&
              deserializeJson(lampDoc, buf.get(), lampLen) == DeserializationError::Ok &&
              lampDoc["lamp"].is<JsonObject>()) {
            from["lamp"] = lampDoc["lamp"];
          } else if (store.has(kLampCfgKey)) {
            unreadable = true;
          }
        }
      }
    } else if (live.slot >= 0 || root["lamp"].is<JsonObject>()) {
      // A rest blob that did not parse, or none beside a lamp-only cfg
      // string: a split store has lost its rest.
      unreadable = true;
    }
  }
  withinCaps = fromJson(from, lamp, base, shade, expressions, homeMode);
  LoadedRecords loaded;
  if (!from["expressions"].is<JsonArray>()) {
    loaded = loadRecords(store, base, expressions, from["base"]["knockout"].is<JsonArray>(),
                         false);
  }
  loaded.unreadable = unreadable;
  return loaded;
}

namespace {

// The settings blob, in two keys by turns as the rest blob is: with both
// present the first is live, for saves and loads alike.
constexpr const char* kSettingsKeys[2] = {"st0", "st1"};
// Every key a release up to 1.3.3 wrote the config to, besides the records.
constexpr const char* kLegacyKeys[] = {"cfg", "cfgb", "cfgc", "cfgl", "ko"};

struct LiveSettings {
  int slot = -1;  // the key holding the live blob, -1 for none
  size_t len = 0;
  bool unreadable = false;  // a settings key holds a blob that did not read
};

LiveSettings liveSettings(ConfigStore& store, uint8_t* buf, size_t cap) {
  LiveSettings found;
  for (int i = 0; i < 2 && found.slot < 0; ++i) {
    const size_t len = store.readBlob(kSettingsKeys[i], buf, cap);
    if (len > 0) {
      found.slot = i;
      found.len = len;
    } else if (store.has(kSettingsKeys[i])) {
      found.unreadable = true;
    }
  }
  return found;
}

bool hasLegacyKey(ConfigStore& store) {
  for (const char* key : kLegacyKeys) {
    if (store.has(key)) return true;
  }
  return false;
}

bool eraseLegacyKeys(ConfigStore& store) {
  bool erased = true;
  for (const char* key : kLegacyKeys) erased = store.erase(key) && erased;
  return erased;
}

}  // namespace

const char* storeFormName(StoreForm form) {
  switch (form) {
    case StoreForm::Binary: return "binary";
    case StoreForm::Whole: return "whole";
    case StoreForm::Split: return "split";
    default: return "empty";
  }
}

LoadedConfig loadConfig(ConfigStore& store, LampSettings& lamp, BaseSettings& base,
                        ShadeSettings& shade, ExpressionSettings& expressions,
                        HomeModeSettings& homeMode, uint8_t* buffer, size_t bufferLen) {
  LoadedConfig loaded;
  const LiveSettings live =
      liveSettings(store, buffer, std::min(bufferLen, kMaxSettingsBlobLen));
  if (live.slot >= 0 && decodeSettings(buffer, live.len, lamp, base, shade, homeMode)) {
    loaded.form = StoreForm::Binary;
    loaded.namedKeyPresent = true;
    loaded.records = loadRecords(store, base, expressions, true, false);
    loaded.records.unreadable = live.unreadable;
    eraseLegacyKeys(store);
    return loaded;
  }
  const bool settingsUnreadable = live.unreadable || live.slot >= 0;
  const std::string json = store.read("cfg", "{}");
  loaded.cfgBytes = json.length();
  JsonDocument doc;
  if (deserializeJson(doc, json)) {
    loaded.parseFailed = configBlobHasData(json);
    loaded.records.unreadable = settingsUnreadable;
    return loaded;
  }
  const bool legacy = hasLegacyKey(store);
  loaded.records = loadStoredConfig(store, doc.as<JsonObject>(), lamp, base, shade, expressions,
                                    homeMode, loaded.withinCaps);
  loaded.records.unreadable |= settingsUnreadable;
  loaded.namedKeyPresent = doc["lamp"]["named"].is<bool>();
  if (legacy) loaded.form = loaded.records.present ? StoreForm::Split : StoreForm::Whole;
  return loaded;
}

// The literal `lit` at `c`, followed by the end of a value: an ArduinoJson
// unquoted string that starts with a literal's letters is no literal.
static size_t literalAt(const char* c) {
  for (const char* lit : {"true", "false", "null"}) {
    const size_t n = std::strlen(lit);
    if (std::strncmp(c, lit, n) != 0) continue;
    const char* q = c + n;
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') ++q;
    if (*q == '\0' || *q == ',' || *q == ']' || *q == '}') return n;
  }
  return 0;
}

RawJsonShape scanRawJson(const char* json) {
  // The last kSeen short strings, compared byte for byte: a string seen
  // there is one ArduinoJson already holds and costs no new block. Raw bytes
  // that differ (escapes) count twice, which errs high.
  constexpr size_t kSeen = 32;
  constexpr size_t kSeenLen = 15;
  char seen[kSeen][kSeenLen + 1] = {};
  size_t nextSeen = 0;
  // ArduinoJson also takes single-quoted and unquoted strings, which this
  // scan does not measure, so outside double quotes only strict JSON passes:
  // structure, numbers and the three literals, with every key quoted.
  constexpr size_t kMaxDepth = 32;
  bool inObject[kMaxDepth] = {};
  size_t depth = 0;
  bool expectKey = false;
  RawJsonShape shape;
  bool inString = false;
  const char* start = nullptr;
  for (const char* c = json; *c; ++c, ++shape.len) {
    if (*c == '{' || *c == '[' || *c == ':' || *c == ',') ++shape.tokens;
    if (!inString) {
      const char ch = *c;
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
      if (expectKey && ch != '"' && ch != '}') shape.strict = false;
      expectKey = false;
      if (ch == '"') {
        inString = true;
        start = c + 1;
      } else if (ch == '{' || ch == '[') {
        if (depth == kMaxDepth) {
          shape.strict = false;
        } else {
          inObject[depth++] = ch == '{';
        }
        expectKey = ch == '{';
      } else if (ch == '}' || ch == ']') {
        if (depth > 0) --depth;
      } else if (ch == ',') {
        expectKey = depth > 0 && inObject[depth - 1];
      } else if (ch == ':' || ch == '-' || ch == '+' || ch == '.' || (ch >= '0' && ch <= '9')) {
      } else if ((ch == 'e' || ch == 'E') && c > json &&
                 ((c[-1] >= '0' && c[-1] <= '9') || c[-1] == '.')) {
      } else if (const size_t n = literalAt(c)) {
        c += n - 1;
        shape.len += n - 1;
      } else {
        shape.strict = false;
      }
      continue;
    }
    if (*c == '\\' && c[1]) {
      ++c;
      ++shape.len;
    } else if (*c == '"') {
      inString = false;
      const size_t run = static_cast<size_t>(c - start);
      shape.longest = std::max(shape.longest, run);
      bool repeat = false;
      if (run <= kSeenLen) {
        for (size_t i = 0; i < kSeen && !repeat; ++i) {
          repeat = std::strlen(seen[i]) == run && std::memcmp(seen[i], start, run) == 0;
        }
        if (!repeat) {
          std::memcpy(seen[nextSeen], start, run);
          seen[nextSeen][run] = '\0';
          nextSeen = (nextSeen + 1) % kSeen;
        }
      }
      if (!repeat) ++shape.distinct;
    }
  }
  if (inString) {
    shape.longest = std::max(shape.longest, static_cast<size_t>(shape.len - (start - json)));
    ++shape.distinct;
  }
  return shape;
}

SaveResult rawParseResult(DeserializationError error) {
  if (error == DeserializationError::Ok) return SaveResult::Ok;
  return error == DeserializationError::NoMemory ? SaveResult::NoHeap : SaveResult::Invalid;
}

size_t RawJsonShape::heapBytes() const {
  return kRawSlotBytes * (tokens + 1) + len + kRawStringBytes * distinct +
         kRawLongestPerByte * longest + kRawModelBytes;
}

size_t RawJsonShape::largestBlock() const {
  return std::max(kRawPoolBytes, 2 * longest + kRawStringSlack);
}

SaveResult saveRawJson(ConfigStore& store, const char* json, char* cfgBuffer,
                       size_t cfgBufferLen, const std::string& keptPassword) {
  if (json == nullptr) return SaveResult::Invalid;
  const RawJsonShape shape = scanRawJson(json);
  if (!shape.strict) return SaveResult::Invalid;
  if (!heapHasFree(shape.heapBytes()) || !heapCanSpare(shape.largestBlock())) {
    return SaveResult::NoHeap;
  }
  JsonDocument doc;
  const SaveResult parsed = rawParseResult(deserializeJson(doc, json, shape.len));
  if (parsed != SaveResult::Ok) return parsed;
  if (!doc["lamp"].is<JsonObject>()) return SaveResult::Invalid;
  // A longer password could never be typed back, and a longer SSID names no
  // network, so the save is refused rather than stored truncated.
  if (std::strlen(doc["lamp"]["password"] | "") > kMaxPasswordLen ||
      std::strlen(doc["homeMode"]["ssid"] | "") > kMaxSsidLen) {
    return SaveResult::Invalid;
  }
  const bool bodyNamed = (doc["lamp"]["name"] | "")[0] != '\0';
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
  fromJson(doc.as<JsonObject>(), lamp, base, shade, expressions, homeMode);
  // The GET document redacts the password, so a body without one keeps the
  // stored one; a body carrying a name (one left after fromJson normalizes
  // it) is an explicit configuration.
  if (lamp.password.empty()) lamp.password = keptPassword;
  if (bodyNamed && !lamp.name.empty()) lamp.named = true;
  return saveConfig(store, lamp, base, shade, expressions, homeMode, cfgBuffer, cfgBufferLen);
}

}  // namespace config_codec
}  // namespace lamp

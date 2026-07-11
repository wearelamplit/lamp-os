#include "config/config_codec.hpp"

#include "util/color.hpp"

namespace lamp {
namespace config_codec {

// Replace a role's segment list from `node["segments"]`. Absent/empty array
// keeps the current (variant-default) list. Every segment is left with ≥1
// color so broadcastColors() is always dereferenceable.
static void parseSegments(JsonObject node, std::vector<SegmentSettings>& segs,
                          const Color& fallback) {
  JsonArray arr = node["segments"];
  if (arr.isNull() || arr.size() == 0) return;
  segs.clear();
  for (JsonObject segNode : arr) {
    SegmentSettings seg;
    seg.name = std::string(segNode["name"] | "");
    seg.px = segNode["px"] | 0;
    for (JsonVariant c : segNode["colors"].as<JsonArray>()) {
      seg.colors.push_back(hexStringToColor(c));
    }
    if (seg.colors.empty()) seg.colors.push_back(fallback);
    segs.push_back(std::move(seg));
  }
  if (segs.empty()) segs.push_back({"", 0, {fallback}});
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

static void writeSegments(JsonObject node,
                          const std::vector<SegmentSettings>& segs) {
  JsonArray arr = node["segments"].to<JsonArray>();
  for (const auto& s : segs) {
    JsonObject o = arr.add<JsonObject>();
    o["name"] = s.name;
    o["px"] = s.px;
    JsonArray colors = o["colors"].to<JsonArray>();
    for (const auto& c : s.colors) colors.add(colorToHexString(c));
  }
}

void fromJson(JsonObject root, LampSettings& lamp, BaseSettings& base,
              ShadeSettings& shade, ExpressionSettings& expressions,
              HomeModeSettings& homeMode) {
  JsonObject lampNode = root["lamp"];
  // lampType is firmware-owned (see Config::setLampType).
  // Inbound settings_blob writes intentionally do not update it; the app
  // can read but not write the variant identity.
  lamp.name = std::string(lampNode["name"] | "stray");
  lamp.brightness = lampNode["brightness"] | 100;
  std::string password = std::string(lampNode["password"] | "");
  if (!password.empty()) {
    lamp.password = password;
  }
  lamp.setup = lampNode["setup"] | false;
  lamp.advancedEnabled = lampNode["advancedEnabled"] | false;
  lamp.webappEnabled = lampNode["webappEnabled"] | true;
  // SocialMode persists as uint8_t (0=Introvert, 1=Ambivert, 2=Extrovert).
  // Out-of-range values fall back to Ambivert so a corrupt or future-versioned
  // payload doesn't strand the lamp in an unknown personality.
  {
    uint32_t modeRaw = lampNode["socialMode"] | 1;
    if (modeRaw > 2) modeRaw = 1;
    lamp.socialMode = static_cast<SocialMode>(modeRaw);
  }

  JsonObject baseNode = root["base"];
  base.ac = baseNode["ac"] | 0;
  parseSegments(baseNode, base.segments, kBaseDefaultColor);
  clampSumPx(base.segments);
  if (base.ac >= base.broadcastColors().size()) {
    base.ac = 0;
  }
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
  parseSegments(shadeNode, shade.segments, kShadeDefaultColor);
  clampSumPx(shade.segments);
  // colorsEditable is firmware-owned (set by Lamp subclass defaults via
  // applyDefaults). Inbound writes intentionally do not update it.

  // Load expressions
  JsonArray expressionsNode = root["expressions"];
  if (expressionsNode) {
    expressions.expressions.clear();
    for (JsonObject exprNode : expressionsNode) {
      ExpressionConfig expr;
      expr.type = std::string(exprNode["type"] | "");
      expr.enabled = exprNode["enabled"] | false;
      expr.intervalMin = exprNode["intervalMin"] | 60;
      expr.intervalMax = exprNode["intervalMax"] | 900;
      expr.target = exprNode["target"] | 3;
      // disabledDuringWispOverride is a pure type-property, not loaded from
      // NVS. Old blobs carrying the key are tolerated; the skip-list below
      // drops it from the generic parameter loop.
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

  // Load home mode. A legacy "password" field in NVS is silently ignored
  // (presence-only home mode stores no password).
  JsonObject homeModeNode = root["homeMode"];
  if (homeModeNode) {
    homeMode.ssid = std::string(homeModeNode["ssid"] | "");
    homeMode.brightness = homeModeNode["brightness"] | 60;
    // Migration: lamps configured before `enabled` existed have no
    // "enabled" key in their NVS-stored JSON. Treat "has SSID" as proxy
    // for "user wanted home mode on" so they keep working post-update.
    homeMode.enabled = homeModeNode["enabled"] | !homeMode.ssid.empty();
  }

  // downstream code dereferences broadcastColors()[ac] / [0]; parseSegments
  // guarantees ≥1 segment with ≥1 color, so no empty-vector guard is needed.
  if (base.ac >= base.broadcastColors().size()) {
    base.ac = 0;
  }
}

void toJson(JsonObject root, const LampSettings& lamp, const BaseSettings& base,
            const ShadeSettings& shade, const ExpressionSettings& expressions,
            const HomeModeSettings& homeMode) {
  JsonObject lampNode = root["lamp"].to<JsonObject>();
  lampNode["name"] = lamp.name;
  lampNode["brightness"] = lamp.brightness;
  if (!lamp.password.empty()) {
    lampNode["password"] = lamp.password;
  }
  lampNode["setup"] = lamp.setup;
  lampNode["advancedEnabled"] = lamp.advancedEnabled;
  lampNode["webappEnabled"] = lamp.webappEnabled;
  lampNode["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  JsonObject baseNode = root["base"].to<JsonObject>();
  baseNode["ac"] = base.ac;
  writeSegments(baseNode, base.segments);
  // Positional uint8 array, same shape as asBaseJson. Keeps the on-disk
  // NVS format consistent with the BLE per-section read.
  JsonArray baseKnockoutNode = baseNode["knockout"].to<JsonArray>();
  for (int i = 0; i < base.knockoutPixels.size(); i++) {
    baseKnockoutNode.add((int)base.knockoutPixels[i]);
  }

  JsonObject shadeNode = root["shade"].to<JsonObject>();
  writeSegments(shadeNode, shade.segments);

  // Serialize expressions
  JsonArray expressionsNode = root["expressions"].to<JsonArray>();
  for (const auto& expr : expressions.expressions) {
    JsonObject exprNode = expressionsNode.add<JsonObject>();
    exprNode["type"] = expr.type;
    exprNode["enabled"] = expr.enabled;
    exprNode["intervalMin"] = expr.intervalMin;
    exprNode["intervalMax"] = expr.intervalMax;
    exprNode["target"] = expr.target;
    // disabledDuringWispOverride is a pure type-property, not persisted.
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

  JsonObject homeModeNode = root["homeMode"].to<JsonObject>();
  homeModeNode["ssid"] = homeMode.ssid;
  homeModeNode["brightness"] = homeMode.brightness;
  homeModeNode["enabled"] = homeMode.enabled;
}

}  // namespace config_codec
}  // namespace lamp

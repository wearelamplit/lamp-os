#include "config/config_codec.hpp"

#include "util/color.hpp"

namespace lamp {
namespace config_codec {

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
  lamp.colorsRandomized = lampNode["colorsRandomized"] | false;
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

  JsonObject baseNode = root["base"];
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

  JsonObject shadeNode = root["shade"];
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

  // Ensure both color vectors have at least one entry. Empty NVS (or NVS
  // erased / corrupted) returns "{}" with no colors arrays; downstream code
  // (e.g. bt.begin in lamp.cpp uses base.colors[ac] and shade.colors[0])
  // calls operator[] on a std::vector, which is UB on empty and crashes boot
  // with an invalid-PC fault. Fall back to the low-power class defaults so a
  // guard hit can't blast the strip at full-white max PSU draw.
  if (base.colors.empty()) {
    base.colors.push_back(kBaseDefaultColor);
  }
  if (shade.colors.empty()) {
    shade.colors.push_back(kShadeDefaultColor);
  }
  if (base.ac >= base.colors.size()) {
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
  lampNode["colorsRandomized"] = lamp.colorsRandomized;
  lampNode["advancedEnabled"] = lamp.advancedEnabled;
  lampNode["devMode"] = lamp.devMode;
  lampNode["webappEnabled"] = lamp.webappEnabled;
  lampNode["socialMode"] = static_cast<uint8_t>(lamp.socialMode);
  JsonObject baseNode = root["base"].to<JsonObject>();
  baseNode["px"] = base.px;
  baseNode["ac"] = base.ac;
  baseNode["bpp"] = base.bpp;
  baseNode["byteOrder"] = base.byteOrder;
  JsonArray baseColorsNode = baseNode["colors"].to<JsonArray>();
  for (int i = 0; i < base.colors.size(); i++) {
    baseColorsNode[i] = colorToHexString(base.colors[i]);
  }
  // Positional uint8 array, same shape as asBaseJson. Keeps the on-disk
  // NVS format consistent with the BLE per-section read.
  JsonArray baseKnockoutNode = baseNode["knockout"].to<JsonArray>();
  for (int i = 0; i < base.knockoutPixels.size(); i++) {
    baseKnockoutNode.add((int)base.knockoutPixels[i]);
  }

  JsonObject shadeNode = root["shade"].to<JsonObject>();
  shadeNode["px"] = shade.px;
  shadeNode["bpp"] = shade.bpp;
  shadeNode["byteOrder"] = shade.byteOrder;
  JsonArray shadeColorsNode = shadeNode["colors"].to<JsonArray>();
  for (int i = 0; i < shade.colors.size(); i++) {
    shadeColorsNode[i] = colorToHexString(shade.colors[i]);
  }

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

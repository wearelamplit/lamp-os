#pragma once

// The config model as the store tests build and compare it.

#include <ArduinoJson.h>

#include <string>
#include <vector>

#include "config/config_codec.hpp"

namespace lamp {

struct Model {
  LampSettings lamp;
  BaseSettings base;
  ShadeSettings shade;
  ExpressionSettings expressions;
  HomeModeSettings homeMode;
};

inline ExpressionConfig fullExpression(const char* type, uint8_t target) {
  ExpressionConfig e;
  e.type = type;
  e.enabled = true;
  e.target = target;
  e.intervalMin = 0xFFFFFFFFu;
  e.intervalMax = 0xFFFFFFFFu;
  for (size_t i = 0; i < kMaxExpressionColors; ++i) {
    e.colors.push_back(Color(static_cast<uint8_t>(i), 2, 3, 4));
  }
  for (size_t i = 0; i < kMaxExpressionParams; ++i) {
    e.parameters.set(kExpressionParamKeys[i], 0xFFFFFFFFu);
  }
  return e;
}

// Every capped field at its ceiling; `fill` is the character the free-text
// fields are made of, so a '"' or '\\' fill gives the fully escaped worst case.
inline Model worstCaseModel(char fill = 'z') {
  Model m;
  m.lamp.name = std::string(kMaxLampNameLen, fill);
  m.lamp.password = std::string(kMaxPasswordLen, fill);
  // Every number at its widest and every boolean "false", the longer form.
  m.lamp.brightness = 255;
  m.lamp.apBootMinutes = 255;
  m.lamp.brightnessCeiling = 255;
  m.lamp.socialMode = SocialMode::Extrovert;
  m.lamp.setup = m.lamp.named = m.lamp.advancedEnabled = m.lamp.webappEnabled = false;
  m.base.colorsEditable = m.shade.colorsEditable = false;
  m.homeMode.brightness = 255;
  m.homeMode.enabled = m.homeMode.networkBound = m.homeMode.socialDisabled = false;
  m.lamp.lampType = "standard";
  for (auto* segs : {&m.base.segments, &m.shade.segments}) {
    segs->clear();
    for (size_t s = 0; s < kMaxSegments; ++s) {
      SegmentSettings seg;
      seg.name = std::string(kMaxSegmentNameLen, fill);
      seg.px = 255;
      seg.pin = 33;
      seg.colors.assign(kMaxSurfaceColors, Color(255, 255, 255, 255));
      segs->push_back(seg);
    }
  }
  m.base.byteOrder = "GRBW";
  m.shade.byteOrder = "GRBW";
  m.base.knockoutPixels.assign(255, 100);
  m.homeMode.ssid = std::string(kMaxSsidLen, fill);
  m.homeMode.disabledExpressionTypes.assign(kMaxDisabledExpressionTypes,
                                            std::string(kMaxExpressionTypeLen, 't'));
  for (size_t i = 0; i < kMaxExpressions; ++i) {
    m.expressions.expressions.push_back(fullExpression(
        std::string(kMaxExpressionTypeLen, 'a' + i % 26).c_str(), 1 + i % 3));
  }
  return m;
}

// The web GET document: the model's every stored field, as JSON.
inline std::string webDocument(const Model& m) {
  JsonDocument doc;
  config_codec::toJson(doc.to<JsonObject>(), m.lamp, m.base, m.shade, m.expressions, m.homeMode);
  std::string out;
  serializeJson(doc, out);
  return out;
}

// Every field of the stored settings, and the web document, the first that
// differs named; "" when none does.
inline std::string firstDifference(const Model& a, const Model& b) {
  const auto& x = a.lamp;
  const auto& y = b.lamp;
  if (x.name != y.name) return "lamp.name " + x.name + " / " + y.name;
  if (x.brightness != y.brightness) return "lamp.brightness";
  if (x.password != y.password) return "lamp.password";
  if (x.setup != y.setup) return "lamp.setup";
  if (x.named != y.named) return "lamp.named";
  if (x.advancedEnabled != y.advancedEnabled) return "lamp.advancedEnabled";
  if (x.webappEnabled != y.webappEnabled) return "lamp.webappEnabled";
  if (x.apBootMinutes != y.apBootMinutes) return "lamp.apBootMinutes";
  if (x.brightnessCeiling != y.brightnessCeiling) return "lamp.brightnessCeiling";
  if (x.socialMode != y.socialMode) return "lamp.socialMode";
  if (x.lampType != y.lampType) return "lamp.lampType";
  auto segments = [](const char* role, const std::vector<SegmentSettings>& p,
                     const std::vector<SegmentSettings>& q) -> std::string {
    if (p.size() != q.size()) return std::string(role) + ".segments size";
    for (size_t i = 0; i < p.size(); ++i) {
      if (p[i].name != q[i].name) return std::string(role) + ".name";
      if (p[i].px != q[i].px) return std::string(role) + ".px";
      if (p[i].pin != q[i].pin) return std::string(role) + ".pin";
      if (p[i].builtinPin != q[i].builtinPin) return std::string(role) + ".builtinPin";
      if (p[i].colors != q[i].colors) return std::string(role) + ".colors";
    }
    return "";
  };
  std::string d = segments("base", a.base.segments, b.base.segments);
  if (!d.empty()) return d;
  d = segments("shade", a.shade.segments, b.shade.segments);
  if (!d.empty()) return d;
  if (a.base.knockoutPixels != b.base.knockoutPixels) return "base.knockout";
  if (a.base.byteOrder != b.base.byteOrder) return "base.byteOrder";
  if (a.shade.byteOrder != b.shade.byteOrder) return "shade.byteOrder";
  if (a.base.colorsEditable != b.base.colorsEditable) return "base.colorsEditable";
  if (a.shade.colorsEditable != b.shade.colorsEditable) return "shade.colorsEditable";
  if (a.base.rotation != b.base.rotation) return "base.rotation";
  if (a.shade.rotation != b.shade.rotation) return "shade.rotation";
  if (a.base.rotationSupported != b.base.rotationSupported) return "base.rotationSupported";
  if (a.shade.rotationSupported != b.shade.rotationSupported) return "shade.rotationSupported";
  const auto& h = a.homeMode;
  const auto& k = b.homeMode;
  if (h.ssid != k.ssid) return "home.ssid";
  if (h.brightness != k.brightness) return "home.brightness";
  if (h.enabled != k.enabled) return "home.enabled";
  if (h.networkBound != k.networkBound) return "home.networkBound";
  if (h.socialDisabled != k.socialDisabled) return "home.socialDisabled";
  if (h.disabledExpressionTypes != k.disabledExpressionTypes) return "home.types";
  if (webDocument(a) != webDocument(b)) return "web document";
  return "";
}

}  // namespace lamp

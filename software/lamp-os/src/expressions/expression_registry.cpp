#include "expressions/expression_registry.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace lamp {

namespace {

const char* surfaceStr(Surface s) {
  switch (s) {
    case Surface::Shade: return "shade";
    case Surface::Base:  return "base";
    default:             return "shade";
  }
}

void serializeBound(JsonObject parent, const char* key, const Bound& b) {
  switch (b.kind) {
    case Bound::Literal:
      parent[key] = b.v;
      break;
    case Bound::Pixels: {
      JsonObject obj = parent[key].to<JsonObject>();
      obj["rel"] = "pixels";
      if (b.v > 0) obj["cap"] = b.v;
      break;
    }
  }
}

void serializeRangeSpec(JsonObject parent, const RangeSpec& r) {
  parent["min"] = r.min;
  parent["max"] = r.max;
  parent["step"] = r.step;
  if (r.unit) parent["unit"] = r.unit;
  JsonArray def = parent["default"].to<JsonArray>();
  def.add(r.defLo);
  def.add(r.defHi);
  if (r.label) parent["label"] = r.label;
  if (r.minKey) parent["minKey"] = r.minKey;
  if (r.maxKey) parent["maxKey"] = r.maxKey;
}

uint32_t resolveBound(const Bound& b, uint16_t window) {
  switch (b.kind) {
    case Bound::Pixels:  return (b.v > 0) ? std::min<uint32_t>(window, static_cast<uint32_t>(b.v)) : window;
    case Bound::Literal: return static_cast<uint32_t>(b.v < 0 ? 0 : b.v);
  }
  return 0;
}

}  // namespace

void ExpressionRegistry::add(const ExpressionDescriptor& d) {
  for (auto& p : descriptors_) {
    if (std::strcmp(p->id, d.id) == 0) {
      p = &d;
      return;
    }
  }
  descriptors_.push_back(&d);
}

void ExpressionRegistry::remove(const char* id) {
  descriptors_.erase(
      std::remove_if(descriptors_.begin(), descriptors_.end(),
                     [id](const ExpressionDescriptor* p) {
                       return std::strcmp(p->id, id) == 0;
                     }),
      descriptors_.end());
}

const ExpressionDescriptor* ExpressionRegistry::find(const char* id) const {
  for (const auto* p : descriptors_) {
    if (std::strcmp(p->id, id) == 0) return p;
  }
  return nullptr;
}

const std::vector<const ExpressionDescriptor*>& ExpressionRegistry::all() const {
  return descriptors_;
}

void ExpressionRegistry::applyDefaults(const ExpressionDescriptor& d,
                                       std::map<std::string, uint32_t>& params,
                                       uint16_t window) const {
  for (const auto& p : d.params) {
    params.emplace(p.key, resolveBound(p.def, window));
  }
  auto foldRange = [&params](const std::optional<RangeSpec>& r) {
    if (!r.has_value()) return;
    if (r->minKey) params.emplace(r->minKey, static_cast<uint32_t>(r->defLo));
    if (r->maxKey) params.emplace(r->maxKey, static_cast<uint32_t>(r->defHi));
  };
  foldRange(d.interval);
  foldRange(d.duration);
}

std::string ExpressionRegistry::serializeCatalog() const {
  JsonDocument doc;
  doc["schemaVersion"] = 1;
  JsonArray exprs = doc["expressions"].to<JsonArray>();

  for (const auto* d : descriptors_) {
    JsonObject obj = exprs.add<JsonObject>();
    obj["id"] = d->id;
    obj["name"] = d->name;
    obj["continuous"] = d->continuous;
    if (d->pausesWispOverride) obj["pausesWispOverride"] = true;

    JsonObject colorsObj = obj["colors"].to<JsonObject>();
    colorsObj["max"] = d->colors.max;
    if (d->colors.label) colorsObj["label"] = d->colors.label;
    if (d->colors.help) colorsObj["help"] = d->colors.help;
    if (d->colors.inheritsSurface) colorsObj["inheritsSurface"] = true;

    if (d->interval.has_value()) {
      serializeRangeSpec(obj["interval"].to<JsonObject>(), *d->interval);
    }
    if (d->duration.has_value()) {
      serializeRangeSpec(obj["duration"].to<JsonObject>(), *d->duration);
    }

    if (d->hasZone) {
      JsonObject zoneObj = obj["zone"].to<JsonObject>();
      if (d->zoneOptional) zoneObj["optional"] = true;
    }

    if (!d->excludeTargets.empty()) {
      JsonArray excl = obj["excludeTargets"].to<JsonArray>();
      for (const auto& s : d->excludeTargets) excl.add(surfaceStr(s));
    }

    if (!d->params.empty()) {
      JsonArray paramsArr = obj["params"].to<JsonArray>();
      for (const auto& p : d->params) {
        JsonObject pObj = paramsArr.add<JsonObject>();
        pObj["key"] = p.key;
        pObj["type"] = (p.kind == ParamKind::Int) ? "int" : "enum";
        pObj["label"] = p.label;
        pObj["min"] = p.min;
        serializeBound(pObj, "max", p.max);
        pObj["step"] = p.step;
        serializeBound(pObj, "default", p.def);
        if (p.unit) pObj["unit"] = p.unit;
        if (p.invert) pObj["invert"] = true;
        if (p.leftLabel) pObj["leftLabel"] = p.leftLabel;
        if (p.rightLabel) pObj["rightLabel"] = p.rightLabel;
        if (p.requiresZoning) pObj["requiresZoning"] = true;
        if (p.kind == ParamKind::Enum && !p.options.empty()) {
          JsonArray opts = pObj["options"].to<JsonArray>();
          for (const auto& opt : p.options) {
            JsonObject optObj = opts.add<JsonObject>();
            optObj["value"] = opt.value;
            optObj["label"] = opt.label;
            if (opt.zoning) optObj["zoning"] = true;
          }
        }
      }
    }
  }

  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace lamp

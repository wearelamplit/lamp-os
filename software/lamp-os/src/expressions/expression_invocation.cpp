#include "expression_invocation.hpp"

#ifdef LAMP_DEBUG
#include <Arduino.h>
#endif

namespace lamp {

std::map<std::string, uint32_t> parametersWithoutCascadeKeys(
    const std::map<std::string, uint32_t>& parameters) {
  std::map<std::string, uint32_t> out;
  for (const auto& kv : parameters) {
    if (kv.first == kParamCascadeEnabled) continue;
    if (kv.first == kParamCascadeStaggerMs) continue;
    out.insert(kv);
  }
  return out;
}

uint32_t clampDelayMs(uint32_t v) {
  return v > kMaxDelayMs ? kMaxDelayMs : v;
}

void serializeInvocation(const ExpressionInvocation& inv, std::string& out) {
  JsonDocument doc;
  doc["type"] = inv.type;
  doc["target"] = inv.target;
  doc["delayMs"] = inv.delayMs;

  if (!inv.colors.empty()) {
    doc["colors"] = colorsToPackedHex(inv.colors);
  }

  JsonObject paramsObj = doc["parameters"].to<JsonObject>();
  for (const auto& kv : inv.parameters) {
    paramsObj[kv.first] = kv.second;
  }

  serializeJson(doc, out);
}

bool parseInvocation(JsonObjectConst doc, ExpressionInvocation& out) {
  const char* type = doc["type"].as<const char*>();
  if (!type || !*type) return false;

  out.type = type;

  // target is one of ExpressionTarget {SHADE=1, BASE=2, BOTH=3}. Coerce
  // anything else to BOTH so a malformed peer can't silently no-op the trigger.
  const uint32_t t = doc["target"] | 3;
  out.target = (t >= 1 && t <= 3) ? static_cast<uint8_t>(t) : 3;

  // delayMs from a remote peer is untrusted: an unbounded value would hold
  // a pendingTriggers slot for ~49 days. Clamp to kMaxDelayMs (see header).
  out.delayMs = clampDelayMs(doc["delayMs"] | 0u);

  out.colors.clear();
  const char* hex = doc["colors"].as<const char*>();
  if (hex && !packedHexToColors(hex, out.colors)) {
#ifdef LAMP_DEBUG
    Serial.printf("[invocation] dropping bad colors '%s'\n", hex);
#endif
  }

  out.parameters.clear();
  JsonObjectConst params = doc["parameters"].as<JsonObjectConst>();
  if (!params.isNull()) {
    for (JsonPairConst kv : params) {
      JsonVariantConst v = kv.value();
      // bool first; ArduinoJson types `true`/`false` as bool, distinct
      // from int. Coerce so callers can send the JSON-natural form too.
      if (v.is<bool>()) {
        out.parameters[std::string(kv.key().c_str())] =
            v.as<bool>() ? 1u : 0u;
      } else if (v.is<uint32_t>()) {
        out.parameters[std::string(kv.key().c_str())] = v.as<uint32_t>();
      } else if (v.is<int>()) {
        out.parameters[std::string(kv.key().c_str())] =
            static_cast<uint32_t>(v.as<int>());
      }
    }
  }

  return true;
}

}  // namespace lamp

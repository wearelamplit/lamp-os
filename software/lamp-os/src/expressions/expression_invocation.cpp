#include "expression_invocation.hpp"

#include "config/config_types.hpp"

#ifdef LAMP_DEBUG
#include <Arduino.h>
#endif

namespace lamp {

ExpressionParams parametersWithoutCascadeKeys(
    const ExpressionParams& parameters) {
  ExpressionParams out = parameters;
  out.erase(kParamCascadeEnabled);
  out.erase(kParamCascadeStaggerMs);
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
  if (inv.hops != 0) doc["hp"] = inv.hops;
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

  const uint32_t hops = doc["hp"] | 0u;
  out.hops = hops > UINT8_MAX ? UINT8_MAX : static_cast<uint8_t>(hops);

  out.colors.clear();
  const char* hex = doc["colors"].as<const char*>();
  if (hex && !packedHexToColors(hex, out.colors, kMaxExpressionColors)) {
#ifdef LAMP_DEBUG
    Serial.printf("[invocation] dropping bad colors '%s'\n", hex);
#endif
  }

  out.parameters.clear();
  JsonObjectConst params = doc["parameters"].as<JsonObjectConst>();
  if (!params.isNull()) {
    for (JsonPairConst kv : params) {
      if (out.parameters.size() == kMaxExpressionParams) break;
      if (expressionParamKeyIndex(kv.key().c_str()) < 0) continue;
      JsonVariantConst v = kv.value();
      // bool first; ArduinoJson types `true`/`false` as bool, distinct
      // from int. Coerce so callers can send the JSON-natural form too.
      if (v.is<bool>()) {
        out.parameters.set(kv.key().c_str(), v.as<bool>() ? 1u : 0u);
      } else if (v.is<uint32_t>()) {
        out.parameters.set(kv.key().c_str(), v.as<uint32_t>());
      } else if (v.is<int>()) {
        out.parameters.set(kv.key().c_str(), static_cast<uint32_t>(v.as<int>()));
      }
    }
  }

  return true;
}

}  // namespace lamp

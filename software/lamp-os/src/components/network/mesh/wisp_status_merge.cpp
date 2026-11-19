#include "wisp_status_merge.hpp"

#include <ArduinoJson.h>

#include <cstdio>

namespace lamp {

namespace {

// Lamp-side diagnostics, in the order they shed under maxLen.
// configLastSeenMs never sheds: it is the app's cue to re-read wispconfig.
constexpr const char* kShedOrder[] = {
    "helloPaletteIdPrefix", "helloLastSeenMs", "statusLastSeenMs",
    "helloFlags",
};

}  // namespace

std::string mergeWispStatusJson(const WispStatusMergeInput& in, size_t maxLen) {
  const bool locallyControlling =
      in.haveLampState && (in.controllingBase || in.controllingShade);
  // "{}" decodes cleanly on the app and reads as "no wisp detected".
  if (!in.present && in.statusJson.empty() && !locallyControlling) {
    return std::string("{}");
  }

  JsonDocument doc;
  DeserializationError parsed = DeserializationError::Ok;
  if (!in.statusJson.empty()) parsed = deserializeJson(doc, in.statusJson);
  if (parsed == DeserializationError::NoMemory) return std::string();
  // The wispStatus payload is open-set and wisp-owned; start from it.
  if (in.statusJson.empty() || parsed || !doc.is<JsonObject>()) {
    doc.clear();
    doc.to<JsonObject>();
  }
  // "char" tags the mesh frame; no reader of the served document wants it.
  doc.remove("char");

  // Status-payload keys win; isNull() skips fields the wisp already set.
  if (in.present) {
    char macStr[18];
    std::snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
                  in.mac[0], in.mac[1], in.mac[2], in.mac[3], in.mac[4],
                  in.mac[5]);
    if (doc["wispMac"].isNull()) doc["wispMac"] = macStr;
    if (in.wispVersion != 0 && doc["wispVersion"].isNull()) {
      doc["wispVersion"] = in.wispVersion;
    }
    if (in.lastHelloMs != 0 && doc["helloFlags"].isNull()) {
      doc["helloFlags"] = in.flags;
    }
    if (in.helloPaletteIdPrefix[0] != '\0' &&
        doc["helloPaletteIdPrefix"].isNull()) {
      doc["helloPaletteIdPrefix"] = in.helloPaletteIdPrefix;
    }
    if (in.lastHelloMs != 0 && doc["helloLastSeenMs"].isNull()) {
      doc["helloLastSeenMs"] = in.lastHelloMs;
    }
  }
  if (in.lastStatusMs != 0 && doc["statusLastSeenMs"].isNull()) {
    doc["statusLastSeenMs"] = in.lastStatusMs;
  }
  // The app re-reads the wispconfig section when this moves.
  if (in.lastConfigMs != 0 && doc["configLastSeenMs"].isNull()) {
    doc["configLastSeenMs"] = in.lastConfigMs;
  }

  if (in.haveLampState) {
    // Omitted when false, which every app reads as false.
    doc.remove("controllingBase");
    doc.remove("controllingShade");
    if (in.controllingBase) doc["controllingBase"] = true;
    if (in.controllingShade) doc["controllingShade"] = true;
    if (!in.baseWispColor.empty()) doc["baseWispColor"] = in.baseWispColor;
    if (!in.shadeWispColor.empty()) doc["shadeWispColor"] = in.shadeWispColor;
  }

  if (maxLen > 0) {
    for (const char* key : kShedOrder) {
      if (measureJson(doc) <= maxLen) break;
      doc.remove(key);
    }
  }

  if (doc.overflowed()) return std::string();
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool isJsonObject(const char* json, size_t len) {
  JsonDocument doc;
  return !deserializeJson(doc, json, len) && doc.is<JsonObject>();
}

bool storeIfChanged(std::string& cached, const char* json, size_t len) {
  if (cached.size() == len && cached.compare(0, len, json, len) == 0) {
    return false;
  }
  cached.assign(json, len);
  return true;
}

}  // namespace lamp

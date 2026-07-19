#include "status/status_json.hpp"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

namespace wisp {

namespace {

// App-parser defaults (wisp_status.dart). A field at its default is omitted;
// the parser reconstructs it.
constexpr uint32_t kDefaultDriftIntervalMs = 120000;
constexpr uint8_t  kDefaultDriftFadePct    = 50;
constexpr uint16_t kDefaultPixelCount      = 30;
constexpr char     kDefaultLedType[]       = "GRB";
constexpr uint8_t  kDefaultBrightness      = 100;

template <typename T>
void addIfFits(JsonDocument& doc, size_t cap, const char* key,
               const T& value) {
  doc[key] = value;
  if (measureJson(doc) > cap) doc.remove(key);
}

}  // namespace

size_t buildWispStatusJson(const WispStatusFields& f, char* out,
                           size_t outCap, size_t cap) {
  JsonDocument doc;
  // Guaranteed core. Worst-case serialized width is pinned well under cap by
  // test_status_json, so the optional fields below can only ever be dropped,
  // never force a failed frame.
  doc["char"]            = "wispStatus";
  doc["source"]          = f.source;
  doc["currentZone"]     = f.currentZone;
  doc["zoneSource"]      = f.zoneSource;
  doc["wifiConnected"]   = f.wifiConnected;
  doc["auroraConnected"] = f.auroraConnected;
  doc["lastSeenMs"]      = f.lastSeenMs;
  if (f.hasPassword) {
    doc["hasPassword"] = true;
  }

  const bool isOff = f.source && std::strcmp(f.source, "off") == 0;

  // Add-if-fits in priority order. offColor and paletteIdPrefix outrank the
  // cosmetic fields: a dropped prefix stalls the app's palette re-read and a
  // dropped offColor silently renders the app's amber default.
  if (isOff && f.hasOffColor) {
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%02x%02x%02x%02x",
                  f.offR, f.offG, f.offB, f.offW);
    doc["offColor"] = hex;
    if (measureJson(doc) > cap) doc.remove("offColor");
  }
  if (f.paletteIdPrefix && f.paletteIdPrefix[0] != '\0') {
    addIfFits(doc, cap, "paletteIdPrefix", f.paletteIdPrefix);
  }
  // shuffleSeed drives the app's predictTuple(); keep it above the cosmetics.
  if (f.shuffleSeed != 0) {
    addIfFits(doc, cap, "shuffleSeed", f.shuffleSeed);
  }
  // opSeq confirms a sealed op landed (app password-verify); above cosmetics.
  if (f.opSeq != 0) {
    addIfFits(doc, cap, "opSeq", f.opSeq);
  }
  if (f.name && f.name[0] != '\0') {
    addIfFits(doc, cap, "name", f.name);
  }
  if (!isOff) {
    if (f.driftIntervalMs != kDefaultDriftIntervalMs) {
      addIfFits(doc, cap, "driftIntervalMs", f.driftIntervalMs);
    }
    if (f.driftFadePct != kDefaultDriftFadePct) {
      addIfFits(doc, cap, "driftFadePct", f.driftFadePct);
    }
  }
  if (f.rangeStep != 0) {
    addIfFits(doc, cap, "range", f.rangeStep);
  }
  if (f.observedCount > 0) {
    JsonArray z = doc["observedZones"].to<JsonArray>();
    for (size_t i = 0; i < f.observedCount; ++i) {
      z.add(f.observedZones[i]);
      if (measureJson(doc) > cap) { z.remove(z.size() - 1); break; }
    }
    if (z.size() == 0) doc.remove("observedZones");
  }
  if (f.ledType && f.ledType[0] != '\0' &&
      std::strcmp(f.ledType, kDefaultLedType) != 0) {
    addIfFits(doc, cap, "ledType", f.ledType);
  }
  if (f.pixelCount > 0 && f.pixelCount != kDefaultPixelCount) {
    addIfFits(doc, cap, "px", f.pixelCount);
  }
  if (f.brightness != kDefaultBrightness) {
    addIfFits(doc, cap, "brightness", f.brightness);
  }
  return serializeJson(doc, out, outCap);
}

}  // namespace wisp

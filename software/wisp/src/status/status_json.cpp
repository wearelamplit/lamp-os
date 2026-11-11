#include "status/status_json.hpp"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

#include "util/uptime.hpp"

namespace wisp {

namespace {

// App-parser defaults (wisp_status.dart). A field at its default is omitted;
// the parser reconstructs it.
constexpr uint32_t kDefaultDriftIntervalMs = 120000;
constexpr uint8_t  kDefaultDriftFadePct    = 50;
constexpr uint16_t kDefaultPixelCount      = 30;
constexpr char     kDefaultLedType[]       = "GRB";
constexpr uint8_t  kDefaultBrightness      = 100;
constexpr char     kDefaultOscRole[]       = "primary";

template <typename T>
void addIfFits(JsonDocument& doc, size_t cap, const char* key,
               const T& value) {
  doc[key] = value;
  if (measureJson(doc) > cap) doc.remove(key);
}

void addConfigCopy(JsonDocument& doc, size_t cap, const WispConfigFields& c) {
  // shuffleSeed drives the app's predictTuple() and opSeq confirms a sealed
  // op landed, so both outrank the cosmetics below.
  if (c.shuffleSeed != 0) {
    addIfFits(doc, cap, "shuffleSeed", c.shuffleSeed);
  }
  if (c.opSeq != 0) {
    addIfFits(doc, cap, "opSeq", c.opSeq);
  }
  // Drift rides in every mode: the app's drift form is on screen in Off too,
  // and an omitted field there reads as the parser default.
  if (c.driftIntervalMs != kDefaultDriftIntervalMs) {
    addIfFits(doc, cap, "driftIntervalMs", c.driftIntervalMs);
  }
  if (c.driftFadePct != kDefaultDriftFadePct) {
    addIfFits(doc, cap, "driftFadePct", c.driftFadePct);
  }
  if (c.ledType && c.ledType[0] != '\0' &&
      std::strcmp(c.ledType, kDefaultLedType) != 0) {
    addIfFits(doc, cap, "ledType", c.ledType);
  }
  if (c.pixelCount > 0 && c.pixelCount != kDefaultPixelCount) {
    addIfFits(doc, cap, "px", c.pixelCount);
  }
  if (c.brightness != kDefaultBrightness) {
    addIfFits(doc, cap, "brightness", c.brightness);
  }
}

}  // namespace

size_t buildWispStatusJson(const WispStatusFields& f,
                           const WispConfigFields* configCopy, char* out,
                           size_t outCap, size_t cap) {
  JsonDocument doc;
  // Guaranteed core. Worst-case serialized width is pinned well under cap by
  // test_status_json, so the optional fields below can only ever be dropped,
  // never force a failed frame.
  doc["char"]          = "wispStatus";
  doc["source"]        = f.source;
  doc["wifiConnected"] = f.wifiConnected;
  doc["sourceLive"]    = f.sourceLive;
  doc["lastSeenMs"]    = uptimeMs();
  if (f.hasPassword) {
    doc["hasPassword"] = true;
  }

  const bool isOff = f.source && std::strcmp(f.source, "off") == 0;

  // Add-if-fits in priority order. offColor, oscRole and paletteIdPrefix
  // lead: a dropped prefix stalls the app's palette re-read, a dropped
  // offColor silently renders the app's amber default and a dropped oscRole
  // shows the wrong role selected. oscRole rides in every mode so the app
  // shows the right role the moment Osc is picked.
  if (isOff && f.hasOffColor) {
    char hex[9];
    std::snprintf(hex, sizeof(hex), "%02x%02x%02x%02x",
                  f.offR, f.offG, f.offB, f.offW);
    doc["offColor"] = hex;
    if (measureJson(doc) > cap) doc.remove("offColor");
  }
  if (f.oscRole && std::strcmp(f.oscRole, kDefaultOscRole) != 0) {
    addIfFits(doc, cap, "oscRole", f.oscRole);
  }
  if (f.paletteIdPrefix && f.paletteIdPrefix[0] != '\0') {
    addIfFits(doc, cap, "paletteIdPrefix", f.paletteIdPrefix);
  }
  if (f.name && f.name[0] != '\0') {
    addIfFits(doc, cap, "name", f.name);
  }
  // ethIp and the mismatch fields explain a wifiConnected=false the app would
  // otherwise blame on bad creds.
  if (f.ethIp && f.ethIp[0] != '\0') {
    addIfFits(doc, cap, "ethIp", f.ethIp);
  }
  if (f.wifiChannelMismatch) {
    addIfFits(doc, cap, "wifiChannelMismatch", true);
    if (f.wifiApChannel > 0) addIfFits(doc, cap, "wifiApChannel", f.wifiApChannel);
  }
  // TODO: drop configCopy and its parameter, and set "configSplit":true on
  // the status, once the fleet's lamps carry wispConfig. That removal is what
  // takes the config fields off the 30 s heartbeat; see docs/dev/networking.md.
  if (configCopy) addConfigCopy(doc, cap, *configCopy);
  return serializeJson(doc, out, outCap);
}

size_t buildWispConfigJson(const WispConfigFields& f, char* out,
                           size_t outCap) {
  JsonDocument doc;
  doc["char"]            = "wispConfig";
  doc["ledType"]         = f.ledType ? f.ledType : kDefaultLedType;
  doc["px"]              = f.pixelCount;
  doc["brightness"]      = f.brightness;
  doc["driftIntervalMs"] = f.driftIntervalMs;
  doc["driftFadePct"]    = f.driftFadePct;
  doc["shuffleSeed"]     = f.shuffleSeed;
  doc["opSeq"]           = f.opSeq;
  if (measureJson(doc) >= outCap) return 0;
  return serializeJson(doc, out, outCap);
}

}  // namespace wisp

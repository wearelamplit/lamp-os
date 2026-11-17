#include "status/status_json.hpp"
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>

#include "util/uptime.hpp"

namespace wisp {

namespace {

// wispConfig's ledType when the config holds none.
constexpr char     kDefaultLedType[]       = "GRB";
// The app parser's oscRole default (wisp_status.dart), omitted on the wire.
constexpr char     kDefaultOscRole[]       = "primary";

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
  doc["char"]          = "wispStatus";
  doc["source"]        = f.source;
  doc["wifiConnected"] = f.wifiConnected;
  doc["sourceLive"]    = f.sourceLive;
  doc["lastSeenMs"]    = uptimeMs();
  // An app reads a status without it as carrying the settings at its parser
  // defaults, so it rides the core.
  doc["configSplit"]   = true;
  if (f.hasPassword) {
    doc["hasPassword"] = true;
  }
  // Present in every status, "" included, so its absence names an older wisp.
  doc["manualId"]      = f.manualId ? f.manualId : "";
  if (f.manualSeq != 0) {
    doc["manualSeq"] = f.manualSeq;
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
  doc["ip"]              = f.ip;
  if (measureJson(doc) >= outCap) return 0;
  return serializeJson(doc, out, outCap);
}

}  // namespace wisp

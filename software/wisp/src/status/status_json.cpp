#include "status/status_json.hpp"
#include <ArduinoJson.h>
#include <cstring>

namespace wisp {

size_t buildWispStatusJson(const WispStatusFields& f, char* out,
                           size_t outCap, size_t cap) {
  JsonDocument doc;
  doc["char"]            = "wispStatus";
  doc["currentZone"]     = f.currentZone;
  doc["zoneSource"]      = f.zoneSource;
  JsonArray z            = doc["observedZones"].to<JsonArray>();
  doc["wifiConnected"]   = f.wifiConnected;
  doc["auroraConnected"] = f.auroraConnected;
  doc["paletteIdPrefix"] = f.paletteIdPrefix;
  doc["lastSeenMs"]      = f.lastSeenMs;
  doc["source"]          = f.source;
  // Off mode shows the offColor picker; Manual/Aurora show drift sliders.
  // Emitting only the relevant set keeps both within the 230 B cap.
  const bool isOff = f.source && std::strcmp(f.source, "off") == 0;
  if (isOff && f.hasOffColor) {
    JsonArray o = doc["offColor"].to<JsonArray>();
    o.add(f.offR); o.add(f.offG); o.add(f.offB);
  }
  // name before droppable fields so it survives budget pressure.
  if (f.name && f.name[0] != '\0') {
    doc["name"] = f.name;
  }
  // shuffleSeed is priority: without it the app's predictTuple() desyncs from
  // the wisp's paint. The lower-value drift fields and observedZones absorb
  // truncation first; the seed is dropped only as a last resort below.
  if (f.shuffleSeed) {
    doc["shuffleSeed"] = f.shuffleSeed;
  }
  // hasPassword is non-droppable: the app defaults a missing field to false,
  // which would wrongly assume open access when a password is set.
  doc["hasPassword"] = f.hasPassword;
  if (!isOff) {
    doc["driftIntervalMs"] = f.driftIntervalMs;
    if (measureJson(doc) > cap) doc.remove("driftIntervalMs");
    doc["driftFadePct"] = f.driftFadePct;
    if (measureJson(doc) > cap) doc.remove("driftFadePct");
  }
  // ponytail: O(n * measureJson), n <= 16 — trivial, and it gives a
  // by-construction guarantee the serialized doc never exceeds cap.
  for (size_t i = 0; i < f.observedCount; ++i) {
    z.add(f.observedZones[i]);
    if (measureJson(doc) > cap) { z.remove(z.size() - 1); break; }
  }
  // Last resorts: a produced frame beats a perfect one (a 0-length frame stops
  // all broadcasts, vanishing the wisp from the app). Drop in reverse priority.
  if (f.shuffleSeed && measureJson(doc) > cap) {
    doc.remove("shuffleSeed");
  }
  if (f.name && f.name[0] != '\0' && measureJson(doc) > cap) {
    doc.remove("name");
  }
  size_t n = serializeJson(doc, out, outCap);
  return (n > 0 && n <= cap) ? n : 0;
}

}  // namespace wisp

#include "status_json.hpp"
#include <ArduinoJson.h>

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
  if (f.hasOffColor) {
    JsonArray o = doc["offColor"].to<JsonArray>();
    o.add(f.offR); o.add(f.offG); o.add(f.offB);
  }
  // shuffleSeed: include only when non-zero AND it still fits under cap. A
  // non-zero seed can push the zero-zones base JSON past CONTROL_MAX_PAYLOAD;
  // drop it rather than failing the whole frame. A missing seed only costs the
  // app a less-accurate preview, but a failed frame makes the wisp stop
  // broadcasting status + palette entirely (vanishing from the app). The app
  // defaults to 0 when the key is absent.
  if (f.shuffleSeed) {
    doc["shuffleSeed"] = f.shuffleSeed;
    if (measureJson(doc) > cap) doc.remove("shuffleSeed");
  }
  // ponytail: O(n * measureJson), n <= 16 — trivial, and it gives a
  // by-construction guarantee the serialized doc never exceeds cap.
  for (size_t i = 0; i < f.observedCount; ++i) {
    z.add(f.observedZones[i]);
    if (measureJson(doc) > cap) { z.remove(z.size() - 1); break; }
  }
  size_t n = serializeJson(doc, out, outCap);
  return (n > 0 && n <= cap) ? n : 0;
}

}  // namespace wisp

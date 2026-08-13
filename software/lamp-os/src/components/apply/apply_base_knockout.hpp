// settings_blob path for a full base.knockoutPixels map. The
// per-pixel CHAR_BASE_KNOCKOUT drain remains as a separate live-preview
// path; this helper applies the bulk shape via the same applyKnockoutPixel
// helper.

#pragma once

#include <ArduinoJson.h>

namespace lamp {
void applyKnockoutPixel(uint8_t pixel, uint8_t brightness);

namespace apply {

inline void baseKnockoutLocal(JsonArray arr) {
  if (arr.isNull()) return;
  // Expected shape: [{"pixel": N, "brightness": B}, ...]
  for (JsonVariant v : arr) {
    if (!v.is<JsonObject>()) continue;
    JsonObject obj = v.as<JsonObject>();
    if (!obj["pixel"].is<int>() || !obj["brightness"].is<int>()) continue;
    int pixel = obj["pixel"].as<int>();
    int brightness = obj["brightness"].as<int>();
    if (pixel < 0 || pixel > 255) continue;
    if (brightness < 0 || brightness > 100) continue;
    ::lamp::applyKnockoutPixel(static_cast<uint8_t>(pixel),
                                static_cast<uint8_t>(brightness));
  }
}

}  // namespace apply
}  // namespace lamp

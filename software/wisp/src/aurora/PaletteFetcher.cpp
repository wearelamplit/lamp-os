#include "PaletteFetcher.h"
#include <HTTPClient.h>
#include <WiFiClient.h>

bool PaletteFetcher::fetchById(const char* id, Palette& out) {
    if (port_ == 0 || !id || !*id) return false;
    WiFiClient wifi;
    HTTPClient http;
    String url = "http://" + ip_.toString() + ":" + String(port_) +
                 "/api/v1/palettes/color/" + id;
    if (!http.begin(wifi, url)) return false;
    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[http] GET palette %s -> %d\n", id, code);
        http.end();
        return false;
    }
    String body = http.getString();
    http.end();
    bool ok = parsePalette(body.c_str(), out);
    Serial.printf("[http] palette %s ok=%d group=%d hex=%u rgb=%u\n",
                  id, ok, out.isGroup,
                  (unsigned)out.hexColors.size(), (unsigned)out.colors.size());
    return ok;
}

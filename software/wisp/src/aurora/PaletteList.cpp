#include "PaletteList.h"
#include <ArduinoJson.h>
#include <cstdlib>

bool parsePalette(const char* json, Palette& out) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    out = Palette{};
    out.id = doc["paletteId"] | "";
    out.name = doc["name"] | "";

    const char* type = doc["type"] | "";
    out.isGroup = (type && std::string(type) == "GROUP");

    if (out.isGroup) {
        for (JsonObjectConst c : doc["children"].as<JsonArrayConst>()) {
            out.childIds.push_back(std::string(c["id"] | ""));
        }
        return true;
    }

    // hexColors: decimal-encoded 24-bit RGB, sent as strings or numbers.
    for (JsonVariantConst h : doc["hexColors"].as<JsonArrayConst>()) {
        if (h.is<const char*>()) {
            out.hexColors.push_back(strtoull(h.as<const char*>(), nullptr, 10));
        } else {
            out.hexColors.push_back(h.as<uint64_t>());
        }
    }

    // colors: explicit float channels (r,g,b + optional w,am,u).
    for (JsonObjectConst c : doc["colors"].as<JsonArrayConst>()) {
        PaletteColor col;
        col.r  = c["r"]  | 0.0f;
        col.g  = c["g"]  | 0.0f;
        col.b  = c["b"]  | 0.0f;
        col.w  = c["w"]  | 0.0f;
        col.am = c["am"] | 0.0f;
        col.u  = c["u"]  | 0.0f;
        out.colors.push_back(col);
    }
    return true;
}

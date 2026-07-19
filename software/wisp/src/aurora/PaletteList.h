#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A color palette as returned by GET /api/v1/palettes/color/<id>.
// Field names are camelCase (protobuf->JSON). A palette may expose either or
// both color shapes:
//   - hexColors: decimal-encoded 24-bit RGB values (often sent as strings)
//   - colors:    explicit float channels (0..1): r,g,b plus optional
//                w (white), am (amber), u (uv), when present
// GROUP palettes carry no colors of their own; they reference child ids.
struct PaletteColor { float r = 0, g = 0, b = 0, w = 0, am = 0, u = 0; };

struct Palette {
    std::string id;
    std::string name;
    bool isGroup = false;
    std::vector<PaletteColor> colors;     // optional {r,g,b}
    std::vector<uint64_t> hexColors;      // 24-bit RGB, parsed from str or num
    std::vector<std::string> childIds;    // GROUP: child palette ids
};

// Parse a single-palette JSON object. Returns false on a JSON parse error.
bool parsePalette(const char* json, Palette& out);

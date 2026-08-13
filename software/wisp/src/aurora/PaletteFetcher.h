#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "PaletteList.h"

// Fetches a single palette: GET /api/v1/palettes/color/<id>, parsed into a
// Palette. (Fetching one id keeps the payload tiny; the full list is ~88 KB.)
class PaletteFetcher {
public:
    void setTarget(IPAddress ip, uint16_t port) { ip_ = ip; port_ = port; }
    bool fetchById(const char* id, Palette& out);

private:
    IPAddress ip_;
    uint16_t port_ = 0;
};

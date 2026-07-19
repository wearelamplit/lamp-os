#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace lamp {

// Returns true iff `s` is a canonical-form BD_ADDR string:
// 17 chars, hex pairs separated by ':' at positions 2/5/8/11/14.
// Accepts upper-, lower-, and mixed-case hex. Used by Config to
// silently drop pre-Phase-C name-keyed disposition entries from
// NVS on load.
//
// Zero-dependency: no Arduino, no std::string, char* in for native
// test compatibility.
inline bool isValidBdAddr(const char* s) {
  if (s == nullptr) return false;
  if (std::strlen(s) != 17) return false;
  for (int i = 0; i < 17; ++i) {
    const char c = s[i];
    if (i == 2 || i == 5 || i == 8 || i == 11 || i == 14) {
      if (c != ':') return false;
    } else {
      const bool isHex = (c >= '0' && c <= '9') ||
                         (c >= 'A' && c <= 'F') ||
                         (c >= 'a' && c <= 'f');
      if (!isHex) return false;
    }
  }
  return true;
}

// Parse a canonical BD_ADDR string into 6 bytes. Returns false (out[]
// untouched) unless `s` passes isValidBdAddr. Case-insensitive.
inline bool parseBdAddr(const char* s, uint8_t out[6]) {
  if (!isValidBdAddr(s)) return false;
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
  };
  for (int i = 0; i < 6; ++i) {
    const char* p = s + i * 3;
    out[i] = static_cast<uint8_t>((nibble(p[0]) << 4) | nibble(p[1]));
  }
  return true;
}

// Format 6 address bytes as the canonical uppercase colon-hex form
// ("AA:BB:CC:DD:EE:FF"). `out` must hold at least 18 bytes.
inline void formatBdAddr(const uint8_t in[6], char out[18]) {
  std::snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                in[0], in[1], in[2], in[3], in[4], in[5]);
}

// Recover a lamp's mesh (WiFi-STA) MAC from its scanned BLE address:
// the BLE address is the mesh MAC + 2 on ESP32, so subtract 2 with borrow.
inline void meshMacFromBleAddr(const uint8_t ble[6], uint8_t outMac[6]) {
  int borrow = 2;
  for (int i = 5; i >= 0; --i) {
    const int v = static_cast<int>(ble[i]) - borrow;
    outMac[i] = static_cast<uint8_t>(v & 0xFF);
    borrow = (v < 0) ? 1 : 0;
  }
}

}  // namespace lamp

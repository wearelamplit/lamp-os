#pragma once
#include <cstdint>

namespace ble_control {

// A claimed lamp's BLE address is its mesh (WiFi-STA) MAC + 2 on ESP32.
// 6-byte big-endian add-2 with carry.
inline void bdAddrFromMeshMac(const uint8_t mesh[6], uint8_t outBdAddr[6]) {
  uint16_t carry = 2;
  for (int i = 5; i >= 0; --i) {
    const uint16_t v = static_cast<uint16_t>(mesh[i]) + carry;
    outBdAddr[i] = static_cast<uint8_t>(v & 0xFF);
    carry = v >> 8;
  }
}

}  // namespace ble_control

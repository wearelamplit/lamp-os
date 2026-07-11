// Minimal IPAddress stub for native unit tests (wifi_link.hpp pulls this in).
#pragma once
#include <cstdint>

class IPAddress {
 public:
  IPAddress() = default;
  uint8_t operator[](int) const { return 0; }
};

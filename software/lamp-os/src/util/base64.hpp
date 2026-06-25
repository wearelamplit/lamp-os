#pragma once

// Tiny base64 encoder. Pulled in instead of mbedtls_base64_encode because
// we only need the encoder side (no padding-stripping, no chunking) and
// the wisp-palette payload is small (<=150 bytes → <=200 chars output).
// Header-only so unit tests can pick it up without an extra translation
// unit.

#include <cstddef>
#include <cstdint>
#include <string>

namespace lamp {
namespace base64 {

inline std::string encode(const uint8_t* data, size_t len) {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  if (len == 0) return out;
  out.reserve(((len + 2) / 3) * 4);
  size_t i = 0;
  while (i + 3 <= len) {
    const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                       (static_cast<uint32_t>(data[i + 1]) << 8) |
                       static_cast<uint32_t>(data[i + 2]);
    out.push_back(kAlphabet[(v >> 18) & 0x3F]);
    out.push_back(kAlphabet[(v >> 12) & 0x3F]);
    out.push_back(kAlphabet[(v >> 6) & 0x3F]);
    out.push_back(kAlphabet[v & 0x3F]);
    i += 3;
  }
  if (i < len) {
    uint32_t v = static_cast<uint32_t>(data[i]) << 16;
    if (i + 1 < len) v |= static_cast<uint32_t>(data[i + 1]) << 8;
    out.push_back(kAlphabet[(v >> 18) & 0x3F]);
    out.push_back(kAlphabet[(v >> 12) & 0x3F]);
    if (i + 1 < len) {
      out.push_back(kAlphabet[(v >> 6) & 0x3F]);
    } else {
      out.push_back('=');
    }
    out.push_back('=');
  }
  return out;
}

}  // namespace base64
}  // namespace lamp

#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace lamp {

// Canonical storage form for user-facing names: ASCII-lowercase. Display
// edges (app, web UI) title-case for presentation.
inline std::string asciiLower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

// Longest stored lamp name, the HELLO name field's width.
constexpr size_t kMaxLampNameLen = 32;

// The longest prefix of the `len` bytes at `s` that is at most `max` bytes
// and splits no UTF-8 sequence.
inline size_t utf8PrefixLen(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  while (max > 0 && (static_cast<unsigned char>(s[max]) & 0xC0) == 0x80) --max;
  return max;
}

// Shortens `s` to at most `max` bytes without splitting a UTF-8 sequence.
inline void truncateUtf8(std::string& s, size_t max) {
  s.resize(utf8PrefixLen(s.data(), s.size(), max));
}

// Canonical stored lamp name, at most kMaxLampNameLen bytes. Whitespace is dropped, not just trimmed: the
// name composes the setup AP's SSID, the BLE advert name and the mDNS host,
// and a space in any of those leaves an owner hunting for a network that
// does not match the name their app shows.
inline std::string normalizeLampName(std::string s) {
  s.erase(std::remove_if(s.begin(), s.end(),
                         [](unsigned char c) { return std::isspace(c); }),
          s.end());
  truncateUtf8(s, kMaxLampNameLen);
  return asciiLower(std::move(s));
}

}  // namespace lamp

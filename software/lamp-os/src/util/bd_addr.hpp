#pragma once

#include <cstddef>
#include <cstring>

namespace lamp {

// Returns true iff `s` is a canonical-form BD_ADDR string:
// 17 chars, hex pairs separated by ':' at positions 2/5/8/11/14.
// Accepts upper-, lower-, and mixed-case hex. Used by Config to
// silently drop pre-Phase-C name-keyed disposition entries from
// NVS on load.
//
// Zero-dependency: no Arduino, no std::string — char* in for native
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

}  // namespace lamp

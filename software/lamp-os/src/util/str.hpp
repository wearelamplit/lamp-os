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

}  // namespace lamp

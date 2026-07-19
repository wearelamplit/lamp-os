#pragma once

#include <cstdio>
#include <string>

namespace lamp {

// Append `s` to `out` with JSON string escaping (quote, backslash,
// control chars as \u00XX). Peer names are attacker-reachable over BLE
// adv and MSG_HELLO; an unescaped emit would let a crafted name inject
// fields into the nearby JSON.
inline void appendJsonEscaped(std::string& out, const char* s) {
  for (; *s; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04X", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
}

}  // namespace lamp

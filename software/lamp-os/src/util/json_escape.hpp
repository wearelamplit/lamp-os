#pragma once

#include <cstdio>
#include <string>

namespace lamp {

// Append `s` to `out` with JSON string escaping (quote, backslash,
// control chars as \u00XX). Peer names are attacker-reachable over BLE
// adv and MSG_HELLO; an unescaped emit would let a crafted name inject
// fields into the nearby JSON. `Out` takes `+= char` and `+= const char*`.
template <class Out>
inline void appendJsonEscaped(Out& out, const char* s, bool asciiOnly = false) {
  for (; *s; ++s) {
    const unsigned char c = static_cast<unsigned char>(*s);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20 || (asciiOnly && c >= 0x80)) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04X", c);
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
}

// For a field that is ASCII by contract: a byte past 0x7F is escaped too, so
// a crafted value cannot make the payload invalid UTF-8.
template <class Out>
inline void appendJsonEscapedAscii(Out& out, const char* s) {
  appendJsonEscaped(out, s, true);
}

}  // namespace lamp

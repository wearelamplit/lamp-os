// The Custom OSC address rule (oscCustomAddressValid) as cases. Any client
// that checks an address before sending it mirrors this list case for case,
// so the wisp and the client agree on every one.

#pragma once

struct OscAddressCase {
  const char* address;
  bool valid;
};

inline constexpr OscAddressCase kOscAddressCases[] = {
    {"/", true},
    {"/a", true},
    {"/lamp/palette", true},
    {"/palette/live/3", true},
    {"/A_Z-09.x", true},
    {"/0123456789abcd", true},   // 15 bytes, the cap
    // The allowed punctuation, over two cases.
    {"/!$%&'()+-.:;<=", true},
    {"/>@^_`|~", true},
    {"//", true},

    {"", false},
    {"a/b", false},              // no leading '/'
    {" /a", false},
    {"/0123456789abcde", false}, // 16 bytes
    {"/a b", false},             // 0x20
    {"/a\t", false},
    {"/a\x01", false},
    {"/a\x7f", false},
    {"/caf\xc3\xa9", false},     // é, outside printable ASCII
    {"/a\"", false},
    {"/a\\", false},
    {"/a#", false},
    {"/a*", false},
    {"/a,", false},
    {"/a?", false},
    {"/a[", false},
    {"/a]", false},
    {"/a{", false},
    {"/a}", false},
};

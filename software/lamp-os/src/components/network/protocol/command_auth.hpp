#pragma once

#include <cstddef>
#include <cstdint>

// Shared-key authentication for the two "force another lamp to do a thing"
// message types (MSG_EVENT, MSG_COMMAND). An 8-byte HMAC-SHA256 tag rides as a
// fixed trailer on those frames. The key is baked in at build time from
// LAMP_COMMAND_KEY_HEX (a GitHub secret / local signed-build key) and is absent
// from dev builds, so firmware built from source without the key can join the
// mesh but cannot forge a tag, and keyed lamps drop its EVENT/COMMAND.
//
// It is not real secrecy: secure boot is off and the signed binary is public,
// so the key is extractable by a determined reverser. It stops from-source
// firmware, not a binary-reverser. See docs/dev/networking.md.

namespace lamp_protocol { namespace command_auth {

constexpr size_t TAG_SIZE = 8;

// Parse the build-time key once, at boot, before any send/receive path runs.
// Idempotent.
void init();

// True when a key was compiled in. Keyless builds run permissive (see verify).
bool enabled();

// Append TAG_SIZE bytes at buf[bodyLen]: HMAC-SHA256(key, buf[0..bodyLen))[:8]
// when keyed, else zeros (frame stays a uniform size so keyless peers parse it).
// Returns bodyLen + TAG_SIZE, or bodyLen unchanged if bufCap can't hold the tag.
size_t appendTag(uint8_t* buf, size_t bodyLen, size_t bufCap);

// Verify tag[0..TAG_SIZE) against HMAC-SHA256(key, body[0..bodyLen))[:8].
// Keyless builds return true (can't verify -> accept, for bench/dev). Keyed
// builds return true only on a constant-time match.
bool verify(const uint8_t* body, size_t bodyLen, const uint8_t* tag);

// Test seam: a native test can't exercise keyed and keyless paths from one
// compile-time LAMP_COMMAND_KEY_HEX, so load/clear the key at runtime.
void loadKeyForTest(const uint8_t key[32]);
void clearKeyForTest();

}}  // namespace lamp_protocol::command_auth

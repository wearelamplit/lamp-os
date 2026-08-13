#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include <lampos/crypto.hpp>

namespace lamp { namespace crypto {

using lampos::crypto::MAGIC_PLAINTEXT;
using lampos::crypto::MAGIC_CIPHERTEXT;
using lampos::crypto::NONCE_LEN;
using lampos::crypto::TAG_LEN;
constexpr size_t MAX_RECENT_NONCES = 32;

// Wire bytes on top of the plaintext: magic + nonce + tag. AES-GCM
// ciphertext length equals plaintext length.
constexpr size_t WIRE_OVERHEAD = 1 + NONCE_LEN + TAG_LEN;

/// Per-connection sliding window of recently-seen nonces. Reset on
/// disconnect.
struct PerConnState {
  std::deque<std::array<uint8_t, NONCE_LEN>> recentNonces;
};

/// Returns true on success and writes the plaintext to [out]. Derive +
/// AES-256-GCM decrypt is the shared core in software/shared/crypto; this
/// wrapper adds a per-connection replay check, rejecting a duplicate nonce
/// within [conn.recentNonces].
bool decryptOp(const uint8_t* payload, size_t payloadLen,
               const uint8_t* charUuidLE16,    // 16 bytes
               const char* charShortName,
               const std::string& lampPassword,
               PerConnState& conn,
               std::string& out);

/// Returns the first byte, or 0 if empty.
uint8_t magicByte(const uint8_t* payload, size_t payloadLen);

}}  // namespace lamp::crypto

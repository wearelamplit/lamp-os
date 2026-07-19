// ponytail: derive+GCM core lives in software/shared/crypto (lampos::crypto);
// this tree keeps only its own replay wrapper.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

#include <lampos/crypto.hpp>

namespace wisp { namespace crypto {

using lampos::crypto::MAGIC_PLAINTEXT;
using lampos::crypto::MAGIC_CIPHERTEXT;
using lampos::crypto::NONCE_LEN;
using lampos::crypto::TAG_LEN;
constexpr size_t MAX_RECENT_NONCES = 32;

/// Sliding window of recently-seen nonces for replay detection.
/// RAM-only: a forced reboot clears it.
/// ponytail: reboot replay is accepted; config ops are low-value/idempotent.
struct RecentNonces {
  std::deque<std::array<uint8_t, NONCE_LEN>> entries;
};

/// Returns true on success and writes the plaintext to [out]. Derive +
/// AES-256-GCM decrypt is the shared core in software/shared/crypto; this
/// wrapper adds a replay check, rejecting a duplicate nonce in
/// [nonces.entries].
bool decryptOp(const uint8_t* payload, size_t payloadLen,
               const uint8_t* charUuidLE16,
               const char* charShortName,
               const std::string& password,
               RecentNonces& nonces,
               std::string& out);

/// Returns the first byte, or 0 if empty.
uint8_t magicByte(const uint8_t* payload, size_t payloadLen);

}}  // namespace wisp::crypto

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

namespace lamp { namespace crypto {

constexpr uint8_t MAGIC_PLAINTEXT  = 0x01;
constexpr uint8_t MAGIC_CIPHERTEXT = 0x02;
constexpr size_t  NONCE_LEN        = 12;
constexpr size_t  TAG_LEN          = 16;
constexpr size_t  MAX_RECENT_NONCES = 32;

/// Per-connection sliding window of recently-seen nonces. Reset on
/// disconnect.
struct PerConnState {
  std::deque<std::array<uint8_t, NONCE_LEN>> recentNonces;
};

/// Returns true on success and writes the plaintext to [out].
///
/// Wire format (n bytes total): [0x02][nonce 12B][tag 16B][ciphertext ...].
///
/// HKDF-SHA256 derivation:
///   salt = first 16 bytes of [charUuidLE16]
///   info = "lamp-v1\0" + [charShortName]
///   IKM  = [lampPassword]
///   key  = HKDF(salt, info, IKM, 32 bytes)
/// AES-256-GCM(key, nonce, ciphertext, AAD=empty) with the supplied tag.
///
/// Rejects (returns false, leaves [out] empty) on:
///   - payload too short (< 1 + NONCE_LEN + TAG_LEN)
///   - first byte != 0x02
///   - empty password (no key without a password)
///   - duplicate nonce within [conn.recentNonces]
///   - GCM auth-tag mismatch
bool decryptOp(const uint8_t* payload, size_t payloadLen,
               const uint8_t* charUuidLE16,    // 16 bytes
               const char* charShortName,
               const std::string& lampPassword,
               PerConnState& conn,
               std::string& out);

/// Returns the first byte, or 0 if empty.
uint8_t magicByte(const uint8_t* payload, size_t payloadLen);

}}  // namespace lamp::crypto

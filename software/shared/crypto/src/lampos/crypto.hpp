#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lampos { namespace crypto {

constexpr uint8_t MAGIC_PLAINTEXT  = 0x01;
constexpr uint8_t MAGIC_CIPHERTEXT = 0x02;
constexpr size_t  NONCE_LEN        = 12;
constexpr size_t  TAG_LEN          = 16;

/// HKDF-SHA256 key derivation + AES-256-GCM auth-decrypt. Does no replay
/// tracking; the caller runs its own replay check.
///
/// Wire format (len bytes total): [0x02][nonce 12B][tag 16B][ciphertext ...].
///
/// Returns true and writes plaintext to [out] on success. Returns false
/// (leaves [out] empty) on: payload too short (< 1 + NONCE_LEN + TAG_LEN),
/// first byte != 0x02, empty password, or GCM auth-tag mismatch.
bool decryptPayload(const uint8_t* payload, size_t len,
                    const uint8_t* charUuidLE16,    // 16 bytes
                    const char* charShortName,
                    const std::string& password,
                    std::string& out);

}}  // namespace lampos::crypto

#include "ota_signature.hpp"

#include <cstring>
#include <memory>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

// mbedTLS streaming SHA-256 over the signed region (4 KB blocks via the
// FirmwareByteReader) so the 1.4 MB image is never resident on the ~280 KB heap.
#include <mbedtls/sha256.h>

// ed25519 verify via libsodium (prebuilt in the Arduino framework libs,
// auto-links on include). The native test rig substitutes its own verify shim.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <sodium/crypto_sign_ed25519.h>
#endif

#include "components/catch_ota/dev_pubkey.h"

namespace catch_ota {

namespace {

// Module-static so the returned pointer outlives the call; the caller must
// consume it before the next verifySignedFirmware (serial on the loop task,
// no concurrent re-entry).
char g_channelOut[kLsigChannelLen + 1] = {0};

inline uint32_t readU32LE(const uint8_t* p) {
  return  static_cast<uint32_t>(p[0])
       | (static_cast<uint32_t>(p[1]) << 8)
       | (static_cast<uint32_t>(p[2]) << 16)
       | (static_cast<uint32_t>(p[3]) << 24);
}

// Block size for the streaming SHA-256 pass — one flash sector, matching
// esp_partition_read granularity. verifySignedFirmware heap-allocates this, NOT
// on the loopTask's 8 KB stack: stack-allocating it alongside the mbedtls
// SHA-256 + libsodium ed25519 scratch overflows the stack canary. Same ~4 KB
// resident either way.
constexpr size_t kStreamBlockBytes = 4096;

}  // namespace

bool verifySignedFirmware(FirmwareByteReader reader, size_t imageLen,
                          const char** outChannel, uint32_t* outVersion) {
  if (!reader) return false;
  if (imageLen < kLsigFooterLen) return false;

  // Footer first: its fields gate every later step, so bad magic / length
  // reject before any hash math.
  uint8_t footer[kLsigFooterLen] = {0};
  const size_t footerOffset = imageLen - kLsigFooterLen;
  const int footerRead = reader(footerOffset, kLsigFooterLen, footer);
  if (footerRead != static_cast<int>(kLsigFooterLen)) return false;

  if (std::memcmp(footer + kLsigMagicOffset, "LSIG", kLsigMagicLen) != 0) {
    return false;
  }

  // signedRegionLen lives in the footer. The bytes covered by the
  // SHA-256 (which the ed25519 signature in turn covers) are
  // `image[0 .. signedRegionLen)`. We sanity-check signedRegionLen
  // against the full image length so a malformed footer can't direct
  // verify() to walk out of bounds.
  const uint32_t signedRegionLen = readU32LE(footer + kLsigSignedLenOffset);
#if (defined(ARDUINO) || defined(ESP_PLATFORM)) && defined(LAMP_DEBUG)
  Serial.printf("[ota_sig] verify: imageLen=%u signedRegionLen=%u "
                "footerOffset=%u\n",
                (unsigned)imageLen, (unsigned)signedRegionLen,
                (unsigned)footerOffset);
  Serial.print("[ota_sig] verify: footer[0..16) =");
  for (size_t i = 0; i < 16; ++i) Serial.printf(" %02X", footer[i]);
  Serial.println();
  Serial.print("[ota_sig] verify: footer[80..96) =");
  for (size_t i = 80; i < 96; ++i) Serial.printf(" %02X", footer[i]);
  Serial.println();
#endif
  if (signedRegionLen == 0) return false;
  if (static_cast<size_t>(signedRegionLen) > imageLen - kLsigFooterLen) {
    return false;
  }

  // Stream SHA-256 over the signed region in kStreamBlockBytes chunks.
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  // mbedTLS API: 0 = SHA-256 (not SHA-224). Returns 0 on success.
  if (mbedtls_sha256_starts(&shaCtx, /*is224=*/0) != 0) {
    mbedtls_sha256_free(&shaCtx);
    return false;
  }

  // Heap, not stack — see kStreamBlockBytes. unique_ptr frees on every return.
  auto blockBuf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kStreamBlockBytes]);
  if (!blockBuf) {
    mbedtls_sha256_free(&shaCtx);
    return false;
  }

  size_t pos = 0;
  while (pos < signedRegionLen) {
    const size_t want = (signedRegionLen - pos) < kStreamBlockBytes
                            ? (signedRegionLen - pos)
                            : kStreamBlockBytes;
    const int got = reader(pos, want, blockBuf.get());
    if (got != static_cast<int>(want)) {
      mbedtls_sha256_free(&shaCtx);
      return false;
    }
    if (mbedtls_sha256_update(&shaCtx, blockBuf.get(), want) != 0) {
      mbedtls_sha256_free(&shaCtx);
      return false;
    }
    pos += want;
  }

  uint8_t digest[32];
  if (mbedtls_sha256_finish(&shaCtx, digest) != 0) {
    mbedtls_sha256_free(&shaCtx);
    return false;
  }
  mbedtls_sha256_free(&shaCtx);

#if (defined(ARDUINO) || defined(ESP_PLATFORM)) && defined(LAMP_DEBUG)
  Serial.print("[ota_sig] verify: computed sha256(signed_region) =");
  for (size_t i = 0; i < 32; ++i) Serial.printf(" %02X", digest[i]);
  Serial.println();
#endif

  // ed25519-verify the signature over the 32-byte SHA-256 digest (the signer
  // signs the digest, not the raw region) — constant-size message.
  // Signature lives at footer bytes [32..96).
  const uint8_t* signature = footer + kLsigSignatureOffset;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#ifdef LAMP_DEBUG
  Serial.print("[ota_sig] verify: footer signature =");
  for (size_t i = 0; i < 64; ++i) Serial.printf(" %02X", signature[i]);
  Serial.println();
  Serial.print("[ota_sig] verify: kDevPubkey =");
  for (size_t i = 0; i < 32; ++i) Serial.printf(" %02X", kDevPubkey[i]);
  Serial.println();
#endif
  // Ed25519 verify on production. crypto_sign_ed25519_verify_detached
  // returns 0 on success, -1 on failure.
  const int rc = crypto_sign_ed25519_verify_detached(
      signature, digest, sizeof(digest), kDevPubkey);
#ifdef LAMP_DEBUG
  Serial.printf("[ota_sig] verify: ed25519 rc=%d\n", rc);
#endif
  if (rc != 0) return false;
#else
  // Native test rig path: the test file provides its own
  // crypto_sign_ed25519_verify_detached shim with controllable return
  // value. We declare an extern hook here so the test rig can intercept.
  extern int test_crypto_sign_ed25519_verify_detached(
      const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
      const unsigned char* pk);
  const int rc = test_crypto_sign_ed25519_verify_detached(
      signature, digest, sizeof(digest), kDevPubkey);
  if (rc != 0) return false;
#endif

  // Populate outputs from the footer.
  if (outVersion) {
    *outVersion = readU32LE(footer + kLsigVersionOffset);
  }
  if (outChannel) {
    std::memcpy(g_channelOut, footer + kLsigChannelOffset, kLsigChannelLen);
    g_channelOut[kLsigChannelLen] = '\0';
    *outChannel = g_channelOut;
  }

  return true;
}

}  // namespace catch_ota

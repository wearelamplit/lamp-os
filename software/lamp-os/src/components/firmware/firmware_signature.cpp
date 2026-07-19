#include "firmware_signature.hpp"

#include <cstring>
#include <memory>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#endif

// mbedTLS for streaming SHA-256: init/starts/update/finish/free. The signed
// region streams in 4 KB blocks via the FirmwareByteReader, each block fed
// straight into mbedtls_sha256_update, so the full firmware image (1.4 MB)
// never has to buffer on the lamp's ~280 KB heap.
#include <mbedtls/sha256.h>

// libsodium is unconditionally available in framework-arduinoespressif32-libs;
// the header lives under `sodium/` in the SDK include path. No `lib_deps`
// change is required: the prebuilt lib auto-links when the header is
// included from a translation unit.
//
// The native test rig overrides the verify call (see test_firmware_signature/)
// so libsodium need not be vendored for the host toolchain.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <sodium/crypto_sign_ed25519.h>
#endif

#include "components/firmware/firmware_pubkey.h"

namespace lamp { namespace firmware {

#if !(defined(ARDUINO) || defined(ESP_PLATFORM))
// Namespace-scope so the anon-namespace helper below binds to the test's shim definition.
extern int test_crypto_sign_ed25519_verify_detached(
    const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
    const unsigned char* pk);
#endif

namespace {

// Static buffer for the channel string returned via outChannel. 16 channel
// bytes + 1 null terminator = 17 bytes. Module-static so callers can hold a
// pointer without worrying about lifetime, but must consume it before the
// next call from another thread. In practice, verify runs only from the
// Core 1 state machine, so concurrent re-entry is structurally impossible.
char g_channelOut[kLsigChannelLen + 1] = {0};

inline uint32_t readU32LE(const uint8_t* p) {
  return  static_cast<uint32_t>(p[0])
       | (static_cast<uint32_t>(p[1]) << 8)
       | (static_cast<uint32_t>(p[2]) << 16)
       | (static_cast<uint32_t>(p[3]) << 24);
}

// Block size for the streaming SHA-256 pass. Sized to one flash sector so
// the reader's call cadence matches the natural esp_partition_read granularity
// on the lamp's W25Q. Heap-allocated: the mbedtls SHA-256 context + libsodium
// ed25519 verify scratch together consume enough stack that a 4 KB stack buffer
// overflows the 8 KB loopTask stack canary. Heap keeps the same ~4 KB resident
// without touching the task stack.
constexpr size_t kStreamBlockBytes = 4096;

// ed25519-verify a 64-byte signature over a 32-byte digest against
// kFirmwarePubkey. Shared by the offer-time gate and the end-of-stream verify.
// Returns true only on rc == 0.
inline bool ed25519VerifyDigest(const uint8_t* signature, const uint8_t* digest) {
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  return crypto_sign_ed25519_verify_detached(
             signature, digest, 32, kFirmwarePubkey) == 0;
#else
  return test_crypto_sign_ed25519_verify_detached(
             signature, digest, 32, kFirmwarePubkey) == 0;
#endif
}

}  // namespace

bool verifyFirmwareDigestSignature(const uint8_t digest[32],
                                   const uint8_t signature[64]) {
  if (!digest || !signature) return false;
  return ed25519VerifyDigest(signature, digest);
}

bool verifySignedFirmware(FirmwareByteReader reader, size_t imageLen,
                          const char** outChannel, uint32_t* outVersion,
                          uint8_t* outDigest) {
  if (!reader) return false;
  if (imageLen < kLsigFooterLen) return false;

  // Step 1: read the 96-byte LSIG footer up-front. The footer fields
  // (magic, channel, version, signedRegionLen, signature) drive every
  // subsequent decision; reading them first short-circuits
  // bad-magic / bad-length rejections before doing any hash math.
  uint8_t footer[kLsigFooterLen] = {0};
  const size_t footerOffset = imageLen - kLsigFooterLen;
  const int footerRead = reader(footerOffset, kLsigFooterLen, footer);
  if (footerRead != static_cast<int>(kLsigFooterLen)) return false;

  // Magic check short-circuits the cheap rejections before the
  // hash + signature math.
  if (std::memcmp(footer + kLsigMagicOffset, "LSIG", kLsigMagicLen) != 0) {
    return false;
  }

  // signedRegionLen lives in the footer. The bytes covered by the
  // SHA-256 (which the ed25519 signature in turn covers) are
  // `image[0 .. signedRegionLen)`. Sanity-check signedRegionLen
  // against the full image length so a malformed footer can't direct
  // verify() to walk out of bounds.
  const uint32_t signedRegionLen = readU32LE(footer + kLsigSignedLenOffset);
#if (defined(ARDUINO) || defined(ESP_PLATFORM)) && defined(LAMP_DEBUG)
  Serial.printf("[fw_sig] verify: imageLen=%u signedRegionLen=%u "
                "footerOffset=%u\n",
                (unsigned)imageLen, (unsigned)signedRegionLen,
                (unsigned)footerOffset);
  // Hex-dump the first 16 bytes of the footer (magic+channel start) and
  // the last 16 bytes (last quarter of the ed25519 signature). Tells us
  // if the footer itself is intact.
  Serial.print("[fw_sig] verify: footer[0..16) =");
  for (size_t i = 0; i < 16; ++i) Serial.printf(" %02X", footer[i]);
  Serial.println();
  Serial.print("[fw_sig] verify: footer[80..96) =");
  for (size_t i = 80; i < 96; ++i) Serial.printf(" %02X", footer[i]);
  Serial.println();
#endif
  if (signedRegionLen == 0) return false;
  if (static_cast<size_t>(signedRegionLen) > imageLen - kLsigFooterLen) {
    return false;
  }

  // Step 2: stream-compute SHA-256 over the signed region, reading
  // kStreamBlockBytes (4 KB) chunks from the reader, feeding each
  // chunk straight into mbedtls_sha256_update. Total RAM footprint
  // for the verify pass: ~200 B of SHA context + 4 KB stack block
  // buffer + 96 B footer + 32 B digest. The full ~1.4 MB firmware is
  // never resident in heap.
  mbedtls_sha256_context shaCtx;
  mbedtls_sha256_init(&shaCtx);
  // mbedTLS API: 0 = SHA-256 (not SHA-224). Returns 0 on success.
  if (mbedtls_sha256_starts(&shaCtx, /*is224=*/0) != 0) {
    mbedtls_sha256_free(&shaCtx);
    return false;
  }

  // Heap-allocated read buffer; see kStreamBlockBytes comment for why
  // this can't live on the loopTask stack. std::unique_ptr<uint8_t[]>
  // gives RAII cleanup on every return path below (including the
  // mbedtls_sha256_update failure path) without manual free() calls.
  auto blockBuf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kStreamBlockBytes]);
  if (!blockBuf) {
    // Out-of-heap during verify: log nothing here (this TU has no
    // logger dep) and bubble up a verification failure. The receiver
    // path treats `false` as a clean reject and the firmware update
    // is abandoned.
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
  // Dump the computed digest. Compare to the signer's
  // `[sign] sha256(signed_region)=...` line emitted at build time.
  // - DIGESTS MATCH but verify still fails  -> signature in footer is
  //   wrong (or pubkey on the lamp doesn't match the signing key).
  // - DIGESTS DIFFER                        -> bytes in flash don't
  //   match what the signer hashed; corruption is in the write path
  //   somewhere in [0, signedRegionLen).
  Serial.print("[fw_sig] verify: computed sha256(signed_region) =");
  for (size_t i = 0; i < 32; ++i) Serial.printf(" %02X", digest[i]);
  Serial.println();
#endif

  // Step 3: ed25519-verify the signature against the SHA-256 digest.
  // The signing tool (scripts/sign_firmware.py) signs SHA256(signed
  // region), so verify_detached's message argument is the 32-byte
  // digest, fully constant-size, regardless of the firmware image size.
  //
  // Signature lives at bytes [32..96) of the footer.
  const uint8_t* signature = footer + kLsigSignatureOffset;

#if (defined(ARDUINO) || defined(ESP_PLATFORM)) && defined(LAMP_DEBUG)
  // Dump the signature + pubkey so we can compare to the at-rest binary
  // and the matching key on disk. If kFirmwarePubkey is zeros here, the
  // constexpr array isn't surviving the link.
  Serial.print("[fw_sig] verify: footer signature =");
  for (size_t i = 0; i < 64; ++i) Serial.printf(" %02X", signature[i]);
  Serial.println();
  Serial.print("[fw_sig] verify: kFirmwarePubkey =");
  for (size_t i = 0; i < 32; ++i) Serial.printf(" %02X", kFirmwarePubkey[i]);
  Serial.println();
#endif
  if (!ed25519VerifyDigest(signature, digest)) return false;

  // Populate outputs from the footer.
  if (outDigest) std::memcpy(outDigest, digest, 32);
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

}}  // namespace lamp::firmware

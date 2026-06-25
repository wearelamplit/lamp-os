#include "firmware_signature.hpp"

#include <cstring>
#include <memory>

// mbedTLS for streaming SHA-256: init/starts/update/finish/free. We stream
// the signed region in 4 KB blocks via the FirmwareByteReader and feed
// each block straight into mbedtls_sha256_update so we never have to
// buffer the full firmware image (1.4 MB) on the lamp's ~280 KB heap.
#include <mbedtls/sha256.h>

// libsodium is unconditionally available in framework-arduinoespressif32-libs;
// the header lives under `sodium/` in the SDK include path. No `lib_deps`
// change is required — the prebuilt lib auto-links when the header is
// included from a translation unit.
//
// Native test rig overrides the verify call (see test_firmware_signature/)
// so we don't have to vendor libsodium for the host toolchain.
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <sodium/crypto_sign_ed25519.h>
#endif

#include "components/firmware/firmware_pubkey.h"

namespace lamp { namespace firmware {

namespace {

// Static buffer for the channel string returned via outChannel. 8 footer
// bytes + 1 null terminator = 9 bytes. This is intentionally module-static
// so callers can hold a pointer without worrying about lifetime — but they
// MUST consume the value before another thread re-enters verifySignedFirmware.
// In the lamp's actual flow, verify is called from the Core 1 state machine
// only, so concurrent re-entry is structurally impossible.
char g_channelOut[kLsigChannelLen + 1] = {0};

inline uint32_t readU32LE(const uint8_t* p) {
  return  static_cast<uint32_t>(p[0])
       | (static_cast<uint32_t>(p[1]) << 8)
       | (static_cast<uint32_t>(p[2]) << 16)
       | (static_cast<uint32_t>(p[3]) << 24);
}

// Block size for the streaming SHA-256 pass. Sized to one flash sector
// so the reader's call cadence matches the natural esp_partition_read
// granularity on the lamp's W25Q. The buffer is heap-allocated inside
// verifySignedFirmware — keeping it on the Arduino loopTask stack
// (8 KB total) blew the stack canary in combination with the mbedtls
// SHA-256 context + libsodium ed25519 verify scratch (~several KB) +
// regular call-frame overhead. Hardware capture showed a "Stack canary
// watchpoint triggered (loopTask)" Guru Meditation immediately after
// the verify path opened this buffer. Heap path keeps total resident
// RAM identical (~4 KB) without consuming the verify task's stack.
constexpr size_t kStreamBlockBytes = 4096;

}  // namespace

bool verifySignedFirmware(FirmwareByteReader reader, size_t imageLen,
                          const char** outChannel, uint32_t* outVersion) {
  if (!reader) return false;
  if (imageLen < kLsigFooterLen) return false;

  // Step 1: read the 96-byte LSIG footer up-front. The footer fields
  // (magic, channel, version, signedRegionLen, signature) drive every
  // subsequent decision; reading them first means we can short-circuit
  // bad-magic / bad-length rejections before doing any hash math.
  uint8_t footer[kLsigFooterLen] = {0};
  const size_t footerOffset = imageLen - kLsigFooterLen;
  const int footerRead = reader(footerOffset, kLsigFooterLen, footer);
  if (footerRead != static_cast<int>(kLsigFooterLen)) return false;

  // Magic check — short-circuits the cheap rejections before we touch
  // the hash + signature math.
  if (std::memcmp(footer + kLsigMagicOffset, "LSIG", kLsigMagicLen) != 0) {
    return false;
  }

  // signedRegionLen lives in the footer. The bytes covered by the
  // SHA-256 (which the ed25519 signature in turn covers) are
  // `image[0 .. signedRegionLen)`. We sanity-check signedRegionLen
  // against the full image length so a malformed footer can't direct
  // verify() to walk out of bounds.
  const uint32_t signedRegionLen = readU32LE(footer + kLsigSignedLenOffset);
  if (signedRegionLen == 0) return false;
  if (static_cast<size_t>(signedRegionLen) > imageLen - kLsigFooterLen) {
    return false;
  }

  // Step 2: stream-compute SHA-256 over the signed region. We read in
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

  // Heap-allocated read buffer — see kStreamBlockBytes comment for why
  // this can't live on the loopTask stack. std::unique_ptr<uint8_t[]>
  // gives us RAII cleanup on every return path below (including the
  // mbedtls_sha256_update failure path) without dancing around manual
  // free() calls.
  auto blockBuf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kStreamBlockBytes]);
  if (!blockBuf) {
    // Out-of-heap during verify — log nothing here (this TU has no
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

  // Step 3: ed25519-verify the signature against the SHA-256 digest.
  // The signing tool (scripts/sign_firmware.py) signs SHA256(signed
  // region) — not the raw region — so verify_detached's message
  // argument is the 32-byte digest, fully constant-size, regardless
  // of the firmware image size.
  //
  // Signature lives at bytes [32..96) of the footer.
  const uint8_t* signature = footer + kLsigSignatureOffset;

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  // Ed25519 verify on production. crypto_sign_ed25519_verify_detached
  // returns 0 on success, -1 on failure.
  const int rc = crypto_sign_ed25519_verify_detached(
      signature, digest, sizeof(digest), kFirmwarePubkey);
  if (rc != 0) return false;
#else
  // Native test rig path: the test file provides its own
  // crypto_sign_ed25519_verify_detached shim with controllable return
  // value. We declare an extern hook here so the test rig can intercept.
  extern int test_crypto_sign_ed25519_verify_detached(
      const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
      const unsigned char* pk);
  const int rc = test_crypto_sign_ed25519_verify_detached(
      signature, digest, sizeof(digest), kFirmwarePubkey);
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

}}  // namespace lamp::firmware

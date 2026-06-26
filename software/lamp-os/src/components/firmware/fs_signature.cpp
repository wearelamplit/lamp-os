#include "fs_signature.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

// Same backend split as firmware_signature.cpp: mbedTLS for the streaming
// SHA-256 (available on both host test + device), libsodium for the ed25519
// verify on device, and an extern test shim on the native host (we don't
// vendor libsodium for the host toolchain).
#include <mbedtls/sha256.h>
#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <sodium/crypto_sign_ed25519.h>
#endif

#include "components/firmware/firmware_pubkey.h"

namespace lamp { namespace firmware {

namespace {

constexpr size_t kStreamBlockBytes = 4096;

inline void putU32LE(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

inline uint32_t readU32LE(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool computeFsManifestDigest(std::vector<FsManifestFile>& files, uint8_t out[32]) {
  // Bytewise name sort — std::string::operator< compares as unsigned bytes,
  // matching sign_fs.py's sort on ASCII names.
  std::sort(files.begin(), files.end(),
            [](const FsManifestFile& a, const FsManifestFile& b) {
              return a.name < b.name;
            });

  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  if (mbedtls_sha256_starts(&ctx, /*is224=*/0) != 0) {
    mbedtls_sha256_free(&ctx);
    return false;
  }

  auto buf = std::unique_ptr<uint8_t[]>(new (std::nothrow) uint8_t[kStreamBlockBytes]);
  if (!buf) {
    mbedtls_sha256_free(&ctx);
    return false;
  }

  uint8_t hdr[4];
  for (auto& f : files) {
    putU32LE(hdr, static_cast<uint32_t>(f.name.size()));
    if (mbedtls_sha256_update(&ctx, hdr, 4) != 0) goto fail;
    if (!f.name.empty() &&
        mbedtls_sha256_update(
            &ctx, reinterpret_cast<const uint8_t*>(f.name.data()),
            f.name.size()) != 0) {
      goto fail;
    }
    putU32LE(hdr, static_cast<uint32_t>(f.contentLen));
    if (mbedtls_sha256_update(&ctx, hdr, 4) != 0) goto fail;

    size_t pos = 0;
    while (pos < f.contentLen) {
      const size_t want =
          (f.contentLen - pos) < kStreamBlockBytes ? (f.contentLen - pos)
                                                    : kStreamBlockBytes;
      const int got = f.read ? f.read(pos, want, buf.get()) : -1;
      if (got != static_cast<int>(want)) goto fail;
      if (mbedtls_sha256_update(&ctx, buf.get(), want) != 0) goto fail;
      pos += want;
    }
  }

  if (mbedtls_sha256_finish(&ctx, out) != 0) goto fail;
  mbedtls_sha256_free(&ctx);
  return true;

fail:
  mbedtls_sha256_free(&ctx);
  return false;
}

bool verifyFsManifest(std::vector<FsManifestFile>& files, const uint8_t* fwLsig,
                      size_t fwLsigLen, uint32_t* outVersion) {
  if (!fwLsig || fwLsigLen != kFsSigLen) return false;
  if (std::memcmp(fwLsig, kFsSigMagic, kFsSigMagicLen) != 0) return false;

  uint8_t digest[32];
  if (!computeFsManifestDigest(files, digest)) return false;

  const uint8_t* signature = fwLsig + kFsSigSignatureOffset;
#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (crypto_sign_ed25519_verify_detached(signature, digest, sizeof(digest),
                                          kFirmwarePubkey) != 0) {
    return false;
  }
#else
  // Native test rig provides this shim (see firmware_signature.cpp for the
  // same pattern) so the host build doesn't need libsodium.
  extern int test_crypto_sign_ed25519_verify_detached(
      const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
      const unsigned char* pk);
  if (test_crypto_sign_ed25519_verify_detached(signature, digest, sizeof(digest),
                                               kFirmwarePubkey) != 0) {
    return false;
  }
#endif

  if (outVersion) *outVersion = readU32LE(fwLsig + kFsSigVersionOffset);
  return true;
}

}}  // namespace lamp::firmware

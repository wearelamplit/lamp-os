#pragma once

// Ed25519 signature verification for OTA-received firmware images.
//
// Backend: libsodium's `crypto_sign_ed25519_verify_detached`. We use
// libsodium (not mbedTLS) for the ed25519 verify because pioarduino's
// prebuilt mbedTLS does NOT ship with the Everest Ed25519 module
// compiled in — see the wisp-OTA reconciliation doc's "Ed25519 backend"
// section. libsodium is unconditionally present in
// framework-arduinoespressif32-libs and links cleanly; no `lib_deps`
// change required. For the SHA-256 streaming hash we use mbedTLS
// (which is available + has a clean init/update/finish API).
//
// Streaming verify:
//
// The lamp's free heap budget (~280 KB) can't fit a 1.4 MB firmware
// image for verification, so we cannot buffer the signed region and
// call PureEdDSA `verify_detached(message)` directly. Instead, the
// signing tool signs the 32-byte SHA-256 digest of the signed region
// (not the raw region), and this verify path:
//
//   1. Reads the 96-byte LSIG footer up-front via a streaming reader.
//   2. Validates the LSIG magic + signedRegionLen against `imageLen`.
//   3. Streams the signed region through mbedtls SHA-256 in 4 KB
//      blocks, producing a 32-byte digest.
//   4. Calls `crypto_sign_ed25519_verify_detached(signature, digest,
//      32, kFirmwarePubkey)` — i.e. the message we verify against is
//      the 32-byte SHA-256 of the signed region. This is functionally
//      equivalent to a hash-then-sign scheme; the at-rest LSIG layout
//      is unchanged, only the producer signs the digest instead of
//      the raw region.
//
// LSIG footer (96 bytes total, sits at the tail of the signed image):
//   bytes [0..4)   "LSIG" magic
//   bytes [4..20)  channel — zero-padded ASCII, carries `{type}-{channel}`
//                  (e.g. "standard-stable\0", "snafu-beta\0\0\0\0\0\0")
//   bytes [20..24) firmware version (4 LE — packed semver)
//   bytes [24..28) signedRegionLen (4 LE — bytes covered by SHA-256)
//   bytes [28..32) reserved (4 zero bytes)
//   bytes [32..96) ed25519 signature (64 bytes — over SHA-256 digest)
//
// The signature offset (32) is pinned — widening any future field comes
// out of the 4 reserved bytes or forces another protocol version bump.
//
// The signature covers `SHA256(image[0 .. signedRegionLen))`. The
// public key against which we verify is embedded as
// kFirmwarePubkey[32] in components/firmware/firmware_pubkey.h (generated
// by scripts/gen_firmware_keys.py).
//
// Verification semantics:
//   - Returns true ONLY if the LSIG magic matches, the embedded
//     signedRegionLen is within bounds, and the ed25519 signature
//     verifies against kFirmwarePubkey over the SHA-256 digest of the
//     streamed signed region.
//   - On success, `*outChannel` (if non-null) points at a static
//     null-terminated string (channel bytes from the footer, copied
//     into a module-static buffer); `*outVersion` (if non-null) is
//     the packed semver.
//   - On any failure (bad magic, bad signature, malformed length,
//     missing pubkey, etc.) returns false and leaves out-params
//     unchanged.
//
// The function does NOT enforce channel-string matching against
// FIRMWARE_CHANNEL_STR — that's the caller's job (firmware_receiver
// silently drops cross-channel offers BEFORE accepting; this verify
// is the cryptographic gate, not the routing gate).

#include <cstddef>
#include <cstdint>
#include <functional>

namespace lamp { namespace firmware {

// LSIG footer constants. Shared across signature verification + the
// build-side signing tool. The implementer of gen_firmware_keys.py +
// embed_firmware.py reads these as the byte layout contract.
constexpr size_t kLsigFooterLen        = 96;
constexpr size_t kLsigMagicOffset      = 0;
constexpr size_t kLsigMagicLen         = 4;
constexpr size_t kLsigChannelOffset    = 4;
constexpr size_t kLsigChannelLen       = 16;
constexpr size_t kLsigVersionOffset    = 20;
constexpr size_t kLsigSignedLenOffset  = 24;
constexpr size_t kLsigReservedOffset   = 28;
constexpr size_t kLsigReservedLen      = 4;
constexpr size_t kLsigSignatureOffset  = 32;
constexpr size_t kLsigSignatureLen     = 64;

static_assert(kLsigMagicLen + kLsigChannelLen + 4 + 4 + kLsigReservedLen +
                  kLsigSignatureLen == kLsigFooterLen,
              "LSIG footer field sizes must sum to 96");
static_assert(kLsigSignatureOffset + kLsigSignatureLen == kLsigFooterLen,
              "LSIG signature must end at footer end");

// Streaming byte reader. The verify implementation calls this multiple
// times with ascending `offset`s during the signed-region hash pass;
// it also reads the footer up-front (one call near `imageLen - 96`).
// The implementation reads up to `wantBytes` bytes into `out` and
// returns the number actually read. Anything other than `wantBytes`
// (e.g. a short read or a negative error) fails verification.
//
// In production, FirmwareReceiver constructs a reader that wraps
// esp_partition_read against the inactive OTA partition (see
// firmware_receiver.cpp::verifyAndApply, which feeds a 4 KB stack
// buffer per call). In native tests, the reader is backed by a
// std::vector<uint8_t> fixture.
using FirmwareByteReader =
    std::function<int(size_t offset, size_t wantBytes, uint8_t* out)>;

// Verify a signed firmware image via a streaming reader.
//   reader     — supplies bytes [0 .. imageLen). Footer is read first
//                from offset (imageLen - 96), then the signed region
//                is streamed in ascending blocks.
//   imageLen   — total signed image length including the 96-byte footer.
//   outChannel — optional. On success, populated with a pointer to a
//                static null-terminated channel buffer. Caller must
//                consume before the next call from another thread; the
//                lamp's verify is serial under the Core 1 state machine.
//   outVersion — optional. On success, populated with the packed-semver
//                version field from the footer.
//
// Returns true if all checks pass; false on any failure. Failure is
// silent (no logging from this function — caller decides what to log).
bool verifySignedFirmware(FirmwareByteReader reader, size_t imageLen,
                          const char** outChannel, uint32_t* outVersion);

}}  // namespace lamp::firmware

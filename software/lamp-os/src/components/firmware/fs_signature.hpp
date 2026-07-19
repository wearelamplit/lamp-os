#pragma once

// Ed25519 signature for an OTA-delivered SPIFFS filesystem image.
//
// Why a file, not a footer: the spiffs partition is flash-end-aligned and
// mkspiffs fills it, so there's no room to append an LSIG footer the way
// firmware does. The signature instead rides INSIDE the image as a file,
// `fw.lsig`, which keeps the image self-contained for mesh redistribution
// (a USB-flashed seed carries its own signature).
//
// What is signed: NOT the packed image (mkspiffs output is not byte-stable
// and a mounted partition carries per-lamp wear/metadata). The signed value is
// a canonical digest of the LOGICAL file contents (every data file except
// `fw.lsig`), so host (sign_fs.py) and device recompute the identical value:
//
//   sort files by name, bytewise ascending; then
//   SHA-256 over, per file:  u32LE(nameLen) ∥ name ∥ u32LE(contentLen) ∥ content
//
// Canonical name = the filename with any leading '/' stripped, ASCII only.
// sign_fs.py enforces ASCII + flat files + name ≤ 31 (mkspiffs name cap);
// the device strips the leading '/' that Arduino's File::name() prepends.
// The ed25519 signature covers the 32-byte digest (same hash-then-sign shape
// as firmware_signature.hpp).
//
// fw.lsig layout (72 bytes):
//   [0..4)   magic "LFSG"
//   [4..8)   firmware version, packed (major<<16)|(minor<<8)|patch, LE
//   [8..72)  ed25519 signature over the 32-byte manifest digest
//
// This digest doubles as the FS image's identity on the mesh: the OFFER
// fingerprint and HELLO_TLV_FS_STATE carry a prefix of it (a raw-partition
// SHA would differ per lamp → infinite re-offer).

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace lamp { namespace firmware {

constexpr size_t kFsSigLen             = 72;
constexpr size_t kFsSigMagicLen        = 4;
constexpr size_t kFsSigVersionOffset   = 4;
constexpr size_t kFsSigSignatureOffset = 8;
constexpr size_t kFsSigSignatureLen    = 64;
constexpr char   kFsSigMagic[]         = "LFSG";  // 4 bytes, no NUL counted
constexpr char   kFsSigName[]          = "fw.lsig";  // excluded from the manifest

static_assert(kFsSigSignatureOffset + kFsSigSignatureLen == kFsSigLen,
              "fw.lsig signature must end at fw.lsig end");

// One file in the FS manifest. `name` is canonical (leading '/' stripped,
// ASCII). `read` streams the file body offset-wise (like FirmwareByteReader)
// so a whole file is never buffered; it must return `want` or verification
// fails. Caller supplies files ALREADY excluding fw.lsig.
struct FsManifestFile {
  std::string name;
  size_t contentLen = 0;
  std::function<int(size_t offset, size_t want, uint8_t* out)> read;
};

// Canonical manifest digest. Sorts `files` by name (bytewise) in place, then
// hashes per the framing above. Returns false on a read error.
bool computeFsManifestDigest(std::vector<FsManifestFile>& files, uint8_t out[32]);

// Verify a packed FS image: recompute the manifest digest over `files`
// (caller excludes fw.lsig), require `fwLsig` to be kFsSigLen bytes with the
// "LFSG" magic, ed25519-verify the signature over the digest against
// kFirmwarePubkey, and output the footer version. False on any failure;
// out-params unchanged on failure.
bool verifyFsManifest(std::vector<FsManifestFile>& files, const uint8_t* fwLsig,
                      size_t fwLsigLen, uint32_t* outVersion);

}}  // namespace lamp::firmware

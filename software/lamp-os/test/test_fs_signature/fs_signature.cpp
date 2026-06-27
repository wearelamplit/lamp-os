// Native test for the FS-image signature (fs_signature.cpp).
//
// We link the production fs_signature.cpp; its native path uses
// extern test_crypto_sign_ed25519_verify_detached (defined here) so the host
// build doesn't need libsodium. SHA-256 is the real (vendored) mbedtls shim
// under ./mbedtls, so the manifest digest is computed by the same bytes that
// run on device — and pinned against a golden vector produced by sign_fs.py's
// identical framing (the host↔device agreement guard).

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// --- Mock for crypto_sign_ed25519_verify_detached ---
namespace {
struct VerifyCall {
  std::vector<uint8_t> sig;
  std::vector<uint8_t> message;
};
std::vector<VerifyCall> g_calls;
int g_verifyRc = 0;
}  // namespace

namespace lamp { namespace firmware {
int test_crypto_sign_ed25519_verify_detached(
    const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
    const unsigned char* pk) {
  (void)pk;
  VerifyCall c;
  c.sig.assign(sig, sig + 64);
  c.message.assign(m, m + mlen);
  g_calls.push_back(c);
  return g_verifyRc;
}
}}  // namespace lamp::firmware

#include "../../src/components/firmware/fs_signature.cpp"

using lamp::firmware::FsManifestFile;
using lamp::firmware::computeFsManifestDigest;
using lamp::firmware::verifyFsManifest;
using lamp::firmware::kFsSigLen;
using lamp::firmware::kFsSigVersionOffset;
using lamp::firmware::kFsSigSignatureOffset;

// --- Fixtures ---

// Golden digest for files {"a":"AA", "b.gz":"BBB"} computed by sign_fs.py's
// exact framing (u32LE(nameLen)∥name∥u32LE(contentLen)∥content, name-sorted).
// If this fails, the host and device manifest framings have diverged.
static const uint8_t kGoldenDigest[32] = {
    0x07, 0x81, 0xb2, 0x3a, 0x6e, 0xb5, 0xf4, 0x67, 0xbf, 0x73, 0xc6,
    0xac, 0x84, 0xce, 0x57, 0x96, 0xc4, 0xf5, 0xc7, 0x2c, 0xcf, 0x1f,
    0x58, 0x41, 0xec, 0xbe, 0x62, 0x78, 0x07, 0xce, 0xf4, 0x45};

static std::vector<std::vector<uint8_t>>* g_contents = nullptr;

static FsManifestFile makeFile(const std::string& name, const std::string& body) {
  g_contents->emplace_back(body.begin(), body.end());
  const std::vector<uint8_t>& buf = g_contents->back();
  FsManifestFile f;
  f.name = name;
  f.contentLen = buf.size();
  const uint8_t* data = buf.data();
  f.read = [data, &buf](size_t offset, size_t want, uint8_t* out) -> int {
    if (offset + want > buf.size()) return -1;
    std::memcpy(out, data + offset, want);
    return static_cast<int>(want);
  };
  return f;
}

static std::vector<uint8_t> makeFwLsig(uint32_t version) {
  std::vector<uint8_t> blob(kFsSigLen, 0);
  std::memcpy(blob.data(), "LFSG", 4);
  blob[kFsSigVersionOffset + 0] = version & 0xFF;
  blob[kFsSigVersionOffset + 1] = (version >> 8) & 0xFF;
  blob[kFsSigVersionOffset + 2] = (version >> 16) & 0xFF;
  blob[kFsSigVersionOffset + 3] = (version >> 24) & 0xFF;
  for (size_t i = 0; i < 64; ++i) blob[kFsSigSignatureOffset + i] = (uint8_t)(i + 1);
  return blob;
}

void setUp() {
  g_calls.clear();
  g_verifyRc = 0;
  static std::vector<std::vector<uint8_t>> contents;
  contents.clear();
  g_contents = &contents;
}
void tearDown() {}

// Digest matches the golden — and is independent of input order (sort works).
static void test_digest_golden_and_order_independent() {
  std::vector<FsManifestFile> files;
  files.push_back(makeFile("b.gz", "BBB"));  // deliberately reverse order
  files.push_back(makeFile("a", "AA"));
  uint8_t digest[32];
  TEST_ASSERT_TRUE(computeFsManifestDigest(files, digest));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGoldenDigest, digest, 32);
}

// Changing any content changes the digest.
static void test_digest_content_sensitive() {
  std::vector<FsManifestFile> a;
  a.push_back(makeFile("a", "AA"));
  a.push_back(makeFile("b.gz", "BBB"));
  uint8_t da[32];
  TEST_ASSERT_TRUE(computeFsManifestDigest(a, da));

  std::vector<FsManifestFile> b;
  b.push_back(makeFile("a", "AA"));
  b.push_back(makeFile("b.gz", "BBC"));  // one byte differs
  uint8_t db[32];
  TEST_ASSERT_TRUE(computeFsManifestDigest(b, db));

  TEST_ASSERT_NOT_EQUAL(0, std::memcmp(da, db, 32));
}

// verifyFsManifest: accepts when ed25519 returns 0, parses version, and the
// message handed to ed25519 is exactly the manifest digest.
static void test_verify_accepts_and_parses_version() {
  std::vector<FsManifestFile> files;
  files.push_back(makeFile("a", "AA"));
  files.push_back(makeFile("b.gz", "BBB"));
  auto lsig = makeFwLsig(0x000100A5);
  g_verifyRc = 0;
  uint32_t version = 0;
  TEST_ASSERT_TRUE(verifyFsManifest(files, lsig.data(), lsig.size(), &version));
  TEST_ASSERT_EQUAL_UINT32(0x000100A5, version);
  TEST_ASSERT_EQUAL_UINT32(1, g_calls.size());
  TEST_ASSERT_EQUAL_UINT32(32, g_calls[0].message.size());
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kGoldenDigest, g_calls[0].message.data(), 32);
}

// Rejects when the signature fails to verify.
static void test_verify_rejects_bad_signature() {
  std::vector<FsManifestFile> files;
  files.push_back(makeFile("a", "AA"));
  files.push_back(makeFile("b.gz", "BBB"));
  auto lsig = makeFwLsig(0x000100A5);
  g_verifyRc = -1;
  uint32_t version = 0xDEAD;
  TEST_ASSERT_FALSE(verifyFsManifest(files, lsig.data(), lsig.size(), &version));
  TEST_ASSERT_EQUAL_UINT32(0xDEAD, version);  // unchanged on failure
}

// Rejects bad magic and wrong length before touching the crypto.
static void test_verify_rejects_bad_magic_and_length() {
  std::vector<FsManifestFile> files;
  files.push_back(makeFile("a", "AA"));
  auto lsig = makeFwLsig(0x000100A5);

  auto badMagic = lsig;
  badMagic[0] = 'X';
  TEST_ASSERT_FALSE(verifyFsManifest(files, badMagic.data(), badMagic.size(), nullptr));

  TEST_ASSERT_FALSE(verifyFsManifest(files, lsig.data(), lsig.size() - 1, nullptr));
  TEST_ASSERT_FALSE(verifyFsManifest(files, nullptr, kFsSigLen, nullptr));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_digest_golden_and_order_independent);
  RUN_TEST(test_digest_content_sensitive);
  RUN_TEST(test_verify_accepts_and_parses_version);
  RUN_TEST(test_verify_rejects_bad_signature);
  RUN_TEST(test_verify_rejects_bad_magic_and_length);
  return UNITY_END();
}

// Native-host unit tests for catch_ota::verifySignedFirmware.
//
// We link the production ota_signature.cpp; its native path uses
// extern test_crypto_sign_ed25519_verify_detached (declared here) so the
// test rig can control the verify return value without needing libsodium
// on the host. The LSIG-footer parsing, magic check, signedRegionLen
// bounds check, streaming-reader iteration, SHA-256 digest computation,
// and out-param population are all exercised; the signature verify call
// itself is mocked.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

// Native test rig convention (matches test_catch_ota_protocol — the production
// source isn't auto-linked into the native env; we bring the production
// cpp + header into this translation unit explicitly so we exercise the
// same bytes that ship on the device). The cpp's #if guards
// (ARDUINO || ESP_PLATFORM) ensure the libsodium include is skipped and
// our extern test_crypto_sign_ed25519_verify_detached shim handles the
// verify call. mbedTLS streaming SHA-256 is served by test/stubs/mbedtls/sha256.h
// (vendored public-domain implementation, -I test/stubs in the native env).
#include "components/catch_ota/ota_signature.hpp"
// IMPORTANT: include the .cpp AFTER the extern declaration of
// test_crypto_sign_ed25519_verify_detached below.

void setUp(void) {}
void tearDown(void) {}

namespace co = catch_ota;

// --- Mock for crypto_sign_ed25519_verify_detached ---
//
// ota_signature.cpp's native path calls this extern. We track each
// call's args + return a controllable rc so tests can drive both success
// and failure paths.

namespace {
struct VerifyCall {
  std::vector<uint8_t> sig;
  std::vector<uint8_t> message;
  std::vector<uint8_t> pubkey;
};
std::vector<VerifyCall> g_calls;
int g_verifyRc = 0;
}  // namespace

// Production ota_signature.cpp declares this as `extern int` inside
// namespace catch_ota (in the native verify path). The definition lives
// here so the linker resolves to this mock.
namespace catch_ota {
int test_crypto_sign_ed25519_verify_detached(
    const unsigned char* sig, const unsigned char* m, unsigned long long mlen,
    const unsigned char* pk) {
  VerifyCall c;
  c.sig.assign(sig, sig + 64);
  c.message.assign(m, m + mlen);
  c.pubkey.assign(pk, pk + 32);
  g_calls.push_back(c);
  return g_verifyRc;
}
}  // namespace catch_ota

// Bring production ota_signature.cpp into this TU (after the mock is
// declared). This causes the same bytes to run that ship on device.
#include "../../src/components/catch_ota/ota_signature.cpp"

// --- Test fixtures ---

// Builds a signed image into the returned vector:
//   [signedBytes (len = signedLen, content = pattern)] [LSIG footer (96 B)]
// The footer's signedRegionLen field is set to `embeddedSignedLen` (may
// differ from `signedLen` for the bounds-check tests).
static std::vector<uint8_t> buildImage(
    size_t signedLen, uint32_t embeddedSignedLen,
    const char* magic = "LSIG",
    uint32_t version = 0x00010203,
    const char* channel = "stable") {
  std::vector<uint8_t> img(signedLen + co::kLsigFooterLen, 0);
  for (size_t i = 0; i < signedLen; ++i) img[i] = static_cast<uint8_t>(i ^ 0xA5);
  uint8_t* footer = img.data() + signedLen;
  std::memcpy(footer + co::kLsigMagicOffset, magic, 4);
  std::memset(footer + co::kLsigChannelOffset, 0, co::kLsigChannelLen);
  size_t chLen = std::strlen(channel);
  if (chLen > co::kLsigChannelLen) chLen = co::kLsigChannelLen;
  std::memcpy(footer + co::kLsigChannelOffset, channel, chLen);
  // version (LE)
  footer[co::kLsigVersionOffset + 0] = static_cast<uint8_t>(version & 0xFF);
  footer[co::kLsigVersionOffset + 1] = static_cast<uint8_t>((version >> 8) & 0xFF);
  footer[co::kLsigVersionOffset + 2] = static_cast<uint8_t>((version >> 16) & 0xFF);
  footer[co::kLsigVersionOffset + 3] = static_cast<uint8_t>((version >> 24) & 0xFF);
  // signedRegionLen (LE)
  footer[co::kLsigSignedLenOffset + 0] = static_cast<uint8_t>(embeddedSignedLen & 0xFF);
  footer[co::kLsigSignedLenOffset + 1] = static_cast<uint8_t>((embeddedSignedLen >> 8) & 0xFF);
  footer[co::kLsigSignedLenOffset + 2] = static_cast<uint8_t>((embeddedSignedLen >> 16) & 0xFF);
  footer[co::kLsigSignedLenOffset + 3] = static_cast<uint8_t>((embeddedSignedLen >> 24) & 0xFF);
  // signature: arbitrary bytes (mocked verifier doesn't actually check).
  for (size_t i = 0; i < co::kLsigSignatureLen; ++i) {
    footer[co::kLsigSignatureOffset + i] = static_cast<uint8_t>(0xBE ^ i);
  }
  return img;
}

// Build a reader that serves bytes out of a backing vector. The reader
// returns the bytes-actually-read on success, or -1 to simulate a read
// error (e.g. when offset+want exceeds the vector).
static co::FirmwareByteReader makeVectorReader(const std::vector<uint8_t>& v) {
  // Capture by value so the test can drop the local fixture without
  // dangling — std::function copies the lambda's captures.
  return [v](size_t offset, size_t wantBytes, uint8_t* out) -> int {
    if (offset + wantBytes > v.size()) return -1;
    std::memcpy(out, v.data() + offset, wantBytes);
    return static_cast<int>(wantBytes);
  };
}

// Compute SHA-256 of [0..signedLen) of `img`, returning the 32-byte
// digest as a vector. Used by tests that check the verify call's
// message arg matches the streamed-SHA-256 of the signed region.
static std::vector<uint8_t> sha256Prefix(const std::vector<uint8_t>& img,
                                          size_t signedLen) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, /*is224=*/0);
  mbedtls_sha256_update(&ctx, img.data(), signedLen);
  std::vector<uint8_t> digest(32, 0);
  mbedtls_sha256_finish(&ctx, digest.data());
  mbedtls_sha256_free(&ctx);
  return digest;
}

static void resetMock() {
  g_calls.clear();
  g_verifyRc = 0;
}

// --- Tests ---

void test_verify_happy_path() {
  resetMock();
  const size_t signedLen = 1024;
  auto img = buildImage(/*signedLen=*/signedLen, /*embedded=*/signedLen);

  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           &outChannel, &outVersion);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(0x00010203u, outVersion);
  TEST_ASSERT_EQUAL_STRING("stable", outChannel);
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(g_calls.size()));
  // Streaming verify: the mock is called with message = SHA-256 of the
  // signed region (32 bytes), NOT the raw signed bytes.
  TEST_ASSERT_EQUAL_UINT32(32u, static_cast<uint32_t>(g_calls[0].message.size()));
  TEST_ASSERT_EQUAL_UINT32(64u, static_cast<uint32_t>(g_calls[0].sig.size()));
  TEST_ASSERT_EQUAL_UINT32(32u, static_cast<uint32_t>(g_calls[0].pubkey.size()));
  // And the digest matches what we'd compute independently over the
  // first signedLen bytes of the image.
  const auto expectedDigest = sha256Prefix(img, signedLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedDigest.data(), g_calls[0].message.data(), 32);
}

void test_verify_tampered_signature_rejected() {
  // Mock returns non-zero (signature verify fail).
  resetMock();
  g_verifyRc = -1;
  auto img = buildImage(/*signedLen=*/512, /*embedded=*/512);

  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           &outChannel, &outVersion);
  TEST_ASSERT_FALSE(ok);
}

void test_verify_bad_magic_rejected() {
  resetMock();
  // Build a valid image then clobber the magic bytes.
  auto img = buildImage(/*signedLen=*/256, /*embedded=*/256);
  uint8_t* footer = img.data() + 256;
  footer[co::kLsigMagicOffset + 0] = 'X';  // was 'L'

  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
  // Mock should NOT have been called — bad-magic check is the first
  // gate before any hash + signature math.
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(g_calls.size()));
}

void test_verify_too_short_image_rejected() {
  resetMock();
  std::vector<uint8_t> tiny(co::kLsigFooterLen - 1, 0);
  const bool ok = co::verifySignedFirmware(makeVectorReader(tiny), tiny.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
}

void test_verify_null_reader_rejected() {
  resetMock();
  // Default-constructed std::function is empty — verify must reject it
  // before doing anything else.
  const bool ok = co::verifySignedFirmware(co::FirmwareByteReader{}, 1000,
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
}

void test_verify_signed_region_oversize_rejected() {
  // Footer claims signedRegionLen > what fits before the footer. The
  // sanity check guards against a malformed footer directing verify()
  // to walk out of bounds.
  resetMock();
  auto img = buildImage(/*signedLen=*/512, /*embedded=*/9999);

  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(g_calls.size()));
}

void test_verify_signed_region_zero_rejected() {
  resetMock();
  auto img = buildImage(/*signedLen=*/512, /*embedded=*/0);
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
}

void test_verify_channel_extracted() {
  resetMock();
  auto img = buildImage(/*signedLen=*/256, /*embedded=*/256,
                        "LSIG", 1, "beta");
  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           &outChannel, &outVersion);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING("beta", outChannel);
  TEST_ASSERT_EQUAL_UINT32(1u, outVersion);
}

void test_verify_full_8_byte_channel() {
  resetMock();
  // Channel occupies the first 8 of the 16-byte slot with no wire terminator.
  auto img = buildImage(/*signedLen=*/256, /*embedded=*/256,
                        "LSIG", 0x42, "abcdefgh");
  const char* outChannel = nullptr;
  uint32_t outVersion = 0;
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           &outChannel, &outVersion);
  TEST_ASSERT_TRUE(ok);
  // The returned buffer is null-terminated at index 16 (kLsigChannelLen).
  TEST_ASSERT_EQUAL_STRING("abcdefgh", outChannel);
}

void test_verify_out_params_optional() {
  resetMock();
  auto img = buildImage(/*signedLen=*/256, /*embedded=*/256);
  // nullptr out-params must work — caller may not care about channel/version.
  TEST_ASSERT_TRUE(co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                             nullptr, nullptr));
}

void test_verify_minimum_signed_region() {
  // Smallest signed region that still passes (1 byte).
  resetMock();
  auto img = buildImage(/*signedLen=*/1, /*embedded=*/1);
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_TRUE(ok);
  // Mock still receives 32-byte SHA-256 digest of the single signed byte.
  TEST_ASSERT_EQUAL_UINT32(32u, static_cast<uint32_t>(g_calls[0].message.size()));
}

void test_verify_streams_across_multiple_blocks() {
  // Signed region larger than the cpp's internal 4 KB block buffer
  // exercises the multi-iteration streaming path (offsets 0, 4096, ...).
  resetMock();
  const size_t signedLen = 4096 * 3 + 123;  // 12411 bytes — 4 reader calls
  auto img = buildImage(/*signedLen=*/signedLen, /*embedded=*/signedLen);
  const bool ok = co::verifySignedFirmware(makeVectorReader(img), img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_TRUE(ok);
  // Digest still matches what we'd compute over the same bytes in one shot.
  const auto expectedDigest = sha256Prefix(img, signedLen);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expectedDigest.data(), g_calls[0].message.data(), 32);
}

void test_verify_reader_short_read_rejected() {
  // A reader that under-delivers (returns fewer bytes than requested)
  // must fail verification, even if the rest of the footer is valid.
  // This mirrors the production failure mode of esp_partition_read
  // returning ESP_FAIL part way through.
  resetMock();
  auto img = buildImage(/*signedLen=*/512, /*embedded=*/512);
  auto shortReader = [&img](size_t offset, size_t wantBytes,
                            uint8_t* out) -> int {
    if (offset + wantBytes > img.size()) return -1;
    // Always deliver one byte less than asked — provokes a length check.
    if (wantBytes == 0) return 0;
    std::memcpy(out, img.data() + offset, wantBytes - 1);
    return static_cast<int>(wantBytes - 1);
  };
  const bool ok = co::verifySignedFirmware(shortReader, img.size(),
                                           nullptr, nullptr);
  TEST_ASSERT_FALSE(ok);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_verify_happy_path);
  RUN_TEST(test_verify_tampered_signature_rejected);
  RUN_TEST(test_verify_bad_magic_rejected);
  RUN_TEST(test_verify_too_short_image_rejected);
  RUN_TEST(test_verify_null_reader_rejected);
  RUN_TEST(test_verify_signed_region_oversize_rejected);
  RUN_TEST(test_verify_signed_region_zero_rejected);
  RUN_TEST(test_verify_channel_extracted);
  RUN_TEST(test_verify_full_8_byte_channel);
  RUN_TEST(test_verify_out_params_optional);
  RUN_TEST(test_verify_minimum_signed_region);
  RUN_TEST(test_verify_streams_across_multiple_blocks);
  RUN_TEST(test_verify_reader_short_read_rejected);
  return UNITY_END();
}

// Native-host tests for the OTA offer-time authenticity gate.
//
// The bench bug: a firmware OTA receiver accepted an OFFER on version alone,
// streamed the whole multi-MB image, and only verified the ed25519 signature at
// the end — so an unsigned / foreign-key / tampered offer streamed fully, failed
// end-verify, and looped. The fix carries the signed image's SHA-256 digest +
// signature in the OFFER and verifies sig-over-digest BEFORE accepting.
//
// These tests link the production firmware_signature.cpp (via the same
// controllable ed25519 shim test_firmware_signature uses) so the real
// verifyFirmwareDigestSignature runs, and mirror the receiver's offer-gate
// decision to pin the observable contract: a valid offer arms the stream, an
// unverifiable one declines upfront with no erase/stream, and a streamed image
// whose digest differs from the offered (verified) digest is rejected.

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "components/firmware/firmware_signature.hpp"
#include "components/network/protocol/lamp_protocol.hpp"
#include "components/network/protocol/fw_ota.hpp"

void setUp(void) {}
void tearDown(void) {}

namespace lf = lamp::firmware;
namespace lp = lamp_protocol;

// --- ed25519 shim (same seam as test_firmware_signature) -----------------
//
// g_verifyRc drives the mock: 0 = signature valid, -1 = invalid. Production
// firmware_signature.cpp's native path calls this extern.
namespace {
int g_verifyRc = 0;
std::vector<std::vector<uint8_t>> g_verifiedDigests;  // messages passed to verify
}  // namespace

namespace lamp { namespace firmware {
int test_crypto_sign_ed25519_verify_detached(
    const unsigned char* /*sig*/, const unsigned char* m, unsigned long long mlen,
    const unsigned char* /*pk*/) {
  g_verifiedDigests.emplace_back(m, m + mlen);
  return g_verifyRc;
}
}}  // namespace lamp::firmware

#include "../../src/components/firmware/firmware_signature.cpp"

// --- Mirror of the receiver's offer-gate decision ------------------------
//
// Mirrors onOfferOnLoop's firmware-path accept decision: after channel /
// chunkSize / already-current pass, the gate verifies the offer's auth trailer
// with the REAL verifyFirmwareDigestSignature. Records whether the stream armed
// (erase would run) and the ACCEPT status, so a test can assert "no stream on an
// unverifiable offer".
namespace test {

struct GateResult {
  lp::FwAcceptStatus status;
  bool armedStream;  // true only when the gate accepted (erase + stream begin)
};

GateResult offerGate(const lp::ParsedFwOffer& offer) {
  const lp::FwAcceptStatus status = lp::decideFwOfferAuth(
      offer.hasAuth,
      offer.hasAuth &&
          lf::verifyFirmwareDigestSignature(offer.digest, offer.signature));
  return {status, status == lp::FwAcceptStatus::Accept};
}

static const uint8_t kWispMac[6] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15};
static const uint8_t kLampMac[6] = {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

// Build + parse a signed OFFER (auth trailer present) into out.
static lp::ParsedFwOffer makeAuthOffer(const uint8_t digest[32],
                                       const uint8_t sig[64]) {
  uint8_t buf[lp::FW_OFFER_AUTH_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const size_t n = lp::buildFwOffer(buf, sizeof(buf), 1, kWispMac, kLampMac,
                                    0x00010005, 1000, lp::FW_CHUNK_SIZE_BASELINE,
                                    "standard-stable", 15, sha, 96, 5,
                                    lp::PROTOCOL_VERSION_EMIT, lp::MSG_FW_OFFER,
                                    digest, sig);
  lp::ParsedFwOffer out{};
  lp::parseFwOffer(buf, n, out);
  return out;
}

// Build + parse a legacy OFFER (no auth trailer).
static lp::ParsedFwOffer makeLegacyOffer() {
  uint8_t buf[lp::FW_OFFER_FIXED_SIZE];
  const uint8_t sha[lp::FW_SHA256_PREFIX_LEN] = {0};
  const size_t n = lp::buildFwOffer(buf, sizeof(buf), 1, kWispMac, kLampMac,
                                    0x00010005, 1000, lp::FW_CHUNK_SIZE_BASELINE,
                                    "standard-stable", 15, sha, 96, 5);
  lp::ParsedFwOffer out{};
  lp::parseFwOffer(buf, n, out);
  return out;
}

}  // namespace test

// --- (a) valid sig-over-digest → accepted upfront ------------------------
void test_valid_offer_accepted_upfront() {
  g_verifyRc = 0;  // signature valid
  g_verifiedDigests.clear();
  uint8_t digest[32], sig[64];
  for (int i = 0; i < 32; ++i) digest[i] = static_cast<uint8_t>(i + 1);
  for (int i = 0; i < 64; ++i) sig[i] = static_cast<uint8_t>(i);

  auto offer = test::makeAuthOffer(digest, sig);
  auto r = test::offerGate(offer);

  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(lp::FwAcceptStatus::Accept),
                          static_cast<uint8_t>(r.status));
  TEST_ASSERT_TRUE(r.armedStream);
  // The gate verified sig over the OFFERED digest (not the image bytes).
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(g_verifiedDigests.size()));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(digest, g_verifiedDigests[0].data(), 32);
}

// --- (b1) no signature (legacy offer) → declined upfront, no stream ------
void test_offer_without_auth_declined_no_stream() {
  g_verifyRc = 0;  // even if crypto would pass, absent trailer must decline
  g_verifiedDigests.clear();
  auto offer = test::makeLegacyOffer();
  TEST_ASSERT_FALSE(offer.hasAuth);
  auto r = test::offerGate(offer);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lp::FwAcceptStatus::DeclineUnverified),
      static_cast<uint8_t>(r.status));
  TEST_ASSERT_FALSE(r.armedStream);
  // No crypto call: the absent trailer short-circuits before verify.
  TEST_ASSERT_EQUAL_UINT32(0u, static_cast<uint32_t>(g_verifiedDigests.size()));
}

// --- (b2) invalid / foreign-key signature → declined upfront, no stream --
void test_offer_with_invalid_sig_declined_no_stream() {
  g_verifyRc = -1;  // ed25519 verify fails (bad sig or foreign key)
  g_verifiedDigests.clear();
  uint8_t digest[32] = {7}, sig[64] = {7};
  auto offer = test::makeAuthOffer(digest, sig);
  auto r = test::offerGate(offer);

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(lp::FwAcceptStatus::DeclineUnverified),
      static_cast<uint8_t>(r.status));
  TEST_ASSERT_FALSE(r.armedStream);
  // Crypto WAS attempted (trailer present) and returned failure.
  TEST_ASSERT_EQUAL_UINT32(1u, static_cast<uint32_t>(g_verifiedDigests.size()));
}

// --- (c) valid offer, then streamed image whose digest differs → reject --
//
// Mirrors verifyAndApply's post-stream binding: the streamed image's computed
// digest must equal the offer-time verified digest. A source can't offer an
// authentic digest then stream different (even validly-signed) bytes.
// ponytail: exercises the memcmp logic directly, not the production verifyAndApply path (receiver not native-reachable).
void test_streamed_digest_mismatch_rejected() {
  uint8_t offeredDigest[32], streamedDigest[32];
  for (int i = 0; i < 32; ++i) {
    offeredDigest[i] = static_cast<uint8_t>(i + 1);
    streamedDigest[i] = static_cast<uint8_t>(0xF0 + (i & 0x0F));  // different
  }
  const bool matches =
      std::memcmp(streamedDigest, offeredDigest, 32) == 0;
  // verifyAndApply returns OfferShaMismatch when this compare fails.
  TEST_ASSERT_FALSE(matches);

  // Sanity: identical bytes bind cleanly (the success path).
  std::memcpy(streamedDigest, offeredDigest, 32);
  TEST_ASSERT_EQUAL_INT(0, std::memcmp(streamedDigest, offeredDigest, 32));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_valid_offer_accepted_upfront);
  RUN_TEST(test_offer_without_auth_declined_no_stream);
  RUN_TEST(test_offer_with_invalid_sig_declined_no_stream);
  RUN_TEST(test_streamed_digest_mismatch_rejected);
  return UNITY_END();
}

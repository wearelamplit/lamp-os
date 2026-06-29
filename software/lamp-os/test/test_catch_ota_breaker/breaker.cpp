// Native-host unit tests for catch_ota::rollback_breaker.
//
// Three cases: first attempt is allowed; the same-image prefix is blocked
// once it has failed kMaxAttempts times; a different image prefix resets
// the counter.
//
// The test injects an in-memory KV backend via breakerInjectKv() so no
// NVS/Preferences layer is needed on the host. The production source is
// brought into this TU by the direct #include below (same convention as
// test_catch_ota_protocol and test_catch_ota_signature — no test_build_src).

#include <unity.h>

#include <cstdint>
#include <cstring>

#include "components/catch_ota/rollback_breaker.hpp"

// In-memory KV store shared by all tests. setUp() resets it + re-injects
// the backend so each test starts from a clean slate.
namespace {

struct MemStore {
    uint8_t sha[8]{};
    bool    hasSha{false};
    uint8_t failN{0};
};

static MemStore g_store;

static void injectMemKv() {
    catch_ota::BreakerKv kv;

    kv.getFailSha = [](uint8_t* out) -> bool {
        if (!g_store.hasSha) return false;
        std::memcpy(out, g_store.sha, 8);
        return true;
    };
    kv.putFailSha = [](const uint8_t* sha) {
        std::memcpy(g_store.sha, sha, 8);
        g_store.hasSha = true;
    };
    kv.getFailN = []() -> uint8_t { return g_store.failN; };
    kv.putFailN = [](uint8_t n) { g_store.failN = n; };

    catch_ota::breakerInjectKv(kv);
}

}  // namespace

// Bring production source into this TU (after the KV types are in scope).
#include "../../src/components/catch_ota/rollback_breaker.cpp"

void setUp(void) {
    g_store = MemStore{};
    injectMemKv();
}
void tearDown(void) {}

namespace co = catch_ota;

static const uint8_t kShaA[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
static const uint8_t kShaB[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

// A brand-new image (nothing stored) must be allowed.
void test_first_attempt_allowed() {
    TEST_ASSERT_TRUE(co::shouldAttempt(kShaA));
}

// The same prefix is blocked once it has been recorded kMaxAttempts times.
void test_blocked_after_max_attempts() {
    co::recordAttempt(kShaA);  // failN → 1
    co::recordAttempt(kShaA);  // failN → 2
    co::recordAttempt(kShaA);  // failN → 3
    TEST_ASSERT_FALSE(co::shouldAttempt(kShaA));
}

// A different prefix (even after the old one is exhausted) is allowed, and
// recording it resets the counter so subsequent checks reflect the new image.
void test_new_image_resets_counter() {
    // Exhaust kShaA.
    co::recordAttempt(kShaA);
    co::recordAttempt(kShaA);
    co::recordAttempt(kShaA);
    TEST_ASSERT_FALSE(co::shouldAttempt(kShaA));

    // kShaB has never been tried — must be allowed.
    TEST_ASSERT_TRUE(co::shouldAttempt(kShaB));

    // Record one attempt for kShaB; counter is now 1 < kMaxAttempts.
    co::recordAttempt(kShaB);
    TEST_ASSERT_TRUE(co::shouldAttempt(kShaB));

    // kShaA is now the "old" image (store holds kShaB) — must be allowed.
    TEST_ASSERT_TRUE(co::shouldAttempt(kShaA));
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_first_attempt_allowed);
    RUN_TEST(test_blocked_after_max_attempts);
    RUN_TEST(test_new_image_resets_counter);
    return UNITY_END();
}

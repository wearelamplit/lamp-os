#include "rollback_breaker.hpp"

#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Preferences.h>
#endif

namespace catch_ota {

namespace {

#if defined(ARDUINO) || defined(ESP_PLATFORM)

static BreakerKv makePreferencesKv() {
    BreakerKv kv;

    kv.getFailSha = [](uint8_t* out) -> bool {
        Preferences p;
        p.begin("catchota", /*readOnly=*/true);
        size_t n = p.getBytes("failsha", out, 8);
        p.end();
        return n == 8;
    };

    kv.putFailSha = [](const uint8_t* sha) {
        Preferences p;
        p.begin("catchota", /*readOnly=*/false);
        p.putBytes("failsha", sha, 8);
        p.end();
    };

    kv.getFailN = []() -> uint8_t {
        Preferences p;
        p.begin("catchota", /*readOnly=*/true);
        uint8_t n = p.getUChar("failn", 0);
        p.end();
        return n;
    };

    kv.putFailN = [](uint8_t n) {
        Preferences p;
        p.begin("catchota", /*readOnly=*/false);
        p.putUChar("failn", n);
        p.end();
    };

    return kv;
}

static BreakerKv g_kv = makePreferencesKv();

#else

// Host/native build: no default backend. Tests must inject via breakerInjectKv().
static BreakerKv g_kv;

#endif

}  // namespace

void breakerInjectKv(BreakerKv kv) {
    g_kv = std::move(kv);
}

bool shouldAttempt(const uint8_t sha256Prefix[8]) {
    uint8_t storedSha[8];
    const bool hasSha = g_kv.getFailSha(storedSha);
    if (!hasSha || std::memcmp(storedSha, sha256Prefix, 8) != 0) {
        return true;
    }
    return g_kv.getFailN() < kMaxAttempts;
}

void recordAttempt(const uint8_t sha256Prefix[8]) {
    uint8_t storedSha[8];
    const bool hasSha = g_kv.getFailSha(storedSha);
    if (!hasSha || std::memcmp(storedSha, sha256Prefix, 8) != 0) {
        // Write failN first: a power-loss between the two writes leaves failN=1
        // with the old failsha still stored, which is benign — the new image
        // mismatches the stored SHA and is allowed on next boot.
        g_kv.putFailN(1);
        g_kv.putFailSha(sha256Prefix);
    } else {
        const uint8_t existing = g_kv.getFailN();
        g_kv.putFailN(existing + 1 < kMaxAttempts ? existing + 1 : kMaxAttempts);
    }
}

}  // namespace catch_ota

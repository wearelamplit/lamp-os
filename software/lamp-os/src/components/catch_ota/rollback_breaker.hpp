#pragma once

#include <cstdint>
#include <functional>

namespace catch_ota {

constexpr uint8_t kMaxAttempts = 3;

// Key-value backend for the rollback circuit-breaker.
// On device, the default is Preferences (NVS namespace "catchota").
// Tests inject an in-memory implementation via breakerInjectKv().
struct BreakerKv {
    // Returns true and fills out[8] if a sha prefix is stored; false if absent.
    std::function<bool(uint8_t* out)> getFailSha;
    // Writes sha[8] as the current tracked prefix.
    std::function<void(const uint8_t* sha)> putFailSha;
    // Returns the current fail counter (0 if not yet stored).
    std::function<uint8_t()> getFailN;
    // Writes the fail counter.
    std::function<void(uint8_t n)> putFailN;
};

// Replace the active KV backend. Called by tests before each case.
void breakerInjectKv(BreakerKv kv);

// Returns true if the given image prefix may be attempted.
// A new/different prefix is always allowed. The same prefix is blocked
// once it has been recorded kMaxAttempts times.
bool shouldAttempt(const uint8_t sha256Prefix[8]);

// Records one failed attempt for the given prefix. If the prefix differs
// from the stored one, the counter resets to 1. Otherwise it increments.
void recordAttempt(const uint8_t sha256Prefix[8]);

}  // namespace catch_ota

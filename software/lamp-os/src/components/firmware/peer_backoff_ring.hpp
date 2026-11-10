#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace lamp {

// The firmware distributor's per-peer OTA backoff: a peer that failed a session
// is skipped for a duration, and a persistent block (a cross-variant peer)
// holds for the life of the boot. Bounded to kSize peers; a new transient
// penalty never evicts a persistent block. Not thread-safe; the owner
// serializes access.
class PeerBackoffRing {
 public:
  static constexpr size_t kSize = 8;

  bool inBackoff(const uint8_t mac[6], uint32_t nowMs) const;
  // Re-arms an existing entry for `mac` or claims a slot: free, then an
  // expired transient, then an active transient. With every slot persistent,
  // a persistent penalty replaces the longest-held block and a transient one
  // is dropped.
  void note(const uint8_t mac[6], uint32_t nowMs, uint32_t durationMs,
            bool persistent);
  // Holds each expired transient penalty just past its duration, so none reads
  // as active again one millis() wrap later. Call at least once per wrap.
  void retire(uint32_t nowMs);

 private:
  struct Entry {
    uint8_t mac[6] = {};
    uint32_t sinceMs = 0;
    uint32_t durationMs = 0;
    bool used = false;
    bool persistent = false;
  };
  std::array<Entry, kSize> entries_{};
};

}  // namespace lamp

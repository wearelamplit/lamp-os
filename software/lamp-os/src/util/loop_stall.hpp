#pragma once

#include <atomic>
#include <cstdint>

namespace lamp {

// Known multi-second loop-task blocks in progress. Read from any core.
inline std::atomic<uint8_t> g_knownLoopStalls{0};

// Marks a known multi-second loop-task block for its scope (the OTA upfront
// erase, the BLE teardown for the web app), so staggered relays go out inline
// on the recv task instead of queuing behind it. Loop task only.
class KnownLoopStall {
 public:
  KnownLoopStall() { g_knownLoopStalls.fetch_add(1); }
  ~KnownLoopStall() { g_knownLoopStalls.fetch_sub(1); }
  KnownLoopStall(const KnownLoopStall&) = delete;
  KnownLoopStall& operator=(const KnownLoopStall&) = delete;
};

inline bool inKnownLoopStall() { return g_knownLoopStalls.load() != 0; }

}  // namespace lamp

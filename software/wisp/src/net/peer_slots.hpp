#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wisp {

// The unicast peers MeshLink keeps registered with ESP-NOW, least recently
// used out first once full. Not thread-safe; the owner serializes access.
class PeerSlots {
 public:
  // ESP-NOW caps its peer table at 20 on the C6, and the broadcast peer
  // holds one.
  static constexpr size_t kCapacity = 19;

  // What the caller owes ESP-NOW after a touch: deregister evictedMac, then
  // register the touched MAC.
  struct Change {
    bool    added = false;
    bool    evicted = false;
    uint8_t evictedMac[6] = {};
  };

  // Records a send to `mac` at `nowMs`, a reading of a clock that never
  // wraps (wisp::uptimeMs()).
  Change touch(const uint8_t mac[6], uint64_t nowMs);

 private:
  struct Slot {
    uint8_t  mac[6];
    uint64_t lastUsedMs;
    bool     used;
  };
  std::array<Slot, kCapacity> slots_{};
};

}  // namespace wisp

// Fixed-capacity FIFO of ESP-NOW peer MACs whose unicast send FAILed.
// Pure logic, no locking: MeshLink wraps push()/pop() in a portMUX since
// push runs from the async send-status callback (WiFi task) and pop runs
// from the loop task (net/mesh_link.cpp).
#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class FailedSendQueue {
 public:
  static constexpr size_t kCapacity = 8;

  // No-op if `mac` is already pending (one re-open is enough) or the queue
  // is full (the loop task is behind; it catches up next tick).
  void push(const uint8_t mac[6]);

  // Dequeues the oldest pending MAC into `macOut`. False if empty.
  bool pop(uint8_t macOut[6]);

  size_t size() const { return count_; }

 private:
  bool contains(const uint8_t mac[6]) const;

  uint8_t macs_[kCapacity][6] = {};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace wisp

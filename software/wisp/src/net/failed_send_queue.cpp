#include "net/failed_send_queue.hpp"

#include <cstring>

namespace wisp {

bool FailedSendQueue::contains(const uint8_t mac[6]) const {
  for (size_t i = 0; i < count_; i++) {
    const size_t idx = (head_ + i) % kCapacity;
    if (std::memcmp(macs_[idx], mac, 6) == 0) return true;
  }
  return false;
}

void FailedSendQueue::push(const uint8_t mac[6]) {
  if (count_ >= kCapacity) return;
  if (contains(mac)) return;
  const size_t tail = (head_ + count_) % kCapacity;
  std::memcpy(macs_[tail], mac, 6);
  count_++;
}

bool FailedSendQueue::pop(uint8_t macOut[6]) {
  if (count_ == 0) return false;
  std::memcpy(macOut, macs_[head_], 6);
  head_ = (head_ + 1) % kCapacity;
  count_--;
  return true;
}

}  // namespace wisp

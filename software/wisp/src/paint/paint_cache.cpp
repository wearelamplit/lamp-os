#include "paint/paint_cache.hpp"

#include <cstring>

namespace wisp {

size_t LampPaintCache::indexOf(const uint8_t mac[6]) const {
  for (size_t i = 0; i < count_; i++) {
    if (std::memcmp(entries_[i].mac, mac, 6) == 0) return i;
  }
  return count_;
}

void LampPaintCache::stamp(const uint8_t mac[6], const uint8_t base[4],
                           const uint8_t shade[4], uint32_t fadeEndMs,
                           uint32_t lastPaintMs) {
  size_t idx = indexOf(mac);
  if (idx == count_) {
    if (count_ >= kCapacity) return;
    idx = count_++;
    std::memcpy(entries_[idx].mac, mac, 6);
  }
  std::memcpy(entries_[idx].base, base, 4);
  std::memcpy(entries_[idx].shade, shade, 4);
  entries_[idx].fadeEndMs = fadeEndMs;
  entries_[idx].lastPaintMs = lastPaintMs;
  entries_[idx].forceDue = false;
}

void LampPaintCache::touchLastPaint(const uint8_t mac[6], uint32_t lastPaintMs) {
  size_t idx = indexOf(mac);
  if (idx == count_) return;
  entries_[idx].lastPaintMs = lastPaintMs;
  entries_[idx].forceDue = false;
}

void LampPaintCache::markOverdue(const uint8_t mac[6]) {
  size_t idx = indexOf(mac);
  if (idx == count_) return;
  entries_[idx].lastPaintMs = 0;
  entries_[idx].forceDue = true;
}

bool LampPaintCache::find(const uint8_t mac[6], Entry& out) const {
  size_t idx = indexOf(mac);
  if (idx == count_) return false;
  out = entries_[idx];
  return true;
}

void LampPaintCache::pruneToRoster(const uint8_t macs[][6], size_t count) {
  size_t w = 0;
  for (size_t i = 0; i < count_; i++) {
    bool present = false;
    for (size_t j = 0; j < count; j++) {
      if (std::memcmp(entries_[i].mac, macs[j], 6) == 0) { present = true; break; }
    }
    if (present) {
      if (w != i) entries_[w] = entries_[i];
      w++;
    }
  }
  count_ = w;
}

}  // namespace wisp

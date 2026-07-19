#include "fleet/lamp_inventory.hpp"

#include "fleet/freertos_shim.hpp"

#include <cstring>

namespace wisp {

namespace {
inline SemaphoreHandle_t asHandle(void* m) {
  return reinterpret_cast<SemaphoreHandle_t>(m);
}

void copyName(char (&dst)[lamp_protocol::HELLO_MAX_NAME + 1], const char* src) {
  std::strncpy(dst, src ? src : "", lamp_protocol::HELLO_MAX_NAME);
  dst[lamp_protocol::HELLO_MAX_NAME] = '\0';
}
}  // namespace

LampInventory::LampInventory() {
  mutex_ = xSemaphoreCreateMutex();
}

size_t LampInventory::findByMacLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < count_; i++) {
    if (std::memcmp(entries_[i].mac, mac, 6) == 0) return i;
  }
  return count_;
}

void LampInventory::evictOldestIfFullLocked() {
  if (count_ < MAX_LAMPS) return;
  size_t oldestIdx = 0;
  uint32_t oldestStamp = entries_[0].lastSeenMs;
  for (size_t i = 1; i < count_; i++) {
    if (entries_[i].lastSeenMs < oldestStamp) {
      oldestStamp = entries_[i].lastSeenMs;
      oldestIdx = i;
    }
  }
  if (oldestIdx != count_ - 1) {
    entries_[oldestIdx] = entries_[count_ - 1];
  }
  count_--;
}

void LampInventory::recordHello(const uint8_t mac[6], const char* name,
                                const uint8_t baseRGBW[4], const uint8_t shadeRGBW[4],
                                uint32_t firmwareVersion, uint32_t nowMs,
                                int8_t rssi) {
  // Bounded take, runs on the WiFi recv task; mustn't stall a loop-task reader.
  if (xSemaphoreTake(asHandle(mutex_), pdMS_TO_TICKS(2)) != pdTRUE) return;

  size_t idx = findByMacLocked(mac);
  if (idx == count_) {
    evictOldestIfFullLocked();
    InventoryEntry& e = entries_[count_++];
    std::memcpy(e.mac, mac, 6);
    copyName(e.name, name);
    std::memcpy(e.baseColor, baseRGBW, 4);
    std::memcpy(e.shadeColor, shadeRGBW, 4);
    e.firmwareVersion = firmwareVersion;
    e.lastSeenMs = nowMs;
    e.rssi = rssi;
  } else {
    copyName(entries_[idx].name, name);
    std::memcpy(entries_[idx].baseColor, baseRGBW, 4);
    std::memcpy(entries_[idx].shadeColor, shadeRGBW, 4);
    entries_[idx].firmwareVersion = firmwareVersion;
    entries_[idx].lastSeenMs = nowMs;
    // Preserve the "never measured" sentinel: if the caller didn't have
    // a real RSSI, keep the stored value. A real measurement overwrites
    // with the latest sample.
    if (rssi != INT8_MIN) {
      entries_[idx].rssi = rssi;
    }
  }
  xSemaphoreGive(asHandle(mutex_));
}

void LampInventory::prune(uint32_t nowMs, uint32_t maxAgeMs) {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  for (size_t i = 0; i < count_; ) {
    const uint32_t last = entries_[i].lastSeenMs;
    const uint32_t age  = (nowMs >= last) ? nowMs - last : 0;
    if (last != 0 && age > maxAgeMs) {
      if (i != count_ - 1) entries_[i] = entries_[count_ - 1];
      count_--;
      continue;
    }
    i++;
  }
  xSemaphoreGive(asHandle(mutex_));
}

std::vector<InventoryEntry> LampInventory::snapshot() {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  std::vector<InventoryEntry> out(entries_.begin(), entries_.begin() + count_);
  xSemaphoreGive(asHandle(mutex_));
  return out;
}

size_t LampInventory::copyObservations(LampObservation* out, size_t max) {
  if (!out || max == 0) return 0;
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  const size_t n = count_ < max ? count_ : max;
  for (size_t i = 0; i < n; i++) {
    std::memcpy(out[i].mac, entries_[i].mac, 6);
    out[i].rssi = entries_[i].rssi;
  }
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

size_t LampInventory::size() {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  size_t n = count_;
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

}  // namespace wisp

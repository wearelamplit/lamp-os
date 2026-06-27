#include "LampInventory.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <cstring>

namespace wisp {

namespace {
inline SemaphoreHandle_t asHandle(void* m) {
  return reinterpret_cast<SemaphoreHandle_t>(m);
}
}  // namespace

LampInventory::LampInventory() {
  mutex_ = xSemaphoreCreateMutex();
}

size_t LampInventory::findByMacLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < entries_.size(); i++) {
    if (std::memcmp(entries_[i].mac, mac, 6) == 0) return i;
  }
  return entries_.size();
}

void LampInventory::evictOldestIfFullLocked(uint32_t nowMs) {
  (void)nowMs;  // unused — eviction picks the entry with the smallest stamp
  if (entries_.size() < MAX_LAMPS) return;
  size_t oldestIdx = 0;
  uint32_t oldestStamp = entries_[0].lastSeenMs;
  for (size_t i = 1; i < entries_.size(); i++) {
    if (entries_[i].lastSeenMs < oldestStamp) {
      oldestStamp = entries_[i].lastSeenMs;
      oldestIdx = i;
    }
  }
  if (oldestIdx != entries_.size() - 1) {
    entries_[oldestIdx] = entries_.back();
  }
  entries_.pop_back();
}

void LampInventory::recordHello(const uint8_t mac[6], const std::string& name,
                                const uint8_t baseRGBW[4], const uint8_t shadeRGBW[4],
                                uint32_t firmwareVersion, uint32_t nowMs,
                                int8_t rssi) {
  // Bounded take — mirrors nearby_lamps's pattern for the same reason: this
  // runs on a WiFi-task callback and shouldn't stall on a loop-task reader.
  if (xSemaphoreTake(asHandle(mutex_), pdMS_TO_TICKS(2)) != pdTRUE) return;

  size_t idx = findByMacLocked(mac);
  if (idx == entries_.size()) {
    evictOldestIfFullLocked(nowMs);
    InventoryEntry e;
    std::memcpy(e.mac, mac, 6);
    e.name = name;
    std::memcpy(e.baseColor, baseRGBW, 4);
    std::memcpy(e.shadeColor, shadeRGBW, 4);
    e.firmwareVersion = firmwareVersion;
    e.lastSeenMs = nowMs;
    e.rssi = rssi;
    entries_.push_back(e);
  } else {
    entries_[idx].name = name;
    std::memcpy(entries_[idx].baseColor, baseRGBW, 4);
    std::memcpy(entries_[idx].shadeColor, shadeRGBW, 4);
    entries_[idx].firmwareVersion = firmwareVersion;
    entries_[idx].lastSeenMs = nowMs;
    // Preserve the "never measured" sentinel: if the caller didn't have
    // a real RSSI, keep whatever we had stored. Real measurements always
    // overwrite — RSSI is the latest sample, not a running average.
    if (rssi != INT8_MIN) {
      entries_[idx].rssi = rssi;
    }
  }
  xSemaphoreGive(asHandle(mutex_));
}

void LampInventory::prune(uint32_t nowMs, uint32_t maxAgeMs) {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  for (size_t i = 0; i < entries_.size(); ) {
    const uint32_t last = entries_[i].lastSeenMs;
    const uint32_t age  = (nowMs >= last) ? nowMs - last : 0;
    if (last != 0 && age > maxAgeMs) {
      if (i != entries_.size() - 1) entries_[i] = entries_.back();
      entries_.pop_back();
      continue;
    }
    i++;
  }
  xSemaphoreGive(asHandle(mutex_));
}

std::vector<InventoryEntry> LampInventory::snapshot() {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  std::vector<InventoryEntry> out = entries_;
  xSemaphoreGive(asHandle(mutex_));
  return out;
}

size_t LampInventory::size() {
  xSemaphoreTake(asHandle(mutex_), portMAX_DELAY);
  size_t n = entries_.size();
  xSemaphoreGive(asHandle(mutex_));
  return n;
}

}  // namespace wisp

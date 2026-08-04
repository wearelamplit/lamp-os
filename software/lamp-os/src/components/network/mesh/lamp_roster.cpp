#include "lamp_roster.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#ifdef LAMP_DEBUG
#include <cstdlib>
#endif

namespace lamp {

#ifdef LAMP_DEBUG
LampRoster::DebugGetterViolationFn LampRoster::s_debugGetterViolation = nullptr;

void LampRoster::debugGetterEnter() {
  TaskHandle_t cur = xTaskGetCurrentTaskHandle();
  if (getterTask_ == nullptr) getterTask_ = cur;
  const char* reason = nullptr;
  if (getterTask_ != cur) {
    reason = "roster getter called from unexpected task";
  } else if (getterInUse_) {
    reason = "re-entrant roster getter (nested getNear/getMesh/getAll)";
  }
  if (reason) {
    if (s_debugGetterViolation) {
      s_debugGetterViolation(reason);
    } else {
      Serial.printf("[roster] FATAL %s\n", reason);
      abort();
    }
  }
  getterInUse_ = true;
}

void LampRoster::debugGetterExit() { getterInUse_ = false; }
#endif

namespace {
// Truncate-on-copy into the fixed name slot; always NUL-terminated.
// `src` must be NUL-terminated (HELLO parse and NimBLE names both are).
void copyName(char (&dst)[lamp_protocol::HELLO_MAX_NAME + 1],
              const char* src) {
  std::snprintf(dst, sizeof(dst), "%s", src ? src : "");
}

// Most-recent sighting across either transport. Eviction and prune sort key.
uint32_t lastSeen(const RosterEntry& e) {
  return std::max(e.lastSeenNearMs, e.lastSeenMeshMs);
}
}  // namespace

LampRoster lampRoster;  // global instance

LampRoster::LampRoster() {
  mutex_ = xSemaphoreCreateMutex();
}

size_t LampRoster::findIndexLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < count_; i++) {
    if (store_[i].hasMac && std::memcmp(store_[i].mac, mac, 6) == 0) return i;
  }
  return count_;
}

void LampRoster::evictOldestIfFullLocked() {
  if (count_ < kCapacity) return;
  size_t oldestIdx = 0;
  uint32_t oldestMax = lastSeen(store_[0]);
  for (size_t i = 1; i < count_; i++) {
    uint32_t m = lastSeen(store_[i]);
    if (m < oldestMax) { oldestMax = m; oldestIdx = i; }
  }
  if (oldestIdx != count_ - 1) {
    store_[oldestIdx] = store_[count_ - 1];
  }
  count_--;
}

void LampRoster::addOrUpdateFromBle(const std::string& name,
                                    const std::string& bleAddr,
                                    const Color& base, const Color& shade,
                                    int8_t rssi) {
  uint32_t now = millis();
  uint8_t ble[6];
  if (!parseBdAddr(bleAddr.c_str(), ble)) return;
  uint8_t recoveredMac[6];
  meshMacFromBleAddr(ble, recoveredMac);
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t idx = findIndexLocked(recoveredMac);
  if (idx == count_) {
    evictOldestIfFullLocked();
    RosterEntry e;
    copyName(e.name, name.c_str());
    std::memcpy(e.mac, recoveredMac, 6);
    e.hasMac = true;
    e.baseColor = base;
    e.shadeColor = shade;
    e.lastSeenNearMs = now;
    e.lastRssi = rssi;
    store_[count_++] = e;
  } else {
    // Empty name from a nameless BLE adv must not erase a known display name
    // on the merged entry.
    if (!name.empty()) copyName(store_[idx].name, name.c_str());
    store_[idx].baseColor = base;
    store_[idx].shadeColor = shade;
    store_[idx].lastSeenNearMs = now;
    // RSSI freshens on every BLE adv (~30 Hz) so the closest-peer sort key
    // tracks current signal strength rather than a one-shot first-seen
    // value. -127 is the "unknown" sentinel; only overwrite when the
    // caller supplied a real reading.
    if (rssi != -127) store_[idx].lastRssi = rssi;
  }
  generation_++;
  xSemaphoreGive(mutex_);
}

void LampRoster::addOrUpdateFromEspNow(const char* name, const uint8_t mac[6],
                                       const Color& base, const Color& shade,
                                       uint32_t firmwareVersion,
                                       uint8_t otaState,
                                       uint8_t protocolVersion,
                                       const char* fwChannel,
                                       const uint8_t* fsDigest,
                                       bool hasFsDigest,
                                       uint16_t maxChunk,
                                       bool needsFs,
                                       const uint8_t* otaSendingTo,
                                       bool hasOtaSendingTo,
                                       int8_t rssi) {
  uint32_t now = millis();
  // WiFi task: bounded take so a stall doesn't block recv frames or the
  // link_.broadcast(). On timeout the write drops; the next HELLO retries.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
#ifdef LAMP_DEBUG
    static uint32_t lastDropLogMs = 0;
    uint32_t logNow = millis();
    if (logNow - lastDropLogMs > 1000) {
      Serial.printf("[roster] addOrUpdateFromEspNow: mutex contended, dropped (name=%s)\n",
                    name ? name : "");
      lastDropLogMs = logNow;
    }
#endif
    return;
  }
  // lastRssi not updated from HELLO: single transport source prevents
  // cross-transport contamination in PersonalityEngine's hysteresis.
  size_t idx = findIndexLocked(mac);
  if (idx == count_) {
    evictOldestIfFullLocked();
    RosterEntry e;
    copyName(e.name, name);
    e.baseColor = base;
    e.shadeColor = shade;
    std::memcpy(e.mac, mac, 6);
    e.hasMac = true;
    e.lastSeenMeshMs = now;
    e.firmwareVersion = firmwareVersion;
    e.otaState = otaState;
    e.protocolVersion = protocolVersion;
    if (fwChannel && fwChannel[0] != '\0') {
      std::strncpy(e.fwChannel, fwChannel, sizeof(e.fwChannel) - 1);
      e.fwChannel[sizeof(e.fwChannel) - 1] = '\0';
    }
    if (hasFsDigest && fsDigest) {
      std::memcpy(e.fsDigest, fsDigest, sizeof(e.fsDigest));
      e.hasFsDigest = true;
    }
    e.needsFs = needsFs;
    e.maxChunk = maxChunk;
    e.hasOtaSendingTo = hasOtaSendingTo;
    if (hasOtaSendingTo && otaSendingTo) {
      std::memcpy(e.otaSendingTo, otaSendingTo, 6);
    }
    e.espnowRssi = rssi;
    store_[count_++] = e;
  } else {
    // A HELLO whose nameLen was 0 arrives with an empty name; keep the last
    // known display name rather than blanking the merged entry.
    if (name && name[0] != '\0') copyName(store_[idx].name, name);
    store_[idx].baseColor = base;
    store_[idx].shadeColor = shade;
    std::memcpy(store_[idx].mac, mac, 6);
    store_[idx].hasMac = true;
    store_[idx].lastSeenMeshMs = now;
    // Zero from BLE-only callers (no HELLO) leaves a known version intact.
    if (firmwareVersion != 0) store_[idx].firmwareVersion = firmwareVersion;
    // OTA state is instantaneous; the latest HELLO always wins.
    // BLE-only callers pass 0 (idle), the correct default.
    store_[idx].otaState = otaState;
    // Zero from BLE-only callers leaves a known protocolVersion intact.
    if (protocolVersion != 0) {
      store_[idx].protocolVersion = protocolVersion;
    }
    // Older peers and BLE-only callers pass nullptr; leave a known channel intact.
    if (fwChannel && fwChannel[0] != '\0') {
      std::strncpy(store_[idx].fwChannel, fwChannel,
                   sizeof(store_[idx].fwChannel) - 1);
      store_[idx].fwChannel[sizeof(store_[idx].fwChannel) - 1] = '\0';
    }
    // FS digest always refreshes when present (it changes when the peer's UI
    // image changes); absent (older peer / BLE-only) leaves the last known.
    if (hasFsDigest && fsDigest) {
      std::memcpy(store_[idx].fsDigest, fsDigest, sizeof(store_[idx].fsDigest));
      store_[idx].hasFsDigest = true;
    }
    // Instantaneous like otaState: the latest HELLO wins, so a healed peer that
    // stops emitting NEED_FS clears here. Only HELLO reaches this method.
    store_[idx].needsFs = needsFs;
    // Instantaneous like otaState: the latest HELLO wins. Absent (idle sender,
    // older peer, or BLE-only caller) clears the edge.
    store_[idx].hasOtaSendingTo = hasOtaSendingTo;
    if (hasOtaSendingTo && otaSendingTo) {
      std::memcpy(store_[idx].otaSendingTo, otaSendingTo, 6);
    }
    // Zero from BLE-only callers or older peers leaves a known value intact.
    if (maxChunk != 0) store_[idx].maxChunk = maxChunk;
    // Freshens every HELLO like lastRssi does for BLE adv; -127 sentinel
    // (unavailable RSSI) leaves the last known reading intact.
    if (rssi != -127) store_[idx].espnowRssi = rssi;
  }
  generation_++;
  xSemaphoreGive(mutex_);
}

void LampRoster::prune(uint32_t maxAgeMs) {
  uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < count_; ) {
    uint32_t mostRecent = lastSeen(store_[i]);
    if (mostRecent != 0 && (now - mostRecent) > maxAgeMs) {
      if (i != count_ - 1) store_[i] = store_[count_ - 1];
      count_--;
      generation_++;
      continue;
    }
    i++;
  }
  xSemaphoreGive(mutex_);
}

// Copy store_ into a reused buffer under lock, filter outside. Bounds the
// critical section so ESP-NOW recv-side bounded takes don't time out on a
// loop-task reader, and reuses the buffer so steady-state queries don't churn
// the fragmented heap.
std::vector<RosterEntry>& LampRoster::getNear(uint32_t maxAgeMs) {
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  uint32_t now = millis();
  nearBuf_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  nearBuf_.insert(nearBuf_.end(), store_.begin(), store_.begin() + count_);
  xSemaphoreGive(mutex_);
  nearBuf_.erase(
      std::remove_if(nearBuf_.begin(), nearBuf_.end(),
                     [&](const RosterEntry& e) {
                       return !(e.lastSeenNearMs != 0 &&
                                (now - e.lastSeenNearMs) <= maxAgeMs);
                     }),
      nearBuf_.end());
  // Highest RSSI first: `peers.front()` gives the nearest lamp
  // (cascade-stagger sort key). -127 sorts to the back. std::sort (in-place
  // introsort, no merge-buffer alloc) avoids bad_alloc on the fragmented
  // heap; the mac tiebreak gives equal-RSSI peers a deterministic order.
  std::sort(nearBuf_.begin(), nearBuf_.end(),
            [](const RosterEntry& a, const RosterEntry& b) {
              if (a.lastRssi != b.lastRssi) return a.lastRssi > b.lastRssi;
              return std::memcmp(a.mac, b.mac, 6) < 0;
            });
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return nearBuf_;
}

const std::vector<NearbyCopy>& LampRoster::snapshotNear(uint32_t maxAgeMs) {
  std::vector<RosterEntry>& near = getNear(maxAgeMs);
  nearSnapshot_.clear();
  for (const RosterEntry& e : near) {
    NearbyCopy c;
    std::memcpy(c.name, e.name, sizeof(c.name));
    std::memcpy(c.mac, e.mac, 6);
    c.hasMac = e.hasMac;
    c.baseColor = e.baseColor;
    c.shadeColor = e.shadeColor;
    c.lastRssi = e.lastRssi;
    c.lastSeenNearMs = e.lastSeenNearMs;
    nearSnapshot_.push_back(c);
  }
  return nearSnapshot_;
}

std::vector<RosterEntry>& LampRoster::getMesh(uint32_t maxAgeMs) {
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  uint32_t now = millis();
  meshBuf_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  meshBuf_.insert(meshBuf_.end(), store_.begin(), store_.begin() + count_);
  xSemaphoreGive(mutex_);
  meshBuf_.erase(
      std::remove_if(meshBuf_.begin(), meshBuf_.end(),
                     [&](const RosterEntry& e) {
                       return !(e.lastSeenMeshMs != 0 &&
                                (now - e.lastSeenMeshMs) <= maxAgeMs);
                     }),
      meshBuf_.end());
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return meshBuf_;
}

std::vector<RosterEntry>& LampRoster::getAll() {
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  allBuf_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  allBuf_.insert(allBuf_.end(), store_.begin(), store_.begin() + count_);
  xSemaphoreGive(mutex_);
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return allBuf_;
}

bool LampRoster::findByMac(const uint8_t mac[6], RosterEntry& out) {
  if (mac == nullptr) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < count_; i++) {
    if (store_[i].hasMac && std::memcmp(store_[i].mac, mac, 6) == 0) {
      out = store_[i];
      xSemaphoreGive(mutex_);
      return true;
    }
  }
  xSemaphoreGive(mutex_);
  return false;
}

void LampRoster::acknowledge(const uint8_t mac[6]) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t idx = findIndexLocked(mac);
  if (idx < count_) {
    store_[idx].acknowledged = true;
    generation_++;
  }
  xSemaphoreGive(mutex_);
}

void LampRoster::markNear(const uint8_t mac[6]) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) return;
  size_t idx = findIndexLocked(mac);
  if (idx < count_) {
    store_[idx].lastSeenNearMs = millis();
    generation_++;
  }
  xSemaphoreGive(mutex_);
}

uint32_t LampRoster::generation() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  uint32_t g = generation_;
  xSemaphoreGive(mutex_);
  return g;
}

}  // namespace lamp

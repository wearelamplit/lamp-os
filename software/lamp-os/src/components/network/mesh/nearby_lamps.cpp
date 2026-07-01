#include "nearby_lamps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/network/mesh/wisp_claims_addr.hpp"
#include "util/proximity.hpp"

namespace lamp {

namespace {
// BLE BD_ADDR = WiFi STA MAC + 2 on the last byte. ESP32 silicon
// convention; holds for WROOM and C6.
std::string deriveBdAddrFromEspNowMac(const uint8_t mac[6]) {
  uint8_t bd[6];
  ble_control::bdAddrFromMeshMac(mac, bd);
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                bd[0], bd[1], bd[2], bd[3], bd[4], bd[5]);
  return std::string(buf);
}

// Most-recent sighting across either transport. Eviction and prune sort key.
uint32_t lastSeen(const NearbyLamp& e) {
  return std::max(e.lastSeenViaBleMs, e.lastSeenViaEspNowMs);
}
}  // namespace

NearbyLamps nearbyLamps;  // global instance

NearbyLamps::NearbyLamps() {
  mutex_ = xSemaphoreCreateMutex();
}

size_t NearbyLamps::findIndexLocked(const std::string& name) const {
  for (size_t i = 0; i < store_.size(); i++) {
    if (store_[i].name == name) return i;
  }
  return store_.size();
}

void NearbyLamps::evictOldestIfFullLocked() {
  if (store_.size() < MAX_NEARBY) return;
  size_t oldestIdx = 0;
  uint32_t oldestMax = lastSeen(store_[0]);
  for (size_t i = 1; i < store_.size(); i++) {
    uint32_t m = lastSeen(store_[i]);
    if (m < oldestMax) { oldestMax = m; oldestIdx = i; }
  }
  if (oldestIdx != store_.size() - 1) {
    store_[oldestIdx] = store_.back();
  }
  store_.pop_back();
}

void NearbyLamps::addOrUpdateFromBle(const std::string& name,
                                     const std::string& bdAddr,
                                     const Color& base, const Color& shade,
                                     int8_t rssi) {
  uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t idx = findIndexLocked(name);
  if (idx == store_.size()) {
    evictOldestIfFullLocked();
    NearbyLamp e;
    e.name = name;
    e.bdAddr = bdAddr;
    e.baseColor = base;
    e.shadeColor = shade;
    e.lastSeenViaBleMs = now;
    e.lastRssi = rssi;
    e.firstSeenMs = now;
    store_.push_back(e);
  } else {
    // bdAddr only fills once (first sighting). Name collision silently
    // drops the second peer's address; dispositions route to the first-seen
    // BD_ADDR. Keying by name preserves BLE-only lamp compatibility.
    if (store_[idx].bdAddr.empty()) {
      store_[idx].bdAddr = bdAddr;
    }
    store_[idx].baseColor = base;
    store_[idx].shadeColor = shade;
    store_[idx].lastSeenViaBleMs = now;
    // RSSI freshens on every BLE adv (~30 Hz) so the proximity sort key
    // tracks current signal strength rather than a one-shot first-seen
    // value. -127 is the "unknown" sentinel; only overwrite when the
    // caller supplied a real reading.
    if (rssi != -127) store_[idx].lastRssi = rssi;
  }
  xSemaphoreGive(mutex_);
}

void NearbyLamps::addOrUpdateFromEspNow(const std::string& name, const uint8_t mac[6],
                                        const Color& base, const Color& shade,
                                        uint32_t firmwareVersion,
                                        uint8_t otaState,
                                        uint8_t protocolVersion,
                                        const char* fwChannel,
                                        const uint8_t* fsDigest,
                                        bool hasFsDigest) {
  uint32_t now = millis();
  // Derived before the lock: snprintf + heap alloc outside the critical
  // section so it stays short.
  const std::string derivedBdAddr = deriveBdAddrFromEspNowMac(mac);
  // WiFi task: bounded take so a stall doesn't block recv frames or the
  // link_.broadcast(). On timeout the write drops; HELLO repeats every 5 s.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
#ifdef LAMP_DEBUG
    static uint32_t lastDropLogMs = 0;
    uint32_t logNow = millis();
    if (logNow - lastDropLogMs > 1000) {
      Serial.printf("[nearby] addOrUpdateFromEspNow: mutex contended, dropped (name=%s)\n",
                    name.c_str());
      lastDropLogMs = logNow;
    }
#endif
    return;
  }
  // lastRssi not updated from HELLO: single transport source prevents
  // cross-transport contamination in PersonalityEngine's hysteresis.
  size_t idx = findIndexLocked(name);
  if (idx == store_.size()) {
    evictOldestIfFullLocked();
    NearbyLamp e;
    e.name = name;
    e.baseColor = base;
    e.shadeColor = shade;
    std::memcpy(e.mac, mac, 6);
    e.hasMac = true;
    e.bdAddr = derivedBdAddr;
    e.lastSeenViaEspNowMs = now;
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
    store_.push_back(e);
  } else {
    store_[idx].baseColor = base;
    store_[idx].shadeColor = shade;
    std::memcpy(store_[idx].mac, mac, 6);
    store_[idx].hasMac = true;
    // Observed BLE BD_ADDR wins; only fill from the derivation when
    // the entry has no real BLE sighting yet.
    if (store_[idx].bdAddr.empty()) {
      store_[idx].bdAddr = derivedBdAddr;
    }
    store_[idx].lastSeenViaEspNowMs = now;
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
  }
  xSemaphoreGive(mutex_);
}

void NearbyLamps::prune(uint32_t maxAgeMs) {
  uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < store_.size(); ) {
    uint32_t mostRecent = lastSeen(store_[i]);
    if (mostRecent != 0 && (now - mostRecent) > maxAgeMs) {
      if (i != store_.size() - 1) store_[i] = store_.back();
      store_.pop_back();
      continue;
    }
    i++;
  }
  xSemaphoreGive(mutex_);
}

// Copy store_ under lock, filter outside. Bounds the critical section so
// ESP-NOW recv-side bounded takes don't time out on a loop-task reader.
std::vector<NearbyLamp> NearbyLamps::getReachableViaBle(uint32_t maxAgeMs) {
  uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<NearbyLamp> snapshot = store_;
  xSemaphoreGive(mutex_);
  std::vector<NearbyLamp> out;
  out.reserve(snapshot.size());
  for (const auto& e : snapshot) {
    if (e.lastSeenViaBleMs != 0 && (now - e.lastSeenViaBleMs) <= maxAgeMs) {
      out.push_back(e);
    }
  }
  // Highest RSSI first: `peers.front()` gives the nearest lamp
  // (PersonalityEngine closest-peer tracking, cascade-stagger sort key).
  // -127 sorts to the back. Stable sort keeps equal-RSSI order predictable.
  std::stable_sort(out.begin(), out.end(),
                    [](const NearbyLamp& a, const NearbyLamp& b) {
                      return a.lastRssi > b.lastRssi;
                    });
  return out;
}

std::vector<NearbyLamp> NearbyLamps::getReachableViaEspNow(uint32_t maxAgeMs) {
  uint32_t now = millis();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<NearbyLamp> snapshot = store_;
  xSemaphoreGive(mutex_);
  std::vector<NearbyLamp> out;
  out.reserve(snapshot.size());
  for (const auto& e : snapshot) {
    if (e.lastSeenViaEspNowMs != 0 && (now - e.lastSeenViaEspNowMs) <= maxAgeMs) {
      out.push_back(e);
    }
  }
  return out;
}

std::vector<NearbyLamp> NearbyLamps::getAll() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::vector<NearbyLamp> snapshot = store_;
  xSemaphoreGive(mutex_);
  return snapshot;
}

bool NearbyLamps::findByBdAddr(const std::string& bdAddr, NearbyLamp& out) {
  if (bdAddr.empty()) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (const auto& e : store_) {
    if (e.bdAddr == bdAddr) {
      out = e;
      xSemaphoreGive(mutex_);
      return true;
    }
  }
  xSemaphoreGive(mutex_);
  return false;
}

bool NearbyLamps::findByMac(const uint8_t mac[6], NearbyLamp& out) {
  if (mac == nullptr) return false;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (const auto& e : store_) {
    if (e.hasMac && std::memcmp(e.mac, mac, 6) == 0) {
      out = e;
      xSemaphoreGive(mutex_);
      return true;
    }
  }
  xSemaphoreGive(mutex_);
  return false;
}

void NearbyLamps::acknowledge(const std::string& name) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t idx = findIndexLocked(name);
  if (idx < store_.size()) store_[idx].acknowledged = true;
  xSemaphoreGive(mutex_);
}

}  // namespace lamp

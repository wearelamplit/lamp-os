#include "lamp_roster.hpp"

#include "proximity.hpp"

#include <lampos/clock.hpp>

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
    reason = "re-entrant roster getter (nested snapshotNear/getMesh/getAll)";
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

// A BLE advert carries RGB only. Keep the W a HELLO supplied while the
// advert's RGB still agrees with it, so a W-heavy colour does not read as dark.
Color keepHelloWhite(Color advert, Color known) {
  if (advert.r == known.r && advert.g == known.g && advert.b == known.b) {
    return Color(advert.r, advert.g, advert.b, known.w);
  }
  return advert;
}

NearbyCopy toNearbyCopy(const RosterEntry& e) {
  NearbyCopy c;
  std::memcpy(c.name, e.name, sizeof(c.name));
  std::memcpy(c.mac, e.mac, 6);
  c.hasMac = e.hasMac;
  c.baseColor = e.baseColor;
  c.shadeColor = e.shadeColor;
  c.lastRssi = e.lastRssi;
  c.lastSeenNearMs = e.lastSeenNearMs;
  c.lastSeenBleMs = e.lastSeenBleMs;
  c.variant = e.variant;
  return c;
}

MeshPeer toMeshPeer(const RosterEntry& e) {
  MeshPeer m;
  m.firmwareVersion = e.firmwareVersion;
  m.maxChunk = e.maxChunk;
  std::memcpy(m.mac, e.mac, 6);
  std::memcpy(m.otaSendingTo, e.otaSendingTo, 6);
  std::memcpy(m.fsDigest, e.fsDigest, sizeof(m.fsDigest));
  std::memcpy(m.fwChannel, e.fwChannel, sizeof(m.fwChannel));
  m.otaState = e.otaState;
  m.protocolVersion = e.protocolVersion;
  m.hasMac = e.hasMac;
  m.hasFsDigest = e.hasFsDigest;
  m.needsFs = e.needsFs;
  m.hasOtaSendingTo = e.hasOtaSendingTo;
  m.espnowRssi = e.espnowRssi;
  m.lastRssi = e.lastRssi;
  return m;
}

ListedPeer toListedPeer(const RosterEntry& e) {
  ListedPeer l;
  std::memcpy(l.name, e.name, sizeof(l.name));
  std::memcpy(l.fwChannel, e.fwChannel, sizeof(l.fwChannel));
  std::memcpy(l.mac, e.mac, 6);
  std::memcpy(l.otaSendingTo, e.otaSendingTo, 6);
  l.baseColor = e.baseColor;
  l.shadeColor = e.shadeColor;
  l.lastSeenNearMs = e.lastSeenNearMs;
  l.lastSeenMeshMs = e.lastSeenMeshMs;
  l.firmwareVersion = e.firmwareVersion;
  l.lastRssi = e.lastRssi;
  l.otaState = e.otaState;
  l.hasMac = e.hasMac;
  l.hasOtaSendingTo = e.hasOtaSendingTo;
  return l;
}
}  // namespace

LampRoster lampRoster;  // global instance

LampRoster::LampRoster() {
  mutex_ = xSemaphoreCreateMutex();
  meshBuf_.reserve(kCapacity);
  nearSnapshot_.reserve(kCapacity);
}

size_t LampRoster::findIndexLocked(const uint8_t mac[6]) const {
  for (size_t i = 0; i < count_; i++) {
    if (store_[i].hasMac && std::memcmp(store_[i].mac, mac, 6) == 0) return i;
  }
  return count_;
}

void LampRoster::evictOldestIfFullLocked(uint32_t nowMs) {
  if (count_ < kCapacity) return;
  size_t oldestIdx = 0;
  uint32_t oldestAge = sinceLastSeen(store_[0].lastSeenNearMs, store_[0].lastSeenMeshMs, nowMs);
  for (size_t i = 1; i < count_; i++) {
    const uint32_t age = sinceLastSeen(store_[i].lastSeenNearMs, store_[i].lastSeenMeshMs, nowMs);
    if (age > oldestAge) { oldestAge = age; oldestIdx = i; }
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
  uint8_t ble[6];
  if (!parseBdAddr(bleAddr.c_str(), ble)) return;
  addOrUpdateFromBle(name.c_str(), ble, base, shade, rssi);
}

void LampRoster::addOrUpdateFromBle(const char* name, const uint8_t ble[6],
                                    const Color& base, const Color& shade,
                                    int8_t rssi) {
  uint32_t now = millis();
  uint8_t recoveredMac[6];
  meshMacFromBleAddr(ble, recoveredMac);
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t idx = findIndexLocked(recoveredMac);
  if (idx == count_) {
    evictOldestIfFullLocked(now);
    RosterEntry e;
    copyName(e.name, name);
    std::memcpy(e.mac, recoveredMac, 6);
    e.hasMac = true;
    e.baseColor = base;
    e.shadeColor = shade;
    e.lastSeenNearMs = now;
    if (rssi != -127) e.lastSeenBleMs = now;
    e.lastRssi = rssi;
    store_[count_++] = e;
  } else {
    // Empty name from a nameless BLE adv must not erase a known display name
    // on the merged entry.
    if (name[0] != '\0') copyName(store_[idx].name, name);
    store_[idx].baseColor = keepHelloWhite(base, store_[idx].baseColor);
    store_[idx].shadeColor = keepHelloWhite(shade, store_[idx].shadeColor);
    store_[idx].lastSeenNearMs = now;
    // RSSI freshens on every BLE adv (~30 Hz) so the closest-peer sort key
    // tracks current signal strength rather than a one-shot first-seen
    // value. -127 is the "unknown" sentinel; only overwrite when the
    // caller supplied a real reading.
    if (rssi != -127) {
      store_[idx].lastRssi = rssi;
      store_[idx].lastSeenBleMs = now;
    }
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
                                       int8_t rssi,
                                       lamp_protocol::LampVariant variant) {
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
    evictOldestIfFullLocked(now);
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
    e.variant = variant;
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
    // Unknown from BLE-only callers or older peers leaves a known variant intact.
    if (variant != lamp_protocol::LampVariant::Unknown) {
      store_[idx].variant = variant;
    }
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
    const uint32_t age = sinceLastSeen(store_[i].lastSeenNearMs, store_[i].lastSeenMeshMs, now);
    if (age != UINT32_MAX && age > maxAgeMs) {
      if (i != count_ - 1) store_[i] = store_[count_ - 1];
      count_--;
      generation_++;
      continue;
    }
    RosterEntry& e = store_[i];
    for (uint32_t* stamp : {&e.lastSeenNearMs, &e.lastSeenMeshMs, &e.lastSeenBleMs}) {
      if (*stamp != 0) *stamp = lampos::retireStamp(now, *stamp);
    }
    i++;
  }
  xSemaphoreGive(mutex_);
}

// Project store_ into a reserved buffer under lock; sort outside. Bounds the
// critical section so ESP-NOW recv-side bounded takes don't time out on a
// loop-task reader.
const std::vector<NearbyCopy>& LampRoster::snapshotNear(uint32_t maxAgeMs) {
  uint32_t now = millis();
  if (lastSnapshotValid_ && maxAgeMs == lastSnapshotMaxAge_ &&
      (now - lastSnapshotMs_) < kSnapshotCacheMs) {
    return nearSnapshot_;
  }
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  nearSnapshot_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < count_; i++) {
    if (isNearNow(store_[i].lastSeenNearMs, now, maxAgeMs)) {
      nearSnapshot_.push_back(toNearbyCopy(store_[i]));
    }
  }
  xSemaphoreGive(mutex_);
  // Highest RSSI first: front() is the nearest lamp. -127 sorts to the back.
  // std::sort (in-place introsort, no merge-buffer alloc) avoids bad_alloc on
  // the fragmented heap; the mac tiebreak gives equal-RSSI peers a
  // deterministic order.
  std::sort(nearSnapshot_.begin(), nearSnapshot_.end(),
            [](const NearbyCopy& a, const NearbyCopy& b) {
              if (a.lastRssi != b.lastRssi) return a.lastRssi > b.lastRssi;
              return std::memcmp(a.mac, b.mac, 6) < 0;
            });
  lastSnapshotMs_ = now;
  lastSnapshotMaxAge_ = maxAgeMs;
  lastSnapshotValid_ = true;
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return nearSnapshot_;
}

std::vector<MeshPeer>& LampRoster::getMesh(uint32_t maxAgeMs) {
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  uint32_t now = millis();
  meshBuf_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  for (size_t i = 0; i < count_; i++) {
    const RosterEntry& e = store_[i];
    if (e.lastSeenMeshMs != 0 && (now - e.lastSeenMeshMs) <= maxAgeMs) {
      meshBuf_.push_back(toMeshPeer(e));
    }
  }
  xSemaphoreGive(mutex_);
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return meshBuf_;
}

std::vector<ListedPeer>& LampRoster::getAll() {
  try {
    allBuf_.reserve(kCapacity);
  } catch (const std::bad_alloc&) {
    allBuf_.reserve(size());
  }
#ifdef LAMP_DEBUG
  debugGetterEnter();
#endif
  allBuf_.clear();
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // Capped at capacity so push_back never reallocates (and throws) under the
  // mutex; a peer added since the fallback reserve bumps generation and shows
  // on the next rebuild.
  for (size_t i = 0; i < count_ && allBuf_.size() < allBuf_.capacity(); i++) {
    allBuf_.push_back(toListedPeer(store_[i]));
  }
  xSemaphoreGive(mutex_);
#ifdef LAMP_DEBUG
  debugGetterExit();
#endif
  return allBuf_;
}

void LampRoster::releaseAll() { std::vector<ListedPeer>().swap(allBuf_); }

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

size_t LampRoster::size() {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  size_t n = count_;
  xSemaphoreGive(mutex_);
  return n;
}

}  // namespace lamp

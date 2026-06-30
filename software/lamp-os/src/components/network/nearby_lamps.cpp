#include "nearby_lamps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "util/proximity.hpp"

namespace lamp {

namespace {
// Derive the peer's BLE BD_ADDR from its ESP-NOW MAC using the ESP32
// silicon convention: BLE BD_ADDR = ESP-NOW (WiFi STA) MAC with +2 on
// the last byte. Holds for every ESP32 family chip in the fleet
// (WROOM, C6). Format matches addOrUpdateFromBle: canonical uppercase
// colon-hex.
std::string deriveBdAddrFromEspNowMac(const uint8_t mac[6]) {
  char buf[18];
  std::snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4],
                static_cast<uint8_t>(mac[5] + 2));
  return std::string(buf);
}

// Most-recent sighting across either transport — the eviction + prune
// sort key.
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
    e.firstSeenMs = now;  // stamp on first sighting (never overwritten later)
    store_.push_back(e);
  } else {
    // BD_ADDR is stable for an entry's lifetime — only set if previously
    // empty. Defensive: don't overwrite if a future code path supplies a
    // different address for the same name (would mean two peers share a
    // name; we keep the BD_ADDR of the first sighting).
    //
    // KNOWN EDGE CASE: if two distinct peers share a user-set name
    // (e.g. user names two lamps "stray"), the second peer's BD_ADDR
    // is silently dropped here — the entry's bdAddr stays as peer 1's,
    // and the disposition routes to peer 1. Acceptable trade-off:
    // re-keying NearbyLamps by BD_ADDR would break pre-mesh BLE-only
    // lamp compatibility (spec non-goal); name collisions are rare and
    // user-visible (the social tab would show only one row).
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
    // firstSeenMs is NEVER overwritten on subsequent sightings
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
  // Derive the BD_ADDR outside the lock — depends only on `mac`, no shared
  // state. snprintf + heap alloc happen here so the bounded critical
  // section below stays as short as possible.
  const std::string derivedBdAddr = deriveBdAddrFromEspNowMac(mac);
  // Bounded take: this runs on the ESP-NOW recv callback (WiFi task). A long
  // wait here stalls subsequent recv frames and the immediate
  // link_.broadcast() rebroadcast on the same task. 2 ms is well above
  // typical loop-task contention and well below any radio housekeeping
  // threshold. On timeout we silently drop the write — HELLO repeats every
  // 5 s, so the lamp is caught on the next beacon. Drop is preferable to a
  // recv-task stall.
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
  // lastRssi is sourced solely from BLE adv (see addOrUpdateFromBle +
  // nearby_lamps.hpp's lastRssi doc). The HELLO path's RSSI reading is
  // intentionally not consumed here to avoid cross-transport
  // contamination of PersonalityEngine's closest-peer hysteresis check.
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
    // Don't clobber a known version with a 0 — pre-HELLO BLE-only callers
    // pass the default; we only refresh once we actually got a HELLO.
    if (firmwareVersion != 0) store_[idx].firmwareVersion = firmwareVersion;
    // OTA state always overwrites — it's an instantaneous status flag,
    // not a "best-known" cumulative value, so the latest HELLO wins
    // outright. BLE-only callers pass 0 (idle) which is the correct
    // default for "I have no information."
    store_[idx].otaState = otaState;
    // Don't clobber a known protocolVersion with a 0 — same rationale
    // as firmwareVersion above (BLE-only callers default to 0).
    if (protocolVersion != 0) {
      store_[idx].protocolVersion = protocolVersion;
    }
    // Don't clobber a known channel with empty (older peers / BLE-only
    // callers pass nullptr); a peer's {type}-{channel} is stable.
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

// Reader pattern: take lock just long enough to copy store_ into a stack
// snapshot, then release before any filtering/allocation work. Keeps the
// critical section bounded by the vector copy itself (no per-element
// predicate evaluation inside the lock) so ESP-NOW recv-side bounded takes
// don't time out waiting on a loop-task reader.
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
  // Sort highest RSSI first so consumers can do `peers.front()` for the
  // physically nearest lamp. Comment in the header has long claimed this
  // is the contract (cascade-stagger sort key, PersonalityEngine's
  // closest-smitten pulse target) but the sort was never actually here —
  // peers came back in insertion order. -127 is the "unknown RSSI"
  // sentinel; those sort to the back. Stable sort so equal-RSSI peers
  // keep their original relative order (predictable across ticks).
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

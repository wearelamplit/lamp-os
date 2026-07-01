#include "nearby_lamps.hpp"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "components/network/wisp_claims_addr.hpp"
#include "util/base64.hpp"
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

void NearbyLamps::cacheWispHello(const uint8_t mac[6],
                                 uint32_t wispVersion,
                                 uint8_t flags,
                                 const char* paletteIdPrefix,
                                 const char* carriedFwChannel,
                                 uint32_t carriedFwVersion) {
  // Loop-task-only writer (Core 1 drain). portMAX_DELAY is safe because
  // the only contended reader is also on Core 1.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // MAC mismatch: clear stale status data so the next read doesn't merge
  // wisp-A's payload under wisp-B's identity.
  if (wispCache_.present && std::memcmp(wispCache_.mac, mac, 6) != 0) {
    wispCache_.lastStatusJson.clear();
    wispCache_.lastStatusMs = 0;
  }
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
  wispCache_.lastHelloMs = millis();
  wispCache_.wispVersion = wispVersion;
  wispCache_.flags = flags;
  // On-wire slots are 8 bytes, not NUL-terminated; add the NUL for safe logging.
  std::memcpy(wispCache_.paletteIdPrefix, paletteIdPrefix, 8);
  wispCache_.paletteIdPrefix[8] = '\0';
  std::memcpy(wispCache_.carriedFwChannel, carriedFwChannel, 8);
  wispCache_.carriedFwChannel[8] = '\0';
  wispCache_.carriedFwVersion = carriedFwVersion;
  xSemaphoreGive(mutex_);
}

WispCache NearbyLamps::getWispCache() {
  // Called from Core 0 (MSG_OVERRIDE_BRIGHTNESS recv). A long wait stalls
  // the recv task; on timeout return a "not present" snapshot so the floor
  // check drops the frame.
  WispCache snap;  // present=false by default
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return snap;
  }
  snap = wispCache_;
  xSemaphoreGive(mutex_);
  return snap;
}

void NearbyLamps::cacheWispStatus(const uint8_t mac[6],
                                  const char* json, size_t jsonLen) {
  // Loop-task-only writer (Core 1). portMAX_DELAY is safe; Core 0 only
  // reads via getWispStatusReadJson with a 2 ms bounded take.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  wispCache_.lastStatusJson.assign(json, jsonLen);
  wispCache_.lastStatusMs = millis();
  // Status alone asserts wisp presence if no hello has arrived.
  // MAC mismatch takes the new wisp and clears stale hello fields.
  if (!wispCache_.present || std::memcmp(wispCache_.mac, mac, 6) != 0) {
    std::memcpy(wispCache_.mac, mac, 6);
    wispCache_.present = true;
    // Clear stale hello fields so the next read doesn't merge
    // wisp-A's data under wisp-B's identity.
    wispCache_.wispVersion = 0;
    wispCache_.flags = 0;
    wispCache_.paletteIdPrefix[0] = '\0';
    wispCache_.carriedFwChannel[0] = '\0';
    wispCache_.carriedFwVersion = 0;
    wispCache_.lastHelloMs = 0;
  }
  xSemaphoreGive(mutex_);
}

void NearbyLamps::cacheWispPalette(const uint8_t mac[6],
                                    const uint8_t* rgb, uint8_t count) {
  // Loop-task-only writer (Core 1). portMAX_DELAY is safe; the BLE read
  // side (Core 0) uses a 2 ms bounded take.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // MAC mismatch: clear stale per-wisp data so the next BLE read doesn't
  // merge wisp-A's palette under wisp-B's identity.
  if (wispCache_.present && std::memcmp(wispCache_.mac, mac, 6) != 0) {
    wispCache_.lastStatusJson.clear();
    wispCache_.lastStatusMs = 0;
    wispCache_.lastHelloMs = 0;
    wispCache_.wispVersion = 0;
    wispCache_.flags = 0;
    wispCache_.paletteIdPrefix[0] = '\0';
    wispCache_.carriedFwChannel[0] = '\0';
    wispCache_.carriedFwVersion = 0;
  }
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
  static constexpr uint8_t kSlotMaxColors =
      static_cast<uint8_t>(sizeof(WispCache{}.manualPaletteRgb) / 3);
  const uint8_t safeCount = count > kSlotMaxColors ? kSlotMaxColors : count;
  if (safeCount > 0 && rgb) {
    std::memcpy(wispCache_.manualPaletteRgb, rgb,
                static_cast<size_t>(safeCount) * 3);
  }
  wispCache_.manualPaletteCount = safeCount;
  wispCache_.lastPaletteMs = millis();
  xSemaphoreGive(mutex_);
}

void NearbyLamps::cacheWispMacFromPaint(const uint8_t mac[6]) {
  // 2 ms matches getWispStatusReadJson's bounded take so a BLE read on
  // Core 0 doesn't starve behind this Core 1 paint update.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return;
  }
  // MAC mismatch: clear stale per-wisp data so the next BLE read doesn't
  // merge wisp-A's data under wisp-B's identity.
  if (wispCache_.present && std::memcmp(wispCache_.mac, mac, 6) != 0) {
    wispCache_.lastStatusJson.clear();
    wispCache_.lastStatusMs = 0;
    wispCache_.lastHelloMs = 0;
    wispCache_.wispVersion = 0;
    wispCache_.flags = 0;
    wispCache_.paletteIdPrefix[0] = '\0';
    wispCache_.carriedFwChannel[0] = '\0';
    wispCache_.carriedFwVersion = 0;
  }
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
  // hello-only fields (lastHelloMs, wispVersion, flags, paletteIdPrefix,
  // carriedFw*) not set; their zero defaults signal "no hello received."
  xSemaphoreGive(mutex_);
}

std::string NearbyLamps::getWispStatusReadJson(bool includePalette) {
  // BLE on-read callback (Core 0); bounded take so it doesn't block
  // behind a loop-task writer. On timeout return "{}".
  WispCache snap;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return std::string("{}");
  }
  snap = wispCache_;
  xSemaphoreGive(mutex_);

  // Local wisp-control state from ColorOverride, not from the cache.
  // On a fresh app connection this fires immediately even if no hello
  // has populated wispCache_ yet.
  const bool haveLampState = lampWispStateProvider_ != nullptr;
  LampWispState ws;
  if (haveLampState) {
    ws = lampWispStateProvider_();
  }
  const bool locallyControlling =
      haveLampState && (ws.controllingBase || ws.controllingShade);

  // "{}" on an empty return so the app's JSON decode succeeds with no
  // fields and shows "no wisp detected".
  if (!snap.present && snap.lastStatusJson.empty() && !locallyControlling) {
    return std::string("{}");
  }

  JsonDocument doc;
  // wispStatus payload is open-set and wisp-owned; start from it when present.
  if (!snap.lastStatusJson.empty()) {
    auto err = deserializeJson(doc, snap.lastStatusJson);
    if (err) {
      // Malformed payload: treat as empty to still serve hello-derived fields.
      doc.clear();
      doc.to<JsonObject>();
    }
  } else {
    doc.to<JsonObject>();
    doc["char"] = "wispStatus";
  }

  // Status-payload keys win; isNull() guard skips already-present fields.
  if (snap.present) {
    char macStr[18];
    std::snprintf(macStr, sizeof(macStr),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  snap.mac[0], snap.mac[1], snap.mac[2],
                  snap.mac[3], snap.mac[4], snap.mac[5]);
    if (doc["wispMac"].isNull())              doc["wispMac"] = macStr;
    if (snap.wispVersion != 0 && doc["wispVersion"].isNull()) {
      doc["wispVersion"] = snap.wispVersion;
    }
    if (snap.lastHelloMs != 0 && doc["helloFlags"].isNull()) {
      doc["helloFlags"] = snap.flags;
    }
    if (snap.paletteIdPrefix[0] != '\0' && doc["helloPaletteIdPrefix"].isNull()) {
      doc["helloPaletteIdPrefix"] = snap.paletteIdPrefix;
    }
    if (snap.lastHelloMs != 0 && doc["helloLastSeenMs"].isNull()) {
      doc["helloLastSeenMs"] = snap.lastHelloMs;
    }
  }
  if (snap.lastStatusMs != 0 && doc["statusLastSeenMs"].isNull()) {
    doc["statusLastSeenMs"] = snap.lastStatusMs;
  }

  // ws snapshot reused from above so early-return and this merge see the same value.
  if (haveLampState) {
    doc["controllingBase"]  = ws.controllingBase;
    doc["controllingShade"] = ws.controllingShade;
    if (!ws.baseWispColor.empty()) {
      doc["baseWispColor"] = ws.baseWispColor;
    }
    if (!ws.shadeWispColor.empty()) {
      doc["shadeWispColor"] = ws.shadeWispColor;
    }
  }

  // Palette omitted on the NOTIFY leg (MTU truncation would corrupt it);
  // served on READ only. paletteIdPrefix signals the app to re-read.
  if (includePalette && snap.manualPaletteCount > 0) {
    doc["palette"] = lamp::base64::encode(
        snap.manualPaletteRgb,
        static_cast<size_t>(snap.manualPaletteCount) * 3);
  }

  std::string out;
  serializeJson(doc, out);
  return out;
}

// Pairing lasts ≤60 s; claims older than this are meaningless.
static constexpr uint32_t kWispClaimStaleMs = 60000;

void NearbyLamps::cacheWispClaim(const uint8_t mac[6],
                                  const uint8_t lampMacs[][6], uint8_t count,
                                  uint32_t nowMs) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
  const uint8_t safeCount =
      count > lamp_protocol::kMaxWispClaimEntries
          ? static_cast<uint8_t>(lamp_protocol::kMaxWispClaimEntries)
          : count;
  if (safeCount > 0 && lampMacs) {
    std::memcpy(wispCache_.claimedLampMacs, lampMacs,
                static_cast<size_t>(safeCount) * 6);
  }
  wispCache_.claimedCount = safeCount;
  wispCache_.lastClaimMs = nowMs;
  xSemaphoreGive(mutex_);
}

size_t NearbyLamps::buildWispClaimsBlob(uint8_t* out, size_t outCap,
                                         uint32_t nowMs) {
  if (!out || outCap == 0) return 0;
  WispCache snap;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    out[0] = 0;
    return 1;
  }
  snap = wispCache_;
  xSemaphoreGive(mutex_);

  const bool stale = snap.lastClaimMs == 0 ||
                     (nowMs - snap.lastClaimMs) > kWispClaimStaleMs;
  const uint8_t count = stale ? 0 : snap.claimedCount;
  const size_t needed = 1 + static_cast<size_t>(count) * 6;
  if (needed > outCap) {
    out[0] = 0;
    return 1;
  }
  out[0] = count;
  for (uint8_t i = 0; i < count; ++i) {
    ble_control::bdAddrFromMeshMac(snap.claimedLampMacs[i], out + 1 + static_cast<size_t>(i) * 6);
  }
  return needed;
}

}  // namespace lamp

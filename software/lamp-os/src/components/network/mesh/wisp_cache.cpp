#include "wisp_cache.hpp"

#include "nearby_lamps.hpp"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "util/base64.hpp"
#include "components/network/mesh/wisp_claims_addr.hpp"

namespace lamp {

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

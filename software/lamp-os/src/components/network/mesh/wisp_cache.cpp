#include "wisp_cache.hpp"

#include "lamp_roster.hpp"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "util/base64.hpp"

namespace lamp {

// Display-slot stickiness: a rival wisp is rejected until the current
// wisp misses 6 of its 2 s WISP_HELLOs, so two overlapping wisps don't
// flip-flop the app's wisp view. Adoption stamps the same window
// (slotAdoptedMs), so a claim takeover holds through the loser's next hello.
static constexpr uint32_t kWispDisplayStaleMs = 12000;

// Painter freshness for the obey gates; matches the override watchdog so
// a wisp silent this long loses paint-hold and below-floor rights together.
static constexpr uint32_t kWispPainterFreshMs = 60000;

static bool freshWithin(uint32_t stampMs, uint32_t nowMs) {
  return stampMs != 0 && (nowMs - stampMs) <= kWispDisplayStaleMs;
}

static bool wispSlotFresh(const WispCache& cache, uint32_t nowMs) {
  return freshWithin(cache.lastHelloMs, nowMs) ||
         freshWithin(cache.slotAdoptedMs, nowMs);
}

void LampRoster::adoptWispLocked(const uint8_t mac[6], uint32_t nowMs) {
  wispCache_.lastStatusJson.clear();
  wispCache_.lastStatusMs = 0;
  wispCache_.lastHelloMs = 0;
  wispCache_.slotAdoptedMs = nowMs;
  wispCache_.wispVersion = 0;
  wispCache_.flags = 0;
  wispCache_.paletteIdPrefix[0] = '\0';
  wispCache_.carriedFwChannel[0] = '\0';
  wispCache_.carriedFwVersion = 0;
  wispCache_.manualPaletteCount = 0;
  wispCache_.lastPaletteMs = 0;
  wispFleet_.clear();
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
}

bool LampRoster::admitWispLocked(const uint8_t mac[6], uint32_t nowMs) {
  if (wispCache_.present && std::memcmp(wispCache_.mac, mac, 6) == 0) {
    return true;
  }
  if (wispCache_.present && wispSlotFresh(wispCache_, nowMs)) {
    return false;
  }
  adoptWispLocked(mac, nowMs);
  return true;
}

bool LampRoster::cacheWispHello(const uint8_t mac[6],
                                 uint32_t wispVersion,
                                 uint8_t flags,
                                 const char* paletteIdPrefix,
                                 const char* carriedFwChannel,
                                 uint32_t carriedFwVersion) {
  // Loop-task-only writer (Core 1 drain). portMAX_DELAY is safe because
  // the only contended reader is also on Core 1.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const uint32_t nowMs = millis();
  const bool wasPresent = wispCache_.present;
  uint8_t prevMac[6];
  std::memcpy(prevMac, wispCache_.mac, 6);
  if (!admitWispLocked(mac, nowMs)) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool presenceEdge = !wasPresent || std::memcmp(prevMac, mac, 6) != 0;
  wispCache_.lastHelloMs = nowMs;
  wispCache_.wispVersion = wispVersion;
  wispCache_.flags = flags;
  // On-wire slots are 8 bytes, not NUL-terminated; add the NUL for safe logging.
  std::memcpy(wispCache_.paletteIdPrefix, paletteIdPrefix, 8);
  wispCache_.paletteIdPrefix[8] = '\0';
  std::memcpy(wispCache_.carriedFwChannel, carriedFwChannel, 8);
  wispCache_.carriedFwChannel[8] = '\0';
  wispCache_.carriedFwVersion = carriedFwVersion;
  xSemaphoreGive(mutex_);
  return presenceEdge;
}

void LampRoster::cacheWispStatus(const uint8_t mac[6],
                                  const char* json, size_t jsonLen) {
  // Loop-task-only writer (Core 1). portMAX_DELAY is safe; Core 0 only
  // reads via getWispStatusReadJson with a 2 ms bounded take.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!admitWispLocked(mac, millis())) {
    xSemaphoreGive(mutex_);
    return;
  }
  wispCache_.lastStatusJson.assign(json, jsonLen);
  wispCache_.lastStatusMs = millis();
  xSemaphoreGive(mutex_);
}

void LampRoster::cacheWispPalette(const uint8_t mac[6],
                                    const uint8_t* rgbw, uint8_t count) {
  // Loop-task-only writer (Core 1). portMAX_DELAY is safe; the BLE read
  // side (Core 0) uses a 2 ms bounded take.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!admitWispLocked(mac, millis())) {
    xSemaphoreGive(mutex_);
    return;
  }
  static constexpr uint8_t kSlotMaxColors =
      static_cast<uint8_t>(sizeof(WispCache{}.manualPaletteRgbw) / 4);
  const uint8_t safeCount = count > kSlotMaxColors ? kSlotMaxColors : count;
  if (safeCount > 0 && rgbw) {
    std::memcpy(wispCache_.manualPaletteRgbw, rgbw,
                static_cast<size_t>(safeCount) * 4);
  }
  wispCache_.manualPaletteCount = safeCount;
  wispCache_.lastPaletteMs = millis();
  xSemaphoreGive(mutex_);
}

void LampRoster::cacheWispMacFromPaint(const uint8_t mac[6]) {
  // 2 ms matches getWispStatusReadJson's bounded take so a BLE read on
  // Core 0 doesn't starve behind this Core 1 paint update.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return;
  }
  const uint32_t nowMs = millis();
  painterPresent_ = true;
  std::memcpy(painterMac_, mac, 6);
  painterLastMs_ = nowMs;
  // Display slot only when empty or stale: being painted must not evict a
  // fresh display wisp, but the first BLE wispStatus read should still be
  // non-empty when paint arrives before any hello.
  admitWispLocked(mac, nowMs);
  xSemaphoreGive(mutex_);
}

bool LampRoster::isWispPainter(const uint8_t mac[6], uint32_t nowMs) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return false;
  }
  const bool painter = painterPresent_ &&
                       std::memcmp(painterMac_, mac, 6) == 0 &&
                       (nowMs - painterLastMs_) < kWispPainterFreshMs;
  xSemaphoreGive(mutex_);
  return painter;
}

bool LampRoster::isClaimingWisp(const uint8_t mac[6], uint32_t nowMs) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return false;
  }
  const bool claiming = wispCache_.present &&
                        std::memcmp(wispCache_.mac, mac, 6) == 0 &&
                        wispSlotFresh(wispCache_, nowMs);
  xSemaphoreGive(mutex_);
  return claiming;
}

bool LampRoster::touchWispPainter(const uint8_t mac[6], uint32_t nowMs) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return false;
  }
  const bool painter = painterPresent_ &&
                       std::memcmp(painterMac_, mac, 6) == 0 &&
                       (nowMs - painterLastMs_) < kWispPainterFreshMs;
  if (painter) painterLastMs_ = nowMs;
  xSemaphoreGive(mutex_);
  return painter;
}

std::string LampRoster::getWispStatusReadJson(bool includePalette) {
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
  // paletteBpp is the blob's explicit stride discriminator; the app must
  // never infer it from length (ambiguous at len % 12 == 0).
  if (includePalette && snap.manualPaletteCount > 0) {
    doc["palette"] = lamp::base64::encode(
        snap.manualPaletteRgbw,
        static_cast<size_t>(snap.manualPaletteCount) * 4);
    doc["paletteBpp"] = 4;
  }

  std::string out;
  serializeJson(doc, out);
  return out;
}

void LampRoster::cacheWispClaim(const uint8_t mac[6],
                                  const uint8_t lampMacs[][6], uint8_t count,
                                  const uint8_t selfMac[6], uint32_t nowMs) {
  const uint8_t safeCount =
      count > lamp_protocol::kMaxWispClaimEntries
          ? static_cast<uint8_t>(lamp_protocol::kMaxWispClaimEntries)
          : count;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const bool rival = wispCache_.present &&
                     std::memcmp(wispCache_.mac, mac, 6) != 0 &&
                     wispSlotFresh(wispCache_, nowMs);
  if (rival) {
    // A rival claiming this lamp takes the slot only when the current
    // wisp does not; boundary lamps otherwise stay with first-heard.
    bool claimsUs = false;
    for (uint8_t i = 0; i < safeCount && lampMacs; ++i) {
      if (std::memcmp(lampMacs[i], selfMac, 6) == 0) {
        claimsUs = true;
        break;
      }
    }
    if (!claimsUs || wispFleet_.containsClaim(selfMac, nowMs)) {
      xSemaphoreGive(mutex_);
      return;
    }
    adoptWispLocked(mac, nowMs);
  } else {
    admitWispLocked(mac, nowMs);
  }
  wispFleet_.upsertClaims(lampMacs, safeCount, nowMs);
  xSemaphoreGive(mutex_);
}

void LampRoster::cacheWispPaint(const uint8_t srcMac[6],
                                  const uint8_t* entries, uint8_t count,
                                  uint32_t nowMs) {
  const uint8_t safeCount =
      count > lamp_protocol::WISP_PAINT_MAX_ENTRIES
          ? static_cast<uint8_t>(lamp_protocol::WISP_PAINT_MAX_ENTRIES)
          : count;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!admitWispLocked(srcMac, nowMs)) {
    xSemaphoreGive(mutex_);
    return;
  }
  wispFleet_.upsertPaints(entries, safeCount, nowMs);
  xSemaphoreGive(mutex_);
}

size_t LampRoster::buildWispClaimsBlob(uint8_t* out, size_t outCap,
                                         uint32_t nowMs) {
  if (!out || outCap == 0) return 0;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    out[0] = 0;
    return 1;
  }
  const size_t n = wispFleet_.buildClaimsBlob(out, outCap, nowMs);
  xSemaphoreGive(mutex_);
  return n;
}

}  // namespace lamp

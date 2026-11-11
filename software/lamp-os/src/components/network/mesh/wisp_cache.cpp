#include "wisp_cache.hpp"

#include "lamp_roster.hpp"
#include "pending_slots.hpp"
#include "wisp_status_merge.hpp"

#include <cstring>
#include <utility>

#include <lampos/clock.hpp>

namespace lamp {

// Display-slot stickiness: a rival wisp is rejected until the current
// wisp misses 6 of its 2 s WISP_HELLOs, so two overlapping wisps don't
// flip-flop the app's wisp view. Adoption stamps the same window
// (slotAdoptedMs), so a claim takeover holds through the loser's next hello.
static constexpr uint32_t kWispDisplayStaleMs = 12000;

// Control-op targeting tolerates a longer gap than slot stickiness: the unicast
// to the wisp is reliable, so a command must still send through a run of
// coex-dropped broadcasts. Comfortably exceeds observed HELLO gaps.
static constexpr uint32_t kWispControlStaleMs = 30000;

static bool freshWithin(uint32_t stampMs, uint32_t nowMs, uint32_t windowMs) {
  return stampMs != 0 && (nowMs - stampMs) <= windowMs;
}

static bool wispSlotFresh(const WispCache& cache, uint32_t nowMs) {
  return !cache.displayAgedOut &&
         (freshWithin(cache.lastHelloMs, nowMs, kWispDisplayStaleMs) ||
          freshWithin(cache.slotAdoptedMs, nowMs, kWispDisplayStaleMs));
}

static bool recentWithin(uint32_t stampMs, uint32_t nowMs, uint32_t windowMs) {
  return stampMs != 0 && lampos::msSince(nowMs, stampMs) <= windowMs;
}

void LampRoster::adoptWispLocked(const uint8_t mac[6], uint32_t nowMs) {
  wispCache_.lastStatusJson.clear();
  wispCache_.lastStatusMs = 0;
  wispCache_.lastConfigJson.clear();
  wispCache_.lastConfigMs = 0;
  wispCache_.lastHelloMs = 0;
  wispCache_.slotAdoptedMs = nowMs;
  wispCache_.lastSeenMs = nowMs;
  wispCache_.displayAgedOut = false;
  wispCache_.controlAgedOut = false;
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
    wispCache_.lastSeenMs = nowMs;
    wispCache_.controlAgedOut = false;
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
  wispCache_.displayAgedOut = false;
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

bool LampRoster::cacheWispConfig(const uint8_t mac[6],
                                 const char* json, size_t jsonLen) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!admitWispLocked(mac, millis())) {
    xSemaphoreGive(mutex_);
    return false;
  }
  const bool changed = storeIfChanged(wispCache_.lastConfigJson, json, jsonLen);
  if (changed) wispCache_.lastConfigMs = millis();
  xSemaphoreGive(mutex_);
  return changed;
}

void LampRoster::copyWispConfigJson(std::string& out) {
  // BLE page-section open (Core 0); bounded take like the status read.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    out.assign("{}");
    return;
  }
  if (wispCache_.lastConfigJson.empty()) {
    out.assign("{}");
  } else {
    out.assign(wispCache_.lastConfigJson);
  }
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
  // Display slot only when empty or stale: being painted must not evict a
  // fresh display wisp, but the first BLE wispStatus read should still be
  // non-empty when paint arrives before any hello.
  admitWispLocked(mac, millis());
  xSemaphoreGive(mutex_);
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

bool LampRoster::copyDisplayWispMac(uint8_t out[6], uint32_t nowMs) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return false;
  }
  const bool have = wispCache_.present && !wispCache_.controlAgedOut &&
                    freshWithin(wispCache_.lastSeenMs, nowMs, kWispControlStaleMs);
  if (have) std::memcpy(out, wispCache_.mac, 6);
  xSemaphoreGive(mutex_);
  return have;
}

void LampRoster::retireStaleWisp(uint32_t nowMs) {
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!recentWithin(wispCache_.lastHelloMs, nowMs, kWispDisplayStaleMs) &&
      !recentWithin(wispCache_.slotAdoptedMs, nowMs, kWispDisplayStaleMs)) {
    wispCache_.displayAgedOut = true;
  }
  if (!recentWithin(wispCache_.lastSeenMs, nowMs, kWispControlStaleMs)) {
    wispCache_.controlAgedOut = true;
  }
  wispFleet_.retire(nowMs);
  xSemaphoreGive(mutex_);
}

std::string LampRoster::getWispStatusReadJson(size_t maxLen) {
  // BLE on-read callback (Core 0); bounded take so it doesn't block
  // behind a loop-task writer. On timeout return "{}".
  WispStatusMergeInput in;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return std::string("{}");
  }
  in.present = wispCache_.present;
  std::memcpy(in.mac, wispCache_.mac, 6);
  in.wispVersion = wispCache_.wispVersion;
  in.flags = wispCache_.flags;
  in.lastHelloMs = wispCache_.lastHelloMs;
  std::memcpy(in.helloPaletteIdPrefix, wispCache_.paletteIdPrefix,
              sizeof(in.helloPaletteIdPrefix));
  in.statusJson = wispCache_.lastStatusJson;
  in.lastStatusMs = wispCache_.lastStatusMs;
  in.lastConfigMs = wispCache_.lastConfigMs;
  xSemaphoreGive(mutex_);

  // Local wisp-control state from ColorOverride, not from the cache, so a
  // fresh app connection sees it even before any hello has been cached.
  if (lampWispStateProvider_ != nullptr) {
    LampWispState ws = lampWispStateProvider_();
    in.haveLampState = true;
    in.controllingBase = ws.controllingBase;
    in.controllingShade = ws.controllingShade;
    in.baseWispColor = std::move(ws.baseWispColor);
    in.shadeWispColor = std::move(ws.shadeWispColor);
  }
  return mergeWispStatusJson(in, maxLen);
}

size_t LampRoster::copyManualPaletteBlob(uint8_t* out, size_t cap) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) return 0;
  size_t n = static_cast<size_t>(wispCache_.manualPaletteCount) * 4;
  if (n > cap) n = cap;
  std::memcpy(out, wispCache_.manualPaletteRgbw, n);
  xSemaphoreGive(mutex_);
  return n;
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

void LampRoster::cacheWispState(const PendingWispState& slot, uint32_t nowMs) {
  const uint8_t safeCount =
      slot.count > WispFleetCache::kCapacity
          ? static_cast<uint8_t>(WispFleetCache::kCapacity)
          : slot.count;
  xSemaphoreTake(mutex_, portMAX_DELAY);
  if (!admitWispLocked(slot.sourceMac, nowMs)) {
    xSemaphoreGive(mutex_);
    return;
  }
  const lamp_protocol::WispDriftFade& own = slot.fadeRecord;
  for (uint8_t i = 0; i < safeCount; ++i) {
    const uint8_t* e = &slot.entries[i * lamp_protocol::WISP_STATE_ENTRY_SIZE];
    const uint8_t* w = &slot.w[i * lamp_protocol::WISP_STATE_W_SIZE];
    const bool targeted = slot.entryTargeted(i);
    if (i == slot.ownIndex && targeted) {
      wispFleet_.upsertPaint(e, own.base, own.shade, nowMs, false);
      continue;
    }
    const uint8_t base[4] = {e[6], e[7], e[8], w[0]};
    const uint8_t shade[4] = {e[9], e[10], e[11], w[1]};
    // A partial frame leaves some fading lamps out; theirs is the in-between.
    wispFleet_.upsertPaint(e, base, shade, nowMs,
                           slot.fadePartial && !targeted);
  }
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

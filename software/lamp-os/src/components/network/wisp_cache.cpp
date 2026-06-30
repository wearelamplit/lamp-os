#include "wisp_cache.hpp"

#include "nearby_lamps.hpp"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "util/base64.hpp"

namespace lamp {

void NearbyLamps::cacheWispHello(const uint8_t mac[6],
                                 uint32_t wispVersion,
                                 uint8_t flags,
                                 const char* paletteIdPrefix,
                                 const char* carriedFwChannel,
                                 uint32_t carriedFwVersion) {
  // Loop-task-only writer; the WiFi recv path memcpys into a typed pending
  // slot and the drain calls this on Core 1. portMAX_DELAY is fine here
  // because the only contended reader is also on Core 1 (we never hold
  // this mutex from Core 0). See the addOrUpdate paths above for the
  // bounded-take pattern when the writer is Core 0.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // Different wisp — drop stale status data so the next read doesn't merge
  // wisp-A's wispStatus payload under wisp-B's MAC. Status fields will
  // refresh on the next MSG_CONTROL_OP wispStatus broadcast from this wisp
  // (≤30s heartbeat). Mirror of the same guard in cacheWispStatus.
  if (wispCache_.present && std::memcmp(wispCache_.mac, mac, 6) != 0) {
    wispCache_.lastStatusJson.clear();
    wispCache_.lastStatusMs = 0;
  }
  std::memcpy(wispCache_.mac, mac, 6);
  wispCache_.present = true;
  wispCache_.lastHelloMs = millis();
  wispCache_.wispVersion = wispVersion;
  wispCache_.flags = flags;
  // 8-byte fixed-width on-wire slots — copy as bytes, then ensure the
  // trailing NUL for safe logging. The caller's source pointers are NOT
  // NUL-terminated.
  std::memcpy(wispCache_.paletteIdPrefix, paletteIdPrefix, 8);
  wispCache_.paletteIdPrefix[8] = '\0';
  std::memcpy(wispCache_.carriedFwChannel, carriedFwChannel, 8);
  wispCache_.carriedFwChannel[8] = '\0';
  wispCache_.carriedFwVersion = carriedFwVersion;
  xSemaphoreGive(mutex_);
}

WispCache NearbyLamps::getWispCache() {
  // Bounded take: MeshLink's MSG_OVERRIDE_BRIGHTNESS branch on the
  // WiFi recv task (Core 0) reads this synchronously to decide whether
  // a below-floor brightness is wisp-paired. A long wait would stall the
  // recv task; on contention we return a "not present" snapshot — the
  // floor check then drops the suspect frame, which is the safe default.
  // Loop-task callers (the wispHello drain) take their own write side
  // with portMAX_DELAY; the only contention here is brief.
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
  // Loop-task-only writer (drain of pendingWispStatus on Core 1). The
  // wispHello side uses portMAX_DELAY for the same reason — Core 0
  // never takes the write side, so contention is only brief loop-task
  // reads (BLE notify build path via getWispStatusReadJson).
  xSemaphoreTake(mutex_, portMAX_DELAY);
  wispCache_.lastStatusJson.assign(json, jsonLen);
  wispCache_.lastStatusMs = millis();
  // If we hadn't seen a hello yet, the status broadcast still pins the
  // wisp's identity. If the cached mac belongs to a different (older)
  // wisp, take the new one — single-slot semantics in v1 mirror what
  // cacheWispHello does implicitly when a fresh hello overwrites mac.
  if (!wispCache_.present || std::memcmp(wispCache_.mac, mac, 6) != 0) {
    std::memcpy(wispCache_.mac, mac, 6);
    wispCache_.present = true;
    // Different wisp — drop stale hello data so the next read doesn't
    // merge wisp-A's hello fields under wisp-B's MAC. Hello fields will
    // refresh on the next MSG_WISP_HELLO from this wisp.
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
  // Loop-task-only writer (drain of pendingWispPalette on Core 1). Same
  // mutex pattern as cacheWispStatus: portMAX_DELAY take because the BLE
  // read side (Core 0) uses a bounded 2 ms take, so the writer never
  // starves the reader for long.
  xSemaphoreTake(mutex_, portMAX_DELAY);
  // Different wisp — drop stale per-wisp data so the next BLE read doesn't
  // merge wisp-A's palette under wisp-B's MAC. Mirrors the mac-mismatch
  // branch in cacheWispStatus.
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
  // Clamp at the cache slot capacity (150 B / 3 = 50 colors). The wisp
  // builder also caps at kMaxWispPaletteColors, but defending here keeps
  // the cache safe against a future protocol bump.
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
  // Bounded take: called from the loop-task drain of pendingOverrideColors
  // on Core 1 (see lamp.cpp's OVERRIDE_COLORS branch). Loop-task
  // callers normally use portMAX_DELAY, but the BLE on-read of
  // CHAR_WISP_STATUS runs on Core 0 and takes the same mutex via
  // getWispStatusReadJson's 2 ms bounded-take. Match that 2 ms here so a
  // contended BLE read doesn't get starved by the drain's wisp-paint
  // update. On timeout we drop the update — the next wisp-sourced paint
  // frame will retry.
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return;
  }
  // Same single-slot semantics as cacheWispStatus's mac-mismatch branch:
  // a different wisp invalidates the stale per-wisp data so the next
  // BLE read doesn't merge wisp-A's hello/status under wisp-B's MAC.
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
  // Intentionally NOT touching lastHelloMs / wispVersion / flags /
  // paletteIdPrefix / carriedFw* — those are hello-only fields. The
  // merge in getWispStatusReadJson skips them when their backing
  // timestamps are zero, so leaving them at defaults is fine.
  xSemaphoreGive(mutex_);
}

std::string NearbyLamps::getWispStatusReadJson(bool includePalette) {
  // Bounded take matches getWispCache — a BLE on-read callback runs on
  // Core 0 and can't afford to block behind a long writer. The loop-task
  // writer holds the mutex only for the snapshot copy below, so 2 ms is
  // plenty in practice; on timeout we hand back "{}" rather than stall
  // the GATT response.
  WispCache snap;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(2)) != pdTRUE) {
    return std::string("{}");
  }
  snap = wispCache_;
  xSemaphoreGive(mutex_);

  // Take the lamp's local wisp-control state up front. This is the
  // LOCAL ground truth (driven by ColorOverride.isWispActive) — the
  // lamp knows whether it's actively being wisp-painted right now,
  // regardless of whether a hello/status broadcast has populated
  // wispCache_ yet. Without including it in the empty-cache short-
  // circuit, a fresh app connection to a wisp-painted lamp shows
  // "no wisp" for up to 2 s (the wisp's hello interval) before the
  // indicator pops on. With it, the indicator fires immediately.
  const bool haveLampState = lampWispStateProvider_ != nullptr;
  LampWispState ws;
  if (haveLampState) {
    ws = lampWispStateProvider_();
  }
  const bool locallyControlling =
      haveLampState && (ws.controllingBase || ws.controllingShade);

  // Truly nothing — no cached hello/status AND the lamp itself isn't
  // being wisp-painted right now. Empty object so the app's JSON
  // decode succeeds with no fields and renders the "no wisp detected"
  // state.
  if (!snap.present && snap.lastStatusJson.empty() && !locallyControlling) {
    return std::string("{}");
  }

  JsonDocument doc;
  // Start from the cached wispStatus payload when we have one — its
  // shape is open-set and the wisp owns it. If absent, build from hello
  // fields alone so the app at least sees basic visibility.
  if (!snap.lastStatusJson.empty()) {
    auto err = deserializeJson(doc, snap.lastStatusJson);
    if (err) {
      // Malformed cached payload — treat as empty so we still serve the
      // hello-derived fields below rather than handing back garbage.
      doc.clear();
      doc.to<JsonObject>();
    }
  } else {
    doc.to<JsonObject>();
    doc["char"] = "wispStatus";
  }

  // If the wisp's status payload already contains any of these keys,
  // the wisp-authored value wins — the isNull() guard skips the merge.
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

  // Lamp-side wisp control snapshot — drives the app's will-o'-wisp
  // indicator and disabledDuringWispOverride expression gating. Reuses
  // the `ws` snapshot taken at the top so the early-return check and
  // this merge see the same value (no second provider call mid-build).
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

  // The full palette is too large for the NOTIFY leg (MTU truncation
  // silently corrupts wispStatus), so it is served only on the READ leg,
  // which long-reads the full value. The NOTIFY path passes
  // includePalette=false and carries paletteIdPrefix as the "re-read me"
  // signal.
  if (includePalette && snap.manualPaletteCount > 0) {
    doc["palette"] = lamp::base64::encode(
        snap.manualPaletteRgb,
        static_cast<size_t>(snap.manualPaletteCount) * 3);
  }

  std::string out;
  serializeJson(doc, out);
  return out;
}

}  // namespace lamp

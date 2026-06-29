#include "StatusBeacon.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>

#include <cstring>

#include "CurrentPalette.h"
#include "MeshLink.h"
#include "PaintDistributor.h"
#include "WispConfig.h"
#include "WispRoster.h"
#include "WispZoneSelector.h"
#include "aurora/AuroraPaletteClient.h"
#include "lamp_protocol.hpp"

namespace wisp {

namespace {

// FF:FF:FF:FF:FF:FF broadcast target, matching every other broadcast
// CONTROL_OP on the wire.
constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Worst-case wispStatus JSON fits under CONTROL_MAX_PAYLOAD for the current
// few-zone setup; the runtime guard below (jsonLen > cap → drop + log) is the
// real defense (three-digit zone ids would push past it).
constexpr size_t kPayloadCap = lamp_protocol::CONTROL_MAX_PAYLOAD;

// Palette-id prefix length; matches the MSG_WISP_HELLO slot so the app sees
// the same id from both paths.
constexpr size_t kPaletteIdPrefixLen = lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN;

}  // namespace

void StatusBeacon::begin(MeshLink* mesh, PaintDistributor* paint,
                         CurrentPalette* palette, ZoneSelector* zone,
                         AuroraPaletteClient* aurora,
                         WispConfig* config,
                         WispRoster* roster) {
  mesh_ = mesh;
  paint_ = paint;
  palette_ = palette;
  zone_ = zone;
  aurora_ = aurora;
  config_ = config;
  roster_ = roster;
}

void StatusBeacon::startTimer() {
  if (!timer_) {
    timer_ = xTimerCreate(
        "wisp_hello",
        pdMS_TO_TICKS(kHelloIntervalMs),
        pdTRUE,  // auto-reload
        this,
        [](TimerHandle_t t) {
          auto* self = static_cast<StatusBeacon*>(pvTimerGetTimerID(t));
          if (self) self->emit();
        });
    if (timer_) {
      xTimerStart(timer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(hello) failed");
    }
  }

  if (!statusTimer_) {
    statusTimer_ = xTimerCreate(
        "wisp_status",
        pdMS_TO_TICKS(kStatusIntervalMs),
        pdTRUE,  // auto-reload
        this,
        [](TimerHandle_t t) {
          auto* self = static_cast<StatusBeacon*>(pvTimerGetTimerID(t));
          if (self) self->emitStatus();
        });
    if (statusTimer_) {
      xTimerStart(statusTimer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(status) failed");
    }
  }
}

void StatusBeacon::triggerOnChange() {
  // Emit first, then reschedule: emitting before the reset captures a
  // consistent snapshot before re-zeroing the 30s clock.
  emitStatus();
  if (statusTimer_) {
    // xTimerReset from the loop task is safe; uses the timer command queue.
    xTimerReset(statusTimer_, 0);
  }
}

void StatusBeacon::emit() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // Snapshot wifi/aurora once so the same values feed both the HELLO frame
  // and the on-change diff below; re-reading could race and yield a stale
  // diff.
  const bool wifiNow   = WiFi.isConnected();
  const bool auroraNow = aurora_ && aurora_->isStreaming();
  uint8_t flags = 0;
  if (paint_ && paint_->paintMode()) {
    flags |= lamp_protocol::WISP_HELLO_FLAG_PAINT_MODE;
  }
  if (wifiNow) {
    flags |= lamp_protocol::WISP_HELLO_FLAG_WIFI_CONNECTED;
  }
  if (auroraNow) {
    flags |= lamp_protocol::WISP_HELLO_FLAG_AURORA_CONNECTED;
  }

  // paletteIdPrefix via the mux-guarded snapshot so this timer-task read
  // doesn't race a loop-task CurrentPalette::update().
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN] = {0};
  size_t paletteIdPrefixLen = 0;
  if (palette_) {
    paletteIdPrefixLen = palette_->copyPaletteIdPrefix(
        paletteIdPrefix, sizeof(paletteIdPrefix));
  }

  // carriedFw* zero-fill: the wisp doesn't distribute firmware. Wire layout
  // retained so older lamps reading these fields don't malform-drop the HELLO.
  const char* carriedFwChannel = nullptr;
  const size_t carriedFwChannelLen = 0;
  const uint32_t carriedFwVersion = 0;

  uint8_t buf[lamp_protocol::WISP_HELLO_FIXED_SIZE];
  size_t n = 0;
  uint16_t seq = 0;
  STATUS_BEACON_PORTMUX_ENTER(&emitMux_);
  seq = seqCounter_++;
  n = lamp_protocol::buildWispHello(
      buf, sizeof(buf), seq,
      srcMac, kWispVersion, flags,
      paletteIdPrefix, paletteIdPrefixLen,
      carriedFwChannel, carriedFwChannelLen,
      carriedFwVersion);
  STATUS_BEACON_PORTMUX_EXIT(&emitMux_);
  if (!n) return;
  // broadcast() ends in esp_now_send (queued), safe outside the mux; seq is
  // already committed.
  mesh_->broadcast(buf, n);

  // MSG_WISP_CLAIM: broadcast the claim set so peers build the shared view.
  // Same 2s cadence as HELLO, gossip-relayed the same way. Roster is optional
  // during init.
  if (roster_) {
    uint8_t claimEntries[lamp_protocol::kMaxWispClaimEntries *
                         lamp_protocol::WISP_CLAIM_ENTRY_SIZE] = {0};
    const size_t entryCount = roster_->snapshotClaimsForBroadcast(
        claimEntries, sizeof(claimEntries));
    uint8_t claimBuf[lamp_protocol::WISP_CLAIM_MAX_SIZE];
    uint16_t claimSeq = 0;
    STATUS_BEACON_PORTMUX_ENTER(&emitMux_);
    claimSeq = seqCounter_++;
    STATUS_BEACON_PORTMUX_EXIT(&emitMux_);
    const size_t claimLen = lamp_protocol::buildWispClaim(
        claimBuf, sizeof(claimBuf), claimSeq, srcMac,
        entryCount > 0 ? claimEntries : nullptr, entryCount);
    if (claimLen) {
      mesh_->broadcast(claimBuf, claimLen);
    }
  }

  // On-change trigger for passive WiFi/Aurora flips. The 2s HELLO timer is
  // the only fast-cadence observer of radio state; without this diff a
  // connect/disconnect waits up to 30s for the next heartbeat. emitStatus()
  // takes its own portMUX and is re-entrant-safe here.
  const bool helloFlagsChanged =
      haveLastHelloConn_ &&
      (wifiNow != lastHelloWifi_ || auroraNow != lastHelloAurora_);
  lastHelloWifi_     = wifiNow;
  lastHelloAurora_   = auroraNow;
  haveLastHelloConn_ = true;
  if (helloFlagsChanged) {
    emitStatus();
    if (statusTimer_) xTimerReset(statusTimer_, 0);
  }
}

void StatusBeacon::emitStatus() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // Snapshot fields before the JSON build for a consistent emission. Reading
  // these outside the lock keeps the mux off external library calls.
  const bool wifiConn   = WiFi.isConnected();
  const bool auroraConn = aurora_ && aurora_->isStreaming();
  const int  currentZone = zone_ ? zone_->currentZone() : -1;
  const char* zoneSrc    = zone_ ? zoneSourceName(zone_->source())
                                 : zoneSourceName(ZoneSource::None);

  // Snapshot observedZones via the mux-guarded accessor. A reference to the
  // vector would race ZoneSelector::observe() (push_back relocates, erase
  // invalidates iterators). Buffer sized to kMaxObservedZones for the full set.
  int obsBuf[kMaxObservedZones];
  size_t obsCount = 0;
  if (zone_) {
    obsCount = zone_->copyObserved(obsBuf, kMaxObservedZones);
  }

  // paletteIdPrefix snapshots under the CurrentPalette mux so this timer-task
  // call won't tear against a loop-task update().
  char paletteIdPrefix[kPaletteIdPrefixLen + 1] = {0};
  if (palette_) {
    const size_t n = palette_->copyPaletteIdPrefix(paletteIdPrefix,
                                                   kPaletteIdPrefixLen);
    paletteIdPrefix[n] = '\0';
  }

  const uint32_t lastSeenMs = millis();

  // Source-mode stringified so the app doesn't dual-decode int/string forms;
  // older apps ignore the field. manualPalette is NOT included: it would push
  // the JSON past CONTROL_MAX_PAYLOAD once observedZones is non-trivial (it
  // rides MSG_WISP_PALETTE instead).
  const char* sourceName = "aurora";  // safe default for nullptr config
  if (config_) {
    switch (config_->sourceMode()) {
      case WispSourceMode::Off:    sourceName = "off";    break;
      case WispSourceMode::Manual: sourceName = "manual"; break;
      case WispSourceMode::Aurora: sourceName = "aurora"; break;
    }
  }

  // JsonDocument on the stack; fields are small.
  JsonDocument doc;
  doc["char"]            = "wispStatus";
  doc["currentZone"]     = currentZone;
  doc["zoneSource"]      = zoneSrc;
  JsonArray zonesArr     = doc["observedZones"].to<JsonArray>();
  for (size_t i = 0; i < obsCount; ++i) zonesArr.add(obsBuf[i]);
  doc["wifiConnected"]   = wifiConn;
  doc["auroraConnected"] = auroraConn;
  doc["paletteIdPrefix"] = paletteIdPrefix;
  doc["lastSeenMs"]      = lastSeenMs;
  doc["source"]          = sourceName;
  // Off-mode ring color (3 ints). Small enough to ride alongside the other
  // fields. Defaults baked into WispConfig so an upgraded wisp emits sensible
  // bytes.
  if (config_) {
    const auto off = config_->offColor();
    JsonArray offArr = doc["offColor"].to<JsonArray>();
    offArr.add(off.r);
    offArr.add(off.g);
    offArr.add(off.b);
  }

  char jsonBuf[kStatusJsonBufLen];
  const size_t jsonLen = serializeJson(doc, jsonBuf, sizeof(jsonBuf));
  if (jsonLen == 0 || jsonLen > kPayloadCap) {
    Serial.printf("[wisp.beacon] wispStatus JSON oversize: %u (cap %u)\n",
                  (unsigned)jsonLen, (unsigned)kPayloadCap);
    return;
  }

  // Update the diff cache + log transitions. Transitions only trigger a
  // log line; the broadcast itself is unconditional on the timer tick.
  if (!haveLastConnState_) {
    haveLastConnState_ = true;
  } else {
    if (wifiConn != lastWifiConnected_) {
      Serial.printf("[wisp.beacon] wifiConnected: %d -> %d\n",
                    (int)lastWifiConnected_, (int)wifiConn);
    }
    if (auroraConn != lastAuroraConnected_) {
      Serial.printf("[wisp.beacon] auroraConnected: %d -> %d\n",
                    (int)lastAuroraConnected_, (int)auroraConn);
    }
  }
  lastWifiConnected_   = wifiConn;
  lastAuroraConnected_ = auroraConn;

  // Build CONTROL_OP frame around the JSON. seq under the mux so a parallel
  // emit() / emitStatus() can't share a seq.
  uint8_t frame[lamp_protocol::CONTROL_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  STATUS_BEACON_PORTMUX_ENTER(&emitMux_);
  seq = seqCounter_++;
  frameLen = lamp_protocol::buildControlOp(
      frame, sizeof(frame), seq,
      kBroadcastMac, srcMac,
      reinterpret_cast<const uint8_t*>(jsonBuf), jsonLen);
  STATUS_BEACON_PORTMUX_EXIT(&emitMux_);
  if (!frameLen) {
    Serial.println("[wisp.beacon] buildControlOp(wispStatus) failed");
    return;
  }
  mesh_->broadcast(frame, frameLen);

  // Piggyback the manualPalette broadcast on the same wispStatus tick.
  // emitPalette() is broken out so WispOpDispatcher can also call it on a
  // setManualPalette write for sub-30-s convergence.
  emitPalette();
}

void StatusBeacon::emitPalette() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // manualPalette() lives in WispConfig and is mutated only on the loop task,
  // so a plain copy here is safe.
  static thread_local uint8_t rgb[lamp_protocol::kMaxWispPaletteColors *
                                  lamp_protocol::WISP_PALETTE_ENTRY_SIZE];
  size_t count = 0;
  if (config_) {
    const auto& palette = config_->manualPalette();
    const size_t available = palette.size();
    if (available > lamp_protocol::kMaxWispPaletteColors) {
      // Truncation caps the app's view at kMaxWispPaletteColors (the wisp
      // keeps painting from the full local palette). Log once per burst.
      static bool truncWarned = false;
      if (!truncWarned) {
        Serial.printf("[wisp.beacon] manualPalette truncated: %u -> %u\n",
                      (unsigned)available,
                      (unsigned)lamp_protocol::kMaxWispPaletteColors);
        truncWarned = true;
      }
      count = lamp_protocol::kMaxWispPaletteColors;
    } else {
      count = available;
    }
    for (size_t i = 0; i < count; ++i) {
      rgb[i * 3 + 0] = palette[i].r;
      rgb[i * 3 + 1] = palette[i].g;
      rgb[i * 3 + 2] = palette[i].b;
    }
  }

  uint8_t frame[lamp_protocol::WISP_PALETTE_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  STATUS_BEACON_PORTMUX_ENTER(&emitMux_);
  seq = seqCounter_++;
  frameLen = lamp_protocol::buildWispPalette(
      frame, sizeof(frame), seq, srcMac,
      count > 0 ? rgb : nullptr, count);
  STATUS_BEACON_PORTMUX_EXIT(&emitMux_);
  if (!frameLen) {
    Serial.println("[wisp.beacon] buildWispPalette failed");
    return;
  }
  mesh_->broadcast(frame, frameLen);
}

}  // namespace wisp

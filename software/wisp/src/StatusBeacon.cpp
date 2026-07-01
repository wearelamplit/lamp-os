#include "StatusBeacon.h"

#include <Arduino.h>
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
#include "status_json.hpp"

namespace wisp {

namespace {

constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
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
  // Emit first, then reset: snapshot is consistent before the 30s clock restarts.
  emitStatus();
  if (statusTimer_) {
    xTimerReset(statusTimer_, 0);
  }
}

void StatusBeacon::emit() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // Snapshot once so the HELLO frame and on-change diff see the same values.
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

  // mux-guarded snapshot: this timer task can race a loop-task update().
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN] = {0};
  size_t paletteIdPrefixLen = 0;
  if (palette_) {
    paletteIdPrefixLen = palette_->copyPaletteIdPrefix(
        paletteIdPrefix, sizeof(paletteIdPrefix));
  }

  // carriedFw* zero-fill; wire layout retained so older lamps don't malform-drop.
  const char* carriedFwChannel = nullptr;
  const size_t carriedFwChannelLen = 0;
  const uint32_t carriedFwVersion = 0;

  uint8_t buf[lamp_protocol::WISP_HELLO_MAX_SIZE];
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
  mesh_->broadcast(buf, n);

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

  // WiFi/Aurora flips would otherwise wait up to 30s for the wispStatus heartbeat.
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

  const bool wifiConn   = WiFi.isConnected();
  const bool auroraConn = aurora_ && aurora_->isStreaming();
  const int  currentZone = zone_ ? zone_->currentZone() : -1;
  const char* zoneSrc    = zone_ ? zoneSourceName(zone_->source())
                                 : zoneSourceName(ZoneSource::None);

  // copyObserved is mux-guarded: a direct reference would race observe()'s
  // push_back (which can relocate backing storage) on the loop task.
  int obsBuf[kMaxObservedZones];
  size_t obsCount = 0;
  if (zone_) {
    obsCount = zone_->copyObserved(obsBuf, kMaxObservedZones);
  }

  char paletteIdPrefix[kPaletteIdPrefixLen + 1] = {0};
  if (palette_) {
    const size_t n = palette_->copyPaletteIdPrefix(paletteIdPrefix,
                                                   kPaletteIdPrefixLen);
    paletteIdPrefix[n] = '\0';
  }

  const uint32_t lastSeenMs = millis();

  // manualPalette excluded: at 10 colors it can push JSON past CONTROL_MAX_PAYLOAD.
  const char* sourceName = "aurora";  // safe default for nullptr config
  if (config_) {
    switch (config_->sourceMode()) {
      case WispSourceMode::Off:    sourceName = "off";    break;
      case WispSourceMode::Manual: sourceName = "manual"; break;
      case WispSourceMode::Aurora: sourceName = "aurora"; break;
    }
  }

  uint8_t offR = 0, offG = 0, offB = 0;
  bool hasOffColor = false;
  uint8_t shuffleSeed = 0;
  if (config_) {
    const auto off = config_->offColor();
    offR = off.r; offG = off.g; offB = off.b;
    hasOffColor = true;
    shuffleSeed = config_->shuffleSeed();
  }
  const WispStatusFields fields{
      currentZone, zoneSrc, obsBuf, obsCount,
      wifiConn, auroraConn, paletteIdPrefix, lastSeenMs,
      sourceName, offR, offG, offB, hasOffColor, shuffleSeed };

  char jsonBuf[kStatusJsonBufLen];
  const size_t jsonLen = buildWispStatusJson(
      fields, jsonBuf, sizeof(jsonBuf), lamp_protocol::CONTROL_MAX_PAYLOAD);

  // Emit palette first: a wispStatus build failure must not suppress it.
  emitPalette();

  if (jsonLen == 0) {
    Serial.println("[wisp.beacon] wispStatus JSON build failed");
    return;
  }

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

  // seq under the mux: parallel emit() / emitStatus() must not share a seq.
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
}

void StatusBeacon::emitPalette() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  uint8_t rgb[lamp_protocol::kMaxWispPaletteColors * 3];
  size_t count = 0;
  if (config_) {
    count = config_->copyManualPalette(rgb, lamp_protocol::kMaxWispPaletteColors);
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

#include "status/status_emitter.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <lampos/led_types.hpp>

#include "paint/current_palette.hpp"
#include "net/mesh_link.hpp"
#include "config/wisp_config.hpp"
#include "config/zone_selector.hpp"
#include "aurora/AuroraPaletteClient.h"
#include "wire/lamp_protocol.hpp"
#include "status/status_json.hpp"
#include "status/seq_source.hpp"

namespace wisp {

namespace {

constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr size_t kPaletteIdPrefixLen = lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN;

}  // namespace

void StatusEmitter::begin(MeshLink* mesh, ZoneSelector* zone,
                          AuroraPaletteClient* aurora, WispConfig* config,
                          CurrentPalette* palette, SeqSource* seq) {
  mesh_ = mesh;
  zone_ = zone;
  aurora_ = aurora;
  config_ = config;
  palette_ = palette;
  seq_ = seq;
}

void StatusEmitter::startTimer() {
  if (!statusTimer_) {
    statusTimer_ = xTimerCreate(
        "wisp_status",
        pdMS_TO_TICKS(kStatusIntervalMs),
        pdTRUE,  // auto-reload
        this,
        [](TimerHandle_t t) {
          auto* self = static_cast<StatusEmitter*>(pvTimerGetTimerID(t));
          if (self) self->statusDue_.store(true, std::memory_order_relaxed);
        });
    if (statusTimer_) {
      xTimerStart(statusTimer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(status) failed");
    }
  }
}

void StatusEmitter::triggerOnChange() {
  statusDue_.store(true, std::memory_order_relaxed);
  if (statusTimer_) {
    xTimerReset(statusTimer_, 0);
  }
}

void StatusEmitter::pump() {
  checkConnEdges();
  if (!statusDue_.load(std::memory_order_relaxed)) return;
  const uint32_t now = millis();
  // Coalesce: leave the due flag set until the window opens.
  if (haveEmitted_ && now - lastEmitMs_ < kMinEmitIntervalMs) return;
  statusDue_.store(false, std::memory_order_relaxed);
  haveEmitted_ = true;
  lastEmitMs_ = now;
  emitStatus();
}

void StatusEmitter::checkConnEdges() {
  const bool wifiConn   = WiFi.isConnected();
  const bool auroraConn = aurora_ && aurora_->isStreaming();
  if (!haveLastConnState_) {
    haveLastConnState_ = true;
  } else if (wifiConn != lastWifiConnected_ ||
             auroraConn != lastAuroraConnected_) {
    Serial.printf("[wisp.beacon] connectivity edge wifi=%d aurora=%d\n",
                  (int)wifiConn, (int)auroraConn);
    triggerOnChange();
  }
  lastWifiConnected_   = wifiConn;
  lastAuroraConnected_ = auroraConn;
}

void StatusEmitter::emitStatus() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  const bool wifiConn   = WiFi.isConnected();
  const bool auroraConn = aurora_ && aurora_->isStreaming();
  const int  currentZone = zone_ ? zone_->currentZone() : -1;
  const char* zoneSrc    = zone_ ? zoneSourceName(zone_->source())
                                 : zoneSourceName(ZoneSource::None);

  // copyObserved is mux-guarded against concurrent observe() calls.
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

  // manualPalette excluded: emitted separately via MSG_WISP_PALETTE (buildWispPalette).
  const char* sourceName = "aurora";  // safe default for nullptr config
  if (config_) {
    switch (config_->sourceMode()) {
      case WispSourceMode::Off:    sourceName = "off";    break;
      case WispSourceMode::Manual: sourceName = "manual"; break;
      case WispSourceMode::Aurora: sourceName = "aurora"; break;
    }
  }

  uint8_t offR = 0, offG = 0, offB = 0, offW = 0;
  bool hasOffColor = false;
  uint8_t shuffleSeed = 0;
  uint32_t driftIntervalMs = 0;
  uint8_t driftFadePct = 0;
  const char* wispName = "";
  bool hasPassword = false;
  const char* ledType = "GRB";
  uint16_t pixelCount = 30;
  uint8_t rangeStep = 0;
  uint32_t opSeq = 0;
  uint8_t brightness = 100;
  if (config_) {
    const auto off = config_->offColor();
    offR = off.r; offG = off.g; offB = off.b; offW = off.w;
    hasOffColor = true;
    shuffleSeed = config_->shuffleSeed();
    driftIntervalMs = config_->driftIntervalMs();
    driftFadePct = config_->driftFadePct();
    wispName = config_->name().c_str();
    hasPassword = config_->password().length() > 0;
    ledType = lampos::led::byteOrderToString(config_->ledFormat());
    pixelCount = config_->pixelCount();
    rangeStep = config_->rangeStep();
    opSeq = config_->opSeq();
    brightness = config_->brightness();
  }
  const WispStatusFields fields{
      currentZone, zoneSrc, obsBuf, obsCount,
      wifiConn, auroraConn, paletteIdPrefix, lastSeenMs,
      sourceName, offR, offG, offB, offW, hasOffColor, shuffleSeed,
      driftIntervalMs, driftFadePct, wispName, hasPassword,
      ledType, pixelCount, rangeStep, opSeq, brightness };

  char jsonBuf[kStatusJsonBufLen];
  const size_t jsonLen = buildWispStatusJson(
      fields, jsonBuf, sizeof(jsonBuf), lamp_protocol::CONTROL_MAX_PAYLOAD);

  // Emit palette first: a wispStatus build failure must not suppress it.
  emitPalette();

  if (jsonLen == 0) {
    Serial.println("[wisp.beacon] wispStatus JSON build failed");
    return;
  }

  // SeqSource mux: concurrent HELLO and wispStatus emits must not share a seq.
  uint8_t frame[lamp_protocol::CONTROL_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  seq = seq_->next();
  frameLen = lamp_protocol::buildControlOp(
      frame, sizeof(frame), seq,
      kBroadcastMac, srcMac,
      reinterpret_cast<const uint8_t*>(jsonBuf), jsonLen);
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  if (!frameLen) {
    Serial.println("[wisp.beacon] buildControlOp(wispStatus) failed");
    return;
  }
  mesh_->broadcast(frame, frameLen);
}

void StatusEmitter::emitPalette() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  uint8_t rgb[lamp_protocol::kMaxWispPaletteColors * 3];
  uint8_t w[lamp_protocol::kMaxWispPaletteColors];
  size_t count = 0;
  if (config_) {
    count = config_->copyManualPalette(rgb, w,
                                       lamp_protocol::kMaxWispPaletteColors);
  }

  uint8_t frame[lamp_protocol::WISP_PALETTE_MAX_SIZE];
  size_t frameLen = 0;
  uint16_t seq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  seq = seq_->next();
  frameLen = lamp_protocol::buildWispPalette(
      frame, sizeof(frame), seq, srcMac,
      count > 0 ? rgb : nullptr, count > 0 ? w : nullptr, count);
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  if (!frameLen) {
    Serial.println("[wisp.beacon] buildWispPalette failed");
    return;
  }
  mesh_->broadcast(frame, frameLen);
}

}  // namespace wisp

#include "status/presence_beacon.hpp"

#include <Arduino.h>
#include <WiFi.h>

#include <cstring>

#include "paint/current_palette.hpp"
#include "net/mesh_link.hpp"
#include "paint/paint_distributor.hpp"
#include "fleet/wisp_roster.hpp"
#include "aurora/AuroraPaletteClient.h"
#include "wire/lamp_protocol.hpp"
#include "status/status_emitter.hpp"
#include "status/seq_source.hpp"
#include "config/wisp_config.hpp"

namespace wisp {

void PresenceBeacon::begin(MeshLink* mesh, PaintDistributor* paint,
                           CurrentPalette* palette, AuroraPaletteClient* aurora,
                           WispRoster* roster, SeqSource* seq,
                           StatusEmitter* statusEmitter, WispConfig* config) {
  mesh_ = mesh;
  paint_ = paint;
  palette_ = palette;
  aurora_ = aurora;
  roster_ = roster;
  seq_ = seq;
  statusEmitter_ = statusEmitter;
  config_ = config;
}

void PresenceBeacon::startTimer() {
  if (!timer_) {
    timer_ = xTimerCreate(
        "wisp_hello",
        pdMS_TO_TICKS(kHelloIntervalMs),
        pdTRUE,  // auto-reload
        this,
        [](TimerHandle_t t) {
          auto* self = static_cast<PresenceBeacon*>(pvTimerGetTimerID(t));
          if (self) self->helloDue_.store(true, std::memory_order_relaxed);
        });
    if (timer_) {
      xTimerStart(timer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(hello) failed");
    }
  }
}

void PresenceBeacon::pump() {
  serviceResends(millis());
  if (helloDue_.exchange(false, std::memory_order_relaxed)) {
    emit();
  }
  if (paint_ && paint_->consumeStateDirty()) {
    emitStateEvent();
  }
}

uint8_t PresenceBeacon::computePresenceFlags(bool wifiNow, bool auroraNow) const {
  uint8_t flags = 0;
  if (paint_ && paint_->paintMode()) {
    flags |= lamp_protocol::WISP_HELLO_FLAG_PAINT_MODE;
  }
  if (wifiNow)   flags |= lamp_protocol::WISP_HELLO_FLAG_WIFI_CONNECTED;
  if (auroraNow) flags |= lamp_protocol::WISP_HELLO_FLAG_AURORA_CONNECTED;
  return flags;
}

void PresenceBeacon::emitStateEvent() {
  if (!mesh_ || !roster_) return;
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);
  const uint8_t flags = computePresenceFlags(WiFi.isConnected(),
                                             aurora_ && aurora_->isStreaming());
  emitState(srcMac, flags);
}

void PresenceBeacon::emit() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // Snapshot once so the HELLO frame and on-change diff see the same values.
  const bool wifiNow   = WiFi.isConnected();
  const bool auroraNow = aurora_ && aurora_->isStreaming();
  const uint8_t flags  = computePresenceFlags(wifiNow, auroraNow);

  // mux-guarded snapshot: this timer task can race a loop-task update().
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN] = {0};
  size_t paletteIdPrefixLen = 0;
  if (palette_) {
    paletteIdPrefixLen = palette_->copyPaletteIdPrefix(
        paletteIdPrefix, sizeof(paletteIdPrefix));
  }

  // zero: no carried firmware channel on this beacon path.
  const char* carriedFwChannel = nullptr;
  const size_t carriedFwChannelLen = 0;
  const uint32_t carriedFwVersion = 0;

  uint8_t buf[lamp_protocol::WISP_HELLO_MAX_SIZE];
  size_t n = 0;
  uint16_t seq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  seq = seq_->next();
  n = lamp_protocol::buildWispHello(
      buf, sizeof(buf), seq,
      srcMac, kWispVersion, flags,
      paletteIdPrefix, paletteIdPrefixLen,
      carriedFwChannel, carriedFwChannelLen,
      carriedFwVersion);
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  if (!n) return;
  mesh_->broadcast(buf, n);
  queueResend(buf, n, kHelloResends, millis());

  // Alternate the two fat full-set frames so they never collide on the single
  // core: claim on odd ticks, state on even ticks (each every 4s).
  const uint32_t t = beaconTick_++;
  if (roster_) {
    if (t % 2 == 1) {
      emitClaim(srcMac);
    } else {
      emitState(srcMac, flags);
    }
  }

  // WiFi/Aurora flips would otherwise wait up to 30s for the wispStatus heartbeat.
  const bool helloFlagsChanged =
      haveLastHelloConn_ &&
      (wifiNow != lastHelloWifi_ || auroraNow != lastHelloAurora_);
  lastHelloWifi_     = wifiNow;
  lastHelloAurora_   = auroraNow;
  haveLastHelloConn_ = true;
  if (helloFlagsChanged && statusEmitter_) {
    statusEmitter_->triggerOnChange();
  }
}

void PresenceBeacon::emitClaim(const uint8_t srcMac[6]) {
  uint8_t claimEntries[lamp_protocol::kMaxWispClaimEntries *
                       lamp_protocol::WISP_CLAIM_ENTRY_SIZE] = {0};
  const size_t entryCount =
      roster_->snapshotClaimsForBroadcast(claimEntries, sizeof(claimEntries));
  uint8_t claimBuf[lamp_protocol::WISP_CLAIM_MAX_SIZE];
  uint16_t claimSeq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  claimSeq = seq_->next();
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  const size_t claimLen = lamp_protocol::buildWispClaim(
      claimBuf, sizeof(claimBuf), claimSeq, srcMac,
      entryCount > 0 ? claimEntries : nullptr, entryCount);
  if (claimLen) {
    mesh_->broadcast(claimBuf, claimLen);
    queueResend(claimBuf, claimLen, kFatResends, millis());
  }
}

void PresenceBeacon::emitState(const uint8_t srcMac[6], uint8_t presenceFlags) {
  uint8_t stateEntries[lamp_protocol::WISP_STATE_MAX_ENTRIES *
                       lamp_protocol::WISP_STATE_ENTRY_SIZE];
  // Off packs no colors: the lamp finds its entry absent and eases home.
  const size_t stateCount =
      (paint_ && paint_->paintMode())
          ? roster_->snapshotStateForBroadcast(stateEntries, sizeof(stateEntries))
          : 0;
  const uint8_t brightness = config_ ? config_->brightness() : 100;
  const uint32_t driftRateMs = config_ ? config_->driftIntervalMs() : 0;
  uint8_t stateBuf[lamp_protocol::WISP_STATE_MAX_SIZE];
  uint16_t stateSeq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  stateSeq = seq_->next();
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  const size_t stateLen = lamp_protocol::buildWispState(
      stateBuf, sizeof(stateBuf), stateSeq, srcMac, brightness, driftRateMs,
      presenceFlags, stateCount > 0 ? stateEntries : nullptr,
      static_cast<uint8_t>(stateCount));
  // No resend: at up to 1459 B STATE overflows the paint-sized resend slot,
  // and it re-covers the whole fleet on its own 4s cadence anyway.
  if (stateLen) mesh_->broadcast(stateBuf, stateLen);
}

void PresenceBeacon::queueResend(const uint8_t* frame, size_t len,
                                 uint8_t copies, uint32_t now) {
  if (len == 0 || len > sizeof(resends_[0].frame) || copies == 0) return;
  ResendSlot& s = resends_[resendCursor_];
  resendCursor_ = (resendCursor_ + 1) % kResendSlots;
  std::memcpy(s.frame, frame, len);
  s.len = static_cast<uint16_t>(len);
  s.remaining = copies;
  s.nextMs = now + kResendGapMs;
}

void PresenceBeacon::serviceResends(uint32_t now) {
  if (!mesh_) return;
  for (ResendSlot& s : resends_) {
    if (s.remaining == 0) continue;
    if (static_cast<int32_t>(now - s.nextMs) < 0) continue;
    mesh_->broadcast(s.frame, s.len);
    --s.remaining;
    s.nextMs += kResendGapMs;
  }
}

}  // namespace wisp

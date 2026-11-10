#include "status/presence_beacon.hpp"

#include <Arduino.h>
#include <WiFi.h>

#include <esp_random.h>

#include <cstring>

#include "paint/current_palette.hpp"
#include "net/mesh_link.hpp"
#include "paint/paint_distributor.hpp"
#include "fleet/wisp_roster.hpp"
#include "status/state_frame.hpp"
#include "core/wisp_controller.hpp"
#include "wire/lamp_protocol.hpp"
#include "status/status_emitter.hpp"
#include "status/seq_source.hpp"
#include "config/wisp_config.hpp"

namespace wisp {

void PresenceBeacon::begin(MeshLink* mesh, PaintDistributor* paint,
                           CurrentPalette* palette,
                           const WispController* controller,
                           WispRoster* roster, SeqSource* seq,
                           StatusEmitter* statusEmitter, WispConfig* config) {
  mesh_ = mesh;
  paint_ = paint;
  palette_ = palette;
  controller_ = controller;
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

void PresenceBeacon::pump(bool fatFrameSentThisPass) {
  const uint32_t now = millis();
  bool passBusy = serviceResends(now) || fatFrameSentThisPass;
  if (helloDue_.exchange(false, std::memory_order_relaxed)) {
    emit();
    passBusy = true;
  }
  if (stateBurstUntilMs_ != 0 &&
      static_cast<int32_t>(now - stateBurstUntilMs_) >= 0) {
    stateBurstUntilMs_ = 0;
  }
  switch (nextStateSend(passBusy, paint_ && paint_->stateDirty(),
                        stateBurstDue(now, stateBurstUntilMs_,
                                      lastStateEmitMs_, kStateBurstIntervalMs),
                        tickState_.due(now))) {
    case StateSend::None:
      return;
    case StateSend::Edge:
      paint_->consumeStateDirty();
      stateBurstUntilMs_ = now + kStateBurstMs;
      break;
    case StateSend::Burst:
      break;
    case StateSend::Tick:
      tickState_.markSent();
      break;
  }
  emitStateEvent();
}

uint8_t PresenceBeacon::computePresenceFlags(bool wifiNow,
                                             bool sourceLiveNow) const {
  uint8_t flags = 0;
  if (paint_ && paint_->paintMode()) {
    flags |= lamp_protocol::WISP_HELLO_FLAG_PAINT_MODE;
  }
  if (wifiNow)   flags |= lamp_protocol::WISP_HELLO_FLAG_WIFI_CONNECTED;
  if (sourceLiveNow) flags |= lamp_protocol::WISP_HELLO_FLAG_SOURCE_LIVE;
  return flags;
}

void PresenceBeacon::emitStateEvent() {
  if (!mesh_ || !roster_) return;
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);
  const uint8_t flags = computePresenceFlags(
      WiFi.isConnected(), controller_ && controller_->sourceLive());
  emitState(srcMac, flags);
}

void PresenceBeacon::emit() {
  if (!mesh_) return;

  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // Snapshot once so the HELLO frame and on-change diff see the same values.
  const bool wifiNow       = WiFi.isConnected();
  const bool sourceLiveNow = controller_ && controller_->sourceLive();
  const uint8_t flags      = computePresenceFlags(wifiNow, sourceLiveNow);

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
  const uint32_t helloMs = millis();
  queueResend(buf, n, kHelloResends, helloMs);

  const uint32_t t = beaconTick_++;
  if (roster_) {
    if (t % 2 == 1) emitClaim(srcMac);
    tickState_.schedule(helloMs, stateTickOffsetMs(esp_random()));
  }

  // WiFi/source-live flips would otherwise wait up to 30s for the wispStatus
  // heartbeat.
  const bool helloFlagsChanged =
      haveLastHelloConn_ &&
      (wifiNow != lastHelloWifi_ || sourceLiveNow != lastHelloSourceLive_);
  lastHelloWifi_       = wifiNow;
  lastHelloSourceLive_ = sourceLiveNow;
  haveLastHelloConn_ = true;
  if (helloFlagsChanged && statusEmitter_) {
    statusEmitter_->requestEmit();
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
  const uint8_t brightness = config_ ? config_->brightness() : 100;
  const uint32_t driftRateMs = config_ ? config_->driftIntervalMs() : 0;
  uint8_t stateBuf[lamp_protocol::WISP_STATE_MAX_SIZE];
  uint16_t stateSeq = 0;
  WISP_SEQ_PORTMUX_ENTER(&seq_->mux);
  stateSeq = seq_->next();
  WISP_SEQ_PORTMUX_EXIT(&seq_->mux);
  const size_t stateLen = buildStateFrame(
      *roster_, stateBuf, sizeof(stateBuf), stateSeq, srcMac, brightness,
      driftRateMs, presenceFlags, paint_ && paint_->painting());
  // No resend: at up to 1461 B STATE overflows the claim-sized resend slot.
  // Edge coverage comes from the burst window in pump(); the 2s cadence backs
  // both up.
  if (stateLen) {
    mesh_->broadcast(stateBuf, stateLen);
  }
  // Advance even on a zero-length build so a failing frame can't respin the
  // burst window every pump.
  lastStateEmitMs_ = millis();
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

bool PresenceBeacon::serviceResends(uint32_t now) {
  if (!mesh_) return false;
  bool sent = false;
  for (ResendSlot& s : resends_) {
    if (s.remaining == 0) continue;
    if (static_cast<int32_t>(now - s.nextMs) < 0) continue;
    mesh_->broadcast(s.frame, s.len);
    --s.remaining;
    s.nextMs += kResendGapMs;
    sent = true;
  }
  return sent;
}

}  // namespace wisp

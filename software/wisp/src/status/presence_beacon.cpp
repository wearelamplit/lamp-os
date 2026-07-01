#include "status/presence_beacon.hpp"

#include <Arduino.h>
#include <WiFi.h>

#include "paint/current_palette.hpp"
#include "net/mesh_link.hpp"
#include "paint/paint_distributor.hpp"
#include "fleet/wisp_roster.hpp"
#include "aurora/AuroraPaletteClient.h"
#include "wire/lamp_protocol.hpp"
#include "status/status_emitter.hpp"
#include "status/seq_source.hpp"

namespace wisp {

void PresenceBeacon::begin(MeshLink* mesh, PaintDistributor* paint,
                           CurrentPalette* palette, AuroraPaletteClient* aurora,
                           WispRoster* roster, SeqSource* seq,
                           StatusEmitter* statusEmitter) {
  mesh_ = mesh;
  paint_ = paint;
  palette_ = palette;
  aurora_ = aurora;
  roster_ = roster;
  seq_ = seq;
  statusEmitter_ = statusEmitter;
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
          if (self) self->emit();
        });
    if (timer_) {
      xTimerStart(timer_, 0);
    } else {
      Serial.println("[wisp.beacon] xTimerCreate(hello) failed");
    }
  }
}

void PresenceBeacon::emit() {
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

  if (roster_) {
    uint8_t claimEntries[lamp_protocol::kMaxWispClaimEntries *
                         lamp_protocol::WISP_CLAIM_ENTRY_SIZE] = {0};
    const size_t entryCount = roster_->snapshotClaimsForBroadcast(
        claimEntries, sizeof(claimEntries));
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

}  // namespace wisp

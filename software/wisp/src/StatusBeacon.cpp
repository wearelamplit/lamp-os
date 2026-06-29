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

// FF:FF:FF:FF:FF:FF broadcast target for the CONTROL_OP frame. Matches the
// targetMac convention every other broadcast CONTROL_OP uses on the wire.
constexpr uint8_t kBroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Take up to N characters of the palette id as the on-wire prefix. Matches
// the MSG_WISP_HELLO 8-byte slot convention so the app sees the same id
// from both paths.
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
  // Emit immediately AND reschedule the heartbeat from now. Order matters:
  // if we reset the timer first and emitStatus is slow (it isn't, but the
  // future could change that), the next heartbeat would land mid-emit. By
  // emitting first we capture a consistent snapshot, then re-zero the
  // 30s clock.
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

  // Flags. WISP_HELLO carries paintMode + wifi + aurora as three single
  // bits; mirrors what wispStatus carries in JSON. Snapshot wifi/aurora into
  // locals once so the same values feed both the HELLO frame and the
  // on-change diff below — otherwise a flip between the two reads could
  // race and produce a stale diff comparison.
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

  // paletteIdPrefix: low 8 bytes of the active palette id (or zeros). Use
  // the mux-guarded snapshot so this timer-task read doesn't race a loop-task
  // CurrentPalette::update() reassigning paletteId_.
  char paletteIdPrefix[lamp_protocol::WISP_HELLO_PALETTE_ID_PREFIX_LEN] = {0};
  size_t paletteIdPrefixLen = 0;
  if (palette_) {
    paletteIdPrefixLen = palette_->copyPaletteIdPrefix(
        paletteIdPrefix, sizeof(paletteIdPrefix));
  }

  // carriedFwChannel / carriedFwVersion zero-fill: wisp no longer
  // distributes firmware. Wire layout retained so older lamps that read
  // these fields don't malform-drop the HELLO.
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
  // broadcast() ends in esp_now_send which is itself queued — safe to call
  // outside the mux. The seq is already committed.
  mesh_->broadcast(buf, n);

  // MSG_WISP_CLAIM — broadcast our current claim set so peers can build
  // the shared view. Same 2 s cadence as MSG_WISP_HELLO; lamps gossip-
  // relay it the same way. Roster is optional during init (legacy
  // construction paths may pass nullptr).
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
  // the only path that observes radio state on a fast cadence; without this
  // diff, a connect/disconnect would wait up to 30s for the next heartbeat
  // to be reported in wispStatus. emitStatus() takes its own portMUX and is
  // re-entrant-safe from this task.
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

  // Snapshot the fields BEFORE the JSON build so the snapshot is
  // consistent within one emission. Reading WiFi + Aurora flags here is
  // cheap (one bool each); doing it inside the lock would needlessly
  // hold the mux during external library calls.
  const bool wifiConn   = WiFi.isConnected();
  const bool auroraConn = aurora_ && aurora_->isStreaming();
  const int  currentZone = zone_ ? zone_->currentZone() : -1;
  const char* zoneSrc    = zone_ ? zoneSourceName(zone_->source())
                                 : zoneSourceName(ZoneSource::None);

  // Snapshot observedZones into a fixed stack buffer via the mux-guarded
  // accessor. Taking a reference to the underlying vector here would race
  // ZoneSelector::observe() on the loop task — push_back can relocate the
  // backing storage, and erase() invalidates iterators. Buffer is sized
  // to kMaxObservedZones so we capture the full set without truncation.
  int obsBuf[kMaxObservedZones];
  size_t obsCount = 0;
  if (zone_) {
    obsCount = zone_->copyObserved(obsBuf, kMaxObservedZones);
  }

  // paletteIdPrefix — first 8 chars of the active palette id (or empty).
  // copyPaletteIdPrefix snapshots under the CurrentPalette mux, so this
  // timer-task call won't tear against a loop-task update().
  char paletteIdPrefix[kPaletteIdPrefixLen + 1] = {0};
  if (palette_) {
    const size_t n = palette_->copyPaletteIdPrefix(paletteIdPrefix,
                                                   kPaletteIdPrefixLen);
    paletteIdPrefix[n] = '\0';
  }

  const uint32_t lastSeenMs = millis();

  // Source-mode field. Stringified so the app side doesn't have to
  // dual-decode int / string forms. Older app versions that don't
  // recognize this field ignore it, so this is back-compat. The manualPalette
  // is deliberately NOT included — at 10 colors it would push the JSON
  // past CONTROL_MAX_PAYLOAD (230 B) once observedZones is non-trivial.
  // The Flutter side keeps its own per-session copy of the saved palette;
  // a fresh app open after a wisp reboot will read empty until the
  // operator re-edits and saves (the wisp keeps painting from NVS in the
  // meantime). Acceptable trade per the spec audit.
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
  if (jsonLen == 0) {
    Serial.println("[wisp.beacon] wispStatus JSON build failed");
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

  // Palette snapshotted under WispConfig's mutex — safe to call from the
  // timer-service task while setManualPalette runs on the loop task.
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

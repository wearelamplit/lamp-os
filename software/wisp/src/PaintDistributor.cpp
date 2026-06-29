#include "PaintDistributor.h"

#include "WispRoster.h"

#include <Arduino.h>

#include <cstring>

#include "CurrentPalette.h"
#include "LampInventory.h"
#include "MeshLink.h"
#include "TupleSampler.h"
#include "lamp_protocol.hpp"

namespace wisp {

void PaintDistributor::begin(LampInventory* inventory, MeshLink* mesh,
                             CurrentPalette* palette, WispRoster* roster) {
  inventory_ = inventory;
  mesh_ = mesh;
  palette_ = palette;
  roster_ = roster;
}

void PaintDistributor::setPaintMode(bool on) {
  if (on == paintMode_) {
    // Idempotent: paint:on while already on still kicks a fresh walk so
    // the user sees instant feedback if a lamp joined since last paint.
    if (on) beginWalk(Mode::Paint);
    return;
  }
  paintMode_ = on;
  if (on) {
    lastBackstopMs_ = millis();
    beginWalk(Mode::Paint);
  } else {
    // Walk every currently-known peer with a RESTORE so they fall back to
    // their authored personality. We deliberately keep paintMode_ false so
    // tick() doesn't re-trigger backstop refreshes.
    beginWalk(Mode::Restore);
  }
}

void PaintDistributor::onPaletteChanged() {
  if (!paintMode_) return;
  beginWalk(Mode::Paint);
}

void PaintDistributor::tick(uint32_t nowMs) {
  // Drain the paced queue first; backstop logic only fires when idle so we
  // don't double-up sends.
  if (walkMode_ != Mode::Idle && walkIdx_ < walkCount_) {
    if (nowMs - lastSendMs_ < kPerPeerPaceMs) return;
    lastSendMs_ = nowMs;
    const uint8_t* mac = walkMacs_[walkIdx_];
    if (walkMode_ == Mode::Paint) {
      sendPaintToPeer(mac);
    } else {
      sendRestoreToPeer(mac);
    }
    walkIdx_++;
    if (walkIdx_ >= walkCount_) walkMode_ = Mode::Idle;
    return;
  }

  // Backstop: every 10s while paintMode is on, kick a fresh walk in case a
  // lamp missed a frame or just joined the roster between palette changes.
  if (paintMode_ && (nowMs - lastBackstopMs_) >= kBackstopRefreshMs) {
    lastBackstopMs_ = nowMs;
    beginWalk(Mode::Paint);
  }
}

void PaintDistributor::beginWalk(Mode mode) {
  if (!inventory_) return;
  auto inventorySnap = inventory_->snapshot();
  walkCount_ = 0;
  for (const auto& e : inventorySnap) {
    if (walkCount_ >= kMaxWalkPeers) break;
    // Multi-wisp coordination: only paint lamps THIS wisp claims via
    // the shared WispRoster view. Without the roster (legacy / test
    // path) every lamp gets painted (regression baseline). With it,
    // lamps owned by a closer peer wisp get skipped — the peer paints
    // them; we don't, so the lamp is only painted by one wisp at a
    // time.
    if (roster_ && !roster_->claims(e.mac)) continue;
    std::memcpy(walkMacs_[walkCount_], e.mac, 6);
    walkCount_++;
  }
  walkIdx_ = 0;
  walkMode_ = walkCount_ ? mode : Mode::Idle;
  // Force the first send through immediately (lastSendMs_ in the past).
  lastSendMs_ = millis() - kPerPeerPaceMs;
  // Debug-session telemetry: walk start gives a per-cycle health view.
  Serial.printf("[paint] walk %s peers=%u\n",
                mode == Mode::Paint ? "Paint" : "Restore",
                (unsigned)walkCount_);
}

void PaintDistributor::sendPaintToPeer(const uint8_t mac[6]) {
  if (!mesh_ || !palette_) return;
  // Defensive: don't blast a peer with a zero-palette paint frame.
  // The Aurora-default boot path flips paintMode on BEFORE any
  // Aurora callback has populated currentPalette — without this
  // gate the 10 s backstop walks every peer with an all-zero
  // (black, fade=1500 ms) OVERRIDE_COLORS frame, which on the lamp
  // side fights every BLE base-color edit the operator makes.
  if (palette_->colors().empty()) return;

  // TupleSampler picks two distinct authored palette colors per peer —
  // FNV-1a + Stafford finalizer over (mac XOR salt), one salt per output
  // (idxA, idxB, swap). t[0] → base, t[1] → shade; ~50% of MACs hit the
  // swap branch so the (base, shade) order varies across the fleet.
  //
  // Send both as ONE combined frame using surface=BaseAndShade,
  // numColors=2 (colors[0]=base, colors[1]=shade). This halves the
  // ESP-NOW frame count per lamp per cycle vs the prior split-into-
  // two-frames design, which became the dominant source of per-surface
  // loss under BLE coex pressure (Base lost ~31%, Shade lost ~15% with
  // 22-lamp fleet + BLE-connected app). Now both surfaces deliver
  // atomically — either both colors land or both are lost — so the
  // base-flicker pattern (base losing while shade keeps painting) is
  // gone by construction.
  //
  // The prior 10ms inter-frame delay + the ESP_NOW_SEND_FAIL workaround
  // it documented are no longer relevant.
  ColorTuple t = sampleTupleForMac(*palette_, mac, shuffleSeed_);
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  uint8_t colorsRGBW[2 * 4] = {
      t.r[0], t.g[0], t.b[0], t.w[0],   // [0] base
      t.r[1], t.g[1], t.b[1], t.w[1],   // [1] shade
  };
  uint8_t buf[lamp_protocol::OVERRIDE_COLORS_MAX_SIZE];
  const uint16_t seq = seqCounter_++;
  size_t n = lamp_protocol::buildOverrideColors(
      buf, sizeof(buf), seq,
      srcMac, mac,
      lamp_protocol::OverrideSurface::BaseAndShade,
      lamp_protocol::OverrideSource::Wisp,
      kDefaultFadeDurationMs,
      colorsRGBW, /*numColors=*/2);
  if (n) mesh_->send(mac, buf, n);
  Serial.printf("[paint] send Pair->%02X:%02X:%02X:%02X:%02X:%02X seq=%u base=%u,%u,%u,%u shade=%u,%u,%u,%u\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                (unsigned)seq,
                t.r[0], t.g[0], t.b[0], t.w[0],
                t.r[1], t.g[1], t.b[1], t.w[1]);
}

void PaintDistributor::sendRestoreToPeer(const uint8_t mac[6]) {
  if (!mesh_) return;
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  // One combined restore frame using surface=BaseAndShade. The lamp's
  // handler dispatches it to BOTH per-surface ColorOverrides. Matches
  // the sendPaintToPeer combined-frame model — one ESP-NOW packet per
  // peer per cycle, atomic delivery.
  uint8_t buf[lamp_protocol::RESTORE_FIXED_SIZE];
  size_t n = lamp_protocol::buildRestoreColors(
      buf, sizeof(buf), seqCounter_++,
      srcMac, mac,
      lamp_protocol::OverrideSurface::BaseAndShade,
      lamp_protocol::OverrideSource::Wisp,
      kDefaultFadeDurationMs);
  if (n) mesh_->send(mac, buf, n);
}

}  // namespace wisp

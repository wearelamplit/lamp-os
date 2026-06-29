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
    // paint:on while already on still kicks a fresh walk so a lamp that
    // joined since last paint gets picked up.
    if (on) beginWalk(Mode::Paint);
    return;
  }
  paintMode_ = on;
  if (on) {
    lastBackstopMs_ = millis();
    beginWalk(Mode::Paint);
  } else {
    // RESTORE every known peer so they fall back to their authored
    // personality. paintMode_ stays false so tick() won't re-trigger backstop
    // refreshes.
    beginWalk(Mode::Restore);
  }
}

void PaintDistributor::onPaletteChanged() {
  if (!paintMode_) return;
  beginWalk(Mode::Paint);
}

void PaintDistributor::tick(uint32_t nowMs) {
  // Drain the paced queue first; backstop fires only when idle to avoid
  // double sends.
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
    // Only paint lamps THIS wisp claims via the shared WispRoster view.
    // Without a roster (tests) every lamp is painted. Lamps owned by a closer
    // peer wisp are skipped so each lamp is painted by only one wisp.
    if (roster_ && !roster_->claims(e.mac)) continue;
    std::memcpy(walkMacs_[walkCount_], e.mac, 6);
    walkCount_++;
  }
  walkIdx_ = 0;
  walkMode_ = walkCount_ ? mode : Mode::Idle;
  // Force the first send through immediately (lastSendMs_ in the past).
  lastSendMs_ = millis() - kPerPeerPaceMs;
  Serial.printf("[paint] walk %s peers=%u\n",
                mode == Mode::Paint ? "Paint" : "Restore",
                (unsigned)walkCount_);
}

void PaintDistributor::sendPaintToPeer(const uint8_t mac[6]) {
  if (!mesh_ || !palette_) return;
  // Don't paint a zero-palette frame: the Aurora-default boot path flips
  // paintMode on before any Aurora callback populates currentPalette.
  // Without this gate the backstop blasts an all-zero (black) OVERRIDE that
  // fights every BLE base-color edit on the lamp side.
  if (palette_->colors().empty()) return;

  // t[0] → base, t[1] → shade as ONE combined frame (surface=BaseAndShade,
  // numColors=2) so both surfaces deliver atomically.
  ColorTuple t = sampleTupleForMac(*palette_, mac);
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

  // One combined restore frame (surface=BaseAndShade); the lamp dispatches it
  // to both per-surface ColorOverrides. One packet per peer, atomic.
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

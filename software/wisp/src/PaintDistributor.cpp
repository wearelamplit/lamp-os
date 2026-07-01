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
    // kick a fresh walk so a newly-joined lamp gets painted
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

  // Backstop: lamp may have missed a frame or joined between palette changes.
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
    if (roster_ && !roster_->claims(e.mac)) continue;
    std::memcpy(walkMacs_[walkCount_], e.mac, 6);
    walkCount_++;
  }
  walkIdx_ = 0;
  walkMode_ = walkCount_ ? mode : Mode::Idle;
  lastSendMs_ = millis() - kPerPeerPaceMs;
  Serial.printf("[paint] walk %s peers=%u\n",
                mode == Mode::Paint ? "Paint" : "Restore",
                (unsigned)walkCount_);
}

void PaintDistributor::sendPaintToPeer(const uint8_t mac[6]) {
  if (!mesh_ || !palette_) return;
  // paintMode can flip on before the first Aurora callback populates the
  // palette; an empty palette would blast peers with an all-zero frame.
  if (palette_->colors().empty()) return;

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

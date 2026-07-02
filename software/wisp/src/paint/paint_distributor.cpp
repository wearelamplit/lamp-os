#include "paint/paint_distributor.hpp"

#include "fleet/wisp_roster.hpp"

#include <Arduino.h>
#include <esp_random.h>

#include <algorithm>
#include <array>
#include <cstring>

#include "paint/current_palette.hpp"
#include "paint/drift.hpp"
#include "fleet/lamp_inventory.hpp"
#include "net/mesh_link.hpp"
#include "paint/tuple_sampler.hpp"
#include "wire/lamp_protocol.hpp"

namespace wisp {

void PaintDistributor::begin(LampInventory* inventory, MeshLink* mesh,
                             CurrentPalette* palette, WispRoster* roster) {
  inventory_ = inventory;
  mesh_ = mesh;
  palette_ = palette;
  roster_ = roster;
  refreshDriftRoster();
}

void PaintDistributor::setPaintMode(bool on) {
  if (on == paintMode_) {
    // kick a fresh walk so a newly-joined lamp gets painted
    if (on) beginWalk(Mode::Paint);
    return;
  }
  paintMode_ = on;
  if (on) {
    beginWalk(Mode::Paint);
    refreshDriftRoster();
  } else {
    beginWalk(Mode::Restore);
  }
}

void PaintDistributor::onPaletteChanged() {
  if (!paintMode_) return;
  beginWalk(Mode::Paint);
}

void PaintDistributor::tick(uint32_t nowMs) {
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

  // Drift rotation replaces the global backstop — each lamp is re-targeted
  // on its slot, so every lamp sees a refresh within one interval.
  if (paintMode_ && driftSlotMs_ > 0 && driftCount_ > 0 &&
      (nowMs - lastDriftFireMs_) >= driftSlotMs_) {
    lastDriftFireMs_ = nowMs;
    sendDriftToPeer(driftIdx_ % driftCount_);
    driftIdx_ = nextDriftIdx(driftIdx_, driftCount_);
  }

  // Fold in lamps that joined or left during steady paint without waiting
  // for a mode change or interval update.
  if (paintMode_ && (nowMs - lastDriftRosterMs_) >= 5000) {
    lastDriftRosterMs_ = nowMs;
    refreshDriftRoster(/*paintNewcomers=*/true);
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

void PaintDistributor::setDriftInterval(uint32_t intervalMs, uint8_t fadePct) {
  driftIntervalMs_ = intervalMs;
  driftFadePct_    = fadePct;
  refreshDriftRoster();
}

void PaintDistributor::refreshDriftRoster(bool paintNewcomers) {
  if (!inventory_) return;
  uint8_t prev[kMaxWalkPeers][6];
  const size_t prevCount = driftCount_;
  for (size_t i = 0; i < prevCount; i++) std::memcpy(prev[i], driftMacs_[i], 6);

  auto snap = inventory_->snapshot();
  std::array<uint8_t, 6> tmp[kMaxWalkPeers];
  driftCount_ = 0;
  for (const auto& e : snap) {
    if (driftCount_ >= kMaxWalkPeers) break;
    if (roster_ && !roster_->claims(e.mac)) continue;
    std::memcpy(tmp[driftCount_].data(), e.mac, 6);
    driftCount_++;
  }
  std::stable_sort(tmp, tmp + driftCount_, [](const auto& a, const auto& b) {
    return std::memcmp(a.data(), b.data(), 6) < 0;
  });
  for (size_t i = 0; i < driftCount_; i++) std::memcpy(driftMacs_[i], tmp[i].data(), 6);
  driftSlotMs_ = driftSlotMs(driftIntervalMs_, driftCount_);
  driftIdx_    = driftCount_ ? driftIdx_ % driftCount_ : 0;

  // A lamp that joins mid-run would otherwise wait a full interval for its
  // drift slot; paint it immediately (snappy) so it catches up within one
  // refresh, then it folds into the rotation.
  // ponytail: direct unpaced sends. Typically 0-1 joiners per refresh; a mass
  // simultaneous rejoin is bounded at kMaxWalkPeers and self-heals — a dropped
  // immediate paint just waits for that lamp's normal drift slot.
  if (paintNewcomers && paintMode_) {
    for (size_t i = 0; i < driftCount_; i++) {
      bool known = false;
      for (size_t j = 0; j < prevCount; j++) {
        if (std::memcmp(driftMacs_[i], prev[j], 6) == 0) { known = true; break; }
      }
      if (!known) sendPaintToPeer(driftMacs_[i]);
    }
  }
}

void PaintDistributor::sendDriftToPeer(size_t idx) {
  if (!mesh_ || !palette_) return;
  if (palette_->colors().empty()) return;

  const uint8_t* mac = driftMacs_[idx];
  ColorTuple t = sampleTupleForMac(*palette_, mac, shuffleSeed_);
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);

  uint8_t colorsRGBW[2 * 4] = {
      t.r[0], t.g[0], t.b[0], t.w[0],
      t.r[1], t.g[1], t.b[1], t.w[1],
  };
  const uint32_t fadeDurationMs = driftFadeMs(driftIntervalMs_, driftFadePct_, esp_random());
  uint8_t buf[lamp_protocol::OVERRIDE_COLORS_MAX_SIZE];
  const uint16_t seq = seqCounter_++;
  size_t n = lamp_protocol::buildOverrideColors(
      buf, sizeof(buf), seq,
      srcMac, mac,
      lamp_protocol::OverrideSurface::BaseAndShade,
      lamp_protocol::OverrideSource::Wisp,
      fadeDurationMs,
      colorsRGBW, /*numColors=*/2);
  if (n) mesh_->send(mac, buf, n);
  Serial.printf("[drift] send Pair->%02X:%02X:%02X:%02X:%02X:%02X seq=%u fade=%ums\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                (unsigned)seq, (unsigned)fadeDurationMs);
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

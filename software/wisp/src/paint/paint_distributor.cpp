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
  stateDirty_ = true;
  if (on == paintMode_) {
    if (on) assignAllClaimed();
    return;
  }
  paintMode_ = on;
  if (on) {
    refreshDriftRoster();
    assignAllClaimed();
  }
}

void PaintDistributor::onPaletteChanged() {
  if (!paintMode_) return;
  assignAllClaimed();
}

void PaintDistributor::tick(uint32_t nowMs) {
  // Re-color one lamp per drift slot so every lamp shifts within one interval;
  // STATE broadcasts the roster colors on its own cadence.
  if (paintMode_ && driftSlotMs_ > 0 && driftCount_ > 0 &&
      (nowMs - lastDriftFireMs_) >= driftSlotMs_) {
    lastDriftFireMs_ = nowMs;
    assignDriftColor(driftIdx_ % driftCount_);
    driftIdx_ = nextDriftIdx(driftIdx_, driftCount_);
  }

  // Space-dim re-assert, independent of paint mode (runs in Off too). The
  // lamp override times out at 60 s, so only a live dim needs re-sending.
  if (brightness_ < 100 && !brWalkActive_ &&
      (nowMs - lastBrWalkMs_) >= kBrightnessReassertMs) {
    beginBrightnessWalk();
  }
  if (brRepeatsLeft_ > 0 && !brWalkActive_ && nowMs >= nextBrPassMs_) {
    brRepeatsLeft_--;
    beginBrightnessWalk();
  }
  if (brWalkActive_ && brWalkIdx_ < brWalkCount_ &&
      (nowMs - lastBrSendMs_) >= kPerPeerPaceMs &&
      unicastGateOpen(nowMs, lastUnicastMs_, kMinUnicastGapMs)) {
    lastBrSendMs_ = nowMs;
    lastUnicastMs_ = nowMs;
    sendBrightnessToPeer(brWalkMacs_[brWalkIdx_++]);
    if (brWalkIdx_ >= brWalkCount_) {
      brWalkActive_ = false;
      if (brRepeatsLeft_ > 0) nextBrPassMs_ = nowMs + kRestorePassGapMs;
    }
  }
}

void PaintDistributor::assignAllClaimed() {
  for (size_t i = 0; i < driftCount_; i++) assignPaintColor(driftMacs_[i]);
  stateDirty_ = true;
}

void PaintDistributor::setBrightness(uint8_t pct) {
  brightness_ = pct > 100 ? 100 : pct;
  brRepeatsLeft_ = brightness_ >= 100 ? kRestoreRepeats : 0;
  beginBrightnessWalk();
}

void PaintDistributor::beginBrightnessWalk() {
  lastBrWalkMs_ = millis();
  brWalkCount_ = 0;
  brWalkIdx_ = 0;
  if (!inventory_) { brWalkActive_ = false; return; }
  LampObservation obs[kMaxWalkPeers];
  const size_t n = inventory_->copyObservations(obs, kMaxWalkPeers);
  for (size_t i = 0; i < n; i++) {
    if (roster_ && !roster_->claims(obs[i].mac)) continue;
    std::memcpy(brWalkMacs_[brWalkCount_], obs[i].mac, 6);
    brWalkCount_++;
  }
  brWalkActive_ = brWalkCount_ > 0;
  lastBrSendMs_ = millis() - kPerPeerPaceMs;
}

void PaintDistributor::sendBrightnessToPeer(const uint8_t mac[6]) {
  if (!mesh_) return;
  uint8_t srcMac[6] = {0};
  mesh_->getMac(srcMac);
  uint8_t buf[lamp_protocol::OVERRIDE_BRIGHTNESS_FIXED_SIZE];
  const uint16_t seq = seqCounter_++;
  const size_t n = lamp_protocol::buildOverrideBrightness(
      buf, sizeof(buf), seq, srcMac, mac,
      lamp_protocol::OverrideSurface::BaseAndShade,
      lamp_protocol::OverrideSource::Wisp,
      kBrightnessFadeMs, brightness_);
  if (n) mesh_->send(mac, buf, n);
#ifdef LAMP_DEBUG
  Serial.printf("[bright] send %u->%02X:%02X:%02X:%02X:%02X:%02X seq=%u\n",
                (unsigned)brightness_, mac[0], mac[1], mac[2], mac[3], mac[4],
                mac[5], (unsigned)seq);
#endif
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

  LampObservation obs[kMaxWalkPeers];
  const size_t n = inventory_->copyObservations(obs, kMaxWalkPeers);
  std::array<uint8_t, 6> tmp[kMaxWalkPeers];
  driftCount_ = 0;
  for (size_t i = 0; i < n; i++) {
    if (roster_ && !roster_->claims(obs[i].mac)) continue;
    std::memcpy(tmp[driftCount_].data(), obs[i].mac, 6);
    driftCount_++;
  }
  std::sort(tmp, tmp + driftCount_, [](const auto& a, const auto& b) {
    return std::memcmp(a.data(), b.data(), 6) < 0;
  });
  for (size_t i = 0; i < driftCount_; i++) std::memcpy(driftMacs_[i], tmp[i].data(), 6);
  driftSlotMs_ = driftSlotMs(driftIntervalMs_, driftCount_);
  driftIdx_    = driftCount_ ? driftIdx_ % driftCount_ : 0;

  if (paintNewcomers && paintMode_) {
    for (size_t i = 0; i < driftCount_; i++) {
      bool known = false;
      for (size_t j = 0; j < prevCount; j++) {
        if (std::memcmp(driftMacs_[i], prev[j], 6) == 0) { known = true; break; }
      }
      if (!known) { assignPaintColor(driftMacs_[i]); stateDirty_ = true; }
    }
  }
}

void PaintDistributor::assignDriftColor(size_t idx) {
  if (!roster_ || !palette_) return;
  if (palette_->colors().empty()) return;
  ColorTuple t = sampleTupleAtPositions(palette_->colors(), esp_random(), esp_random());
  const uint8_t base3[3]  = {t.r[0], t.g[0], t.b[0]};
  const uint8_t shade3[3] = {t.r[1], t.g[1], t.b[1]};
  roster_->setLampPaint(driftMacs_[idx], base3, shade3);
}

void PaintDistributor::assignPaintColor(const uint8_t mac[6]) {
  if (!roster_ || !palette_) return;
  // paintMode can flip on before the first Aurora callback populates the
  // palette; an empty palette would store an all-zero color.
  if (palette_->colors().empty()) return;
  ColorTuple t = sampleTupleForMac(*palette_, mac, shuffleSeed_);
  const uint8_t base3[3]  = {t.r[0], t.g[0], t.b[0]};
  const uint8_t shade3[3] = {t.r[1], t.g[1], t.b[1]};
  roster_->setLampPaint(mac, base3, shade3);
}

}  // namespace wisp

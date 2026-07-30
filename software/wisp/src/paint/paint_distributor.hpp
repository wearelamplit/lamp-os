// PaintDistributor assigns the active palette to every claimed lamp by
// writing per-lamp base/shade colors into the WispRoster. MSG_WISP_STATE
// (PresenceBeacon) then broadcasts those colors as the sole paint render
// path; the distributor emits no per-lamp color unicast.
//
// Space-dim brightness is the one surviving unicast: it rides
// MSG_OVERRIDE_BRIGHTNESS to each claimed lamp, independent of paint mode.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class CurrentPalette;
class LampInventory;
class MeshLink;
class WispRoster;

class PaintDistributor {
 public:
  void begin(LampInventory* inventory, MeshLink* mesh,
             CurrentPalette* palette, WispRoster* roster = nullptr);

  // On: color every claimed lamp so STATE carries paint. Off: stop coloring so
  // PresenceBeacon packs no color entries and lamps ease home. Either edge
  // marks STATE dirty for an immediate event-driven emit.
  void setPaintMode(bool on);
  bool paintMode() const { return paintMode_; }

  // Consumed by the beacon: a paint-mode edge wants an out-of-cadence STATE
  // emit so a lamp responds within a frame, not up to one STATE tick.
  bool consumeStateDirty() { const bool d = stateDirty_; stateDirty_ = false; return d; }

  // Shuffle seed passed through to TupleSampler. Changing it re-rolls
  // per-lamp color assignments on the next pass.
  void setShuffleSeed(uint8_t s) { shuffleSeed_ = s; }

  // intervalMs: how often each lamp is re-colored [30000..3600000].
  // fadePct: fade length as % of interval [0..100], carried on STATE.
  void setDriftInterval(uint32_t intervalMs, uint8_t fadePct);

  // Space-dim factor (0..100) asserted on claimed lamps via unicast
  // MSG_OVERRIDE_BRIGHTNESS. Stores the value and kicks an immediate re-assert
  // walk. Independent of paint mode: fires in every source mode incl Off.
  void setBrightness(uint8_t pct);

  void onPaletteChanged();
  void tick(uint32_t nowMs);

  // Rebuilds driftMacs_ from claimed inventory, sorts by MAC, recomputes slot.
  // With paintNewcomers, any lamp new since the last refresh is colored
  // immediately so its first STATE frame carries paint.
  void refreshDriftRoster(bool paintNewcomers = false);

 private:
  // Store a per-lamp color on the lamp's roster claim; no wire send.
  // assignPaintColor is deterministic per-MAC; assignDriftColor re-rolls.
  void assignPaintColor(const uint8_t mac[6]);
  void assignDriftColor(size_t idx);
  void assignAllClaimed();

  // Snapshot claimed lamps into brWalkMacs_ and arm the paced brightness walk.
  void beginBrightnessWalk();
  void sendBrightnessToPeer(const uint8_t mac[6]);

  LampInventory* inventory_ = nullptr;
  MeshLink* mesh_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  uint8_t shuffleSeed_ = 0;
  // Null roster: assignment is a no-op (used in tests).
  WispRoster* roster_ = nullptr;
  bool paintMode_ = false;
  bool stateDirty_ = false;
  uint16_t seqCounter_ = 0;

  static constexpr size_t kMaxWalkPeers = 100;  // matches LampInventory::MAX_LAMPS

  uint32_t driftIntervalMs_    = 120000;
  uint8_t  driftFadePct_       = 50;
  uint8_t  driftMacs_[kMaxWalkPeers][6];
  size_t   driftCount_         = 0;
  size_t   driftIdx_           = 0;
  uint32_t driftSlotMs_        = 0;
  uint32_t lastDriftFireMs_    = 0;

  // Space-dim brightness re-assert. Own paced walk buffer. Dimming (< 100) is
  // held by the periodic re-assert loop; the return to 100 has no re-assert,
  // so it repeats the un-dim walk for redundancy, backed by the lamp's 60 s
  // watchdog.
  uint8_t  brightness_         = 100;
  uint8_t  brWalkMacs_[kMaxWalkPeers][6];
  size_t   brWalkCount_        = 0;
  size_t   brWalkIdx_          = 0;
  bool     brWalkActive_       = false;
  uint32_t lastBrWalkMs_       = 0;
  uint32_t lastBrSendMs_       = 0;
  uint8_t  brRepeatsLeft_      = 0;
  uint32_t nextBrPassMs_       = 0;
  static constexpr uint8_t  kRestoreRepeats     = 2;
  static constexpr uint32_t kRestorePassGapMs   = 500;

  static constexpr uint32_t kPerPeerPaceMs        = 5;
  static constexpr uint16_t kBrightnessFadeMs      = 1000;
  static constexpr uint32_t kBrightnessReassertMs  = 40000;

  // A no-ACK unicast holds the radio well past kPerPeerPaceMs, so gating the
  // brightness walk on its own timer alone lets one tick fire several
  // back-to-back. This widens the real inter-unicast gap fleet-wide.
  uint32_t lastUnicastMs_ = 0;
  static constexpr uint32_t kMinUnicastGapMs = 18;
};

}  // namespace wisp

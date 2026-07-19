// PaintDistributor fans out the active palette to every claimed lamp as
// MSG_OVERRIDE_COLORS unicast frames.
//
// Each peer gets one combined frame per cycle with surface=BaseAndShade,
// numColors=2. Both surfaces deliver atomically or both are lost, which
// eliminates the asymmetric per-surface loss seen under BLE coex pressure.
// kPerPeerPaceMs (5 ms) between unicasts keeps ESP-NOW's send queue clear.

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

  // paint:off sends RESTORE_COLORS(BaseAndShade) to every peer.
  void setPaintMode(bool on);
  bool paintMode() const { return paintMode_; }

  // Shuffle seed passed through to TupleSampler. Changing it re-rolls
  // per-lamp color assignments on the next paint walk.
  void setShuffleSeed(uint8_t s) { shuffleSeed_ = s; }

  // intervalMs: how often each lamp is re-targeted [30000..3600000].
  // fadePct: fade length as % of interval [0..100].
  void setDriftInterval(uint32_t intervalMs, uint8_t fadePct);

  // Space-dim factor (0..100) asserted on claimed lamps. Stores the value
  // and kicks an immediate re-assert walk. Independent of paint mode: fires
  // in every source mode incl Off.
  void setBrightness(uint8_t pct);

  void onPaletteChanged();
  void tick(uint32_t nowMs);

 private:
  enum class Mode : uint8_t {
    Idle = 0,
    Paint,     // walking the roster sending MSG_OVERRIDE_COLORS
    Restore,   // walking the roster sending MSG_RESTORE_COLORS
  };

  // Snapshots roster into walkMacs_ so mid-flight HELLO mutations don't bobble the walk.
  void beginWalk(Mode mode);

  void sendPaintToPeer(const uint8_t mac[6]);
  void sendRestoreToPeer(const uint8_t mac[6]);
  void sendDriftToPeer(size_t idx);

  // Snapshot claimed lamps into brWalkMacs_ and arm the paced brightness walk.
  void beginBrightnessWalk();
  void sendBrightnessToPeer(const uint8_t mac[6]);

  // Rebuilds driftMacs_ from claimed inventory, sorts by MAC, recomputes slot.
  // Call on roster changes and on setDriftInterval. With paintNewcomers, any
  // lamp new since the last refresh gets an immediate paint (steady-state
  // join path; the setup callers leave it false since they already walk-paint).
  void refreshDriftRoster(bool paintNewcomers = false);

  LampInventory* inventory_ = nullptr;
  MeshLink* mesh_ = nullptr;
  CurrentPalette* palette_ = nullptr;
  uint8_t shuffleSeed_ = 0;
  // Null roster: every lamp gets painted (used in tests).
  WispRoster* roster_ = nullptr;
  bool paintMode_ = false;
  uint32_t lastSendMs_ = 0;
  uint16_t seqCounter_ = 0;

  Mode walkMode_ = Mode::Idle;
  static constexpr size_t kMaxWalkPeers = 100;  // matches LampInventory::MAX_LAMPS
  uint8_t walkMacs_[kMaxWalkPeers][6];
  size_t walkCount_ = 0;
  size_t walkIdx_ = 0;

  // Restore has no lamp-side keepalive; a lamp only drops paint on a received
  // RESTORE or its 60 s watchdog. Re-cover the release across a bounded window
  // so a run of BLE-coex-dropped frames can't strand a lamp for the full 60 s.
  uint8_t restoreRepeatsLeft_ = 0;
  uint32_t nextRestorePassMs_ = 0;
  static constexpr uint8_t  kRestoreRepeats     = 6;
  static constexpr uint32_t kRestorePassGapMs   = 500;

  uint32_t driftIntervalMs_    = 120000;
  uint8_t  driftFadePct_       = 50;
  uint8_t  driftMacs_[kMaxWalkPeers][6];
  size_t   driftCount_         = 0;
  size_t   driftIdx_           = 0;
  uint32_t driftSlotMs_        = 0;
  uint32_t lastDriftFireMs_    = 0;
  uint32_t lastDriftRosterMs_  = 0;

  // Space-dim brightness re-assert. Its own paced walk buffer so it never
  // clobbers an in-flight paint/restore walk. Dimming (< 100) is held by the
  // periodic re-assert loop; the return to 100 has no re-assert, so it repeats
  // the un-dim walk for redundancy, backed by the lamp's 60 s watchdog.
  uint8_t  brightness_         = 100;
  uint8_t  brWalkMacs_[kMaxWalkPeers][6];
  size_t   brWalkCount_        = 0;
  size_t   brWalkIdx_          = 0;
  bool     brWalkActive_       = false;
  uint32_t lastBrWalkMs_       = 0;
  uint32_t lastBrSendMs_       = 0;

  // Return-to-100 is a one-shot un-dim walk with no re-assert (the re-assert
  // loop is gated < 100) and no lamp-side HELLO keepalive, so a dropped frame
  // strands a lamp dim until its 60 s watchdog. Repeat the un-dim walk.
  uint8_t  brRepeatsLeft_      = 0;
  uint32_t nextBrPassMs_       = 0;

  static constexpr uint32_t kPerPeerPaceMs        = 5;
  static constexpr uint16_t kDefaultFadeDurationMs = 1500;
  static constexpr uint16_t kBrightnessFadeMs      = 1000;
  static constexpr uint32_t kBrightnessReassertMs  = 20000;
};

}  // namespace wisp

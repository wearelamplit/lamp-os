// PaintDistributor — fans out the active palette to every claimed lamp as
// MSG_OVERRIDE_COLORS unicast frames.
//
// Each peer gets one combined frame per cycle with surface=BaseAndShade,
// numColors=2 — both surfaces deliver atomically or both are lost, which
// eliminates the asymmetric per-surface loss seen under BLE coex pressure.
// kPerPeerPaceMs (5 ms) between unicasts keeps ESP-NOW's send queue clear.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class CurrentPalette;
class LampInventory;
struct InventoryEntry;
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

  // Rebuilds driftMacs_ from claimed inventory, sorts by MAC, recomputes slot.
  // Call on roster changes and on setDriftInterval.
  void refreshDriftRoster();

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
  static constexpr size_t kMaxWalkPeers = 32;  // matches LampInventory::MAX_LAMPS
  uint8_t walkMacs_[kMaxWalkPeers][6];
  size_t walkCount_ = 0;
  size_t walkIdx_ = 0;

  uint32_t driftIntervalMs_  = 120000;
  uint8_t  driftFadePct_     = 50;
  uint8_t  driftMacs_[kMaxWalkPeers][6];
  size_t   driftCount_       = 0;
  size_t   driftIdx_         = 0;
  uint32_t driftSlotMs_      = 0;
  uint32_t lastDriftFireMs_  = 0;

  static constexpr uint32_t kPerPeerPaceMs        = 5;
  static constexpr uint16_t kDefaultFadeDurationMs = 1500;
};

}  // namespace wisp

// Per-lamp cache of the last colour target the wisp sent, MAC-keyed so it
// survives driftMacs_ being rebuilt/re-sorted on roster change. Backs the
// keep-alive deadline scheduler in PaintDistributor: every paint (drift,
// newcomer, keep-alive) stamps the lamp's entry here.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wisp {

class LampPaintCache {
 public:
  static constexpr size_t kCapacity = 100;  // matches LampInventory::MAX_LAMPS

  struct Entry {
    uint8_t mac[6] = {0};
    uint8_t base[4] = {0};   // RGBW
    uint8_t shade[4] = {0};  // RGBW
    uint32_t fadeEndMs = 0;
    uint32_t lastPaintMs = 0;
    // Set by markOverdue so keepaliveDue() goes true even inside the first
    // keepaliveMs of uptime, when lastPaintMs==0 alone wouldn't yet satisfy
    // the elapsed check. Cleared on the entry's next stamp/touchLastPaint.
    bool forceDue = false;
  };

  // Records a paint sent to `mac`: the RGBW target, when its fade completes,
  // and when it was sent. Upserts in place if `mac` is already cached, else
  // appends. No-op past kCapacity (bounded by refreshDriftRoster pruning).
  void stamp(const uint8_t mac[6], const uint8_t base[4], const uint8_t shade[4],
             uint32_t fadeEndMs, uint32_t lastPaintMs);

  // Re-stamps only the send time, leaving the cached target and fadeEndMs
  // untouched. A keep-alive re-affirm continues the same trajectory, it
  // doesn't restart the fade clock. No-op if `mac` is unknown.
  void touchLastPaint(const uint8_t mac[6], uint32_t lastPaintMs);

  // Re-opens the keep-alive deadline: a failed send already stamped this
  // entry as if delivered, so clear lastPaintMs and set forceDue so the
  // next scan's keepaliveDue() is true regardless of uptime. Target +
  // fadeEndMs untouched. No-op if `mac` is unknown.
  void markOverdue(const uint8_t mac[6]);

  // Looks up the cached entry for `mac`. False if never painted.
  bool find(const uint8_t mac[6], Entry& out) const;

  // Drops any cached entry not present in `macs` (a lamp that left the roster).
  void pruneToRoster(const uint8_t macs[][6], size_t count);

  size_t size() const { return count_; }

 private:
  size_t indexOf(const uint8_t mac[6]) const;

  Entry entries_[kCapacity];
  size_t count_ = 0;
};

}  // namespace wisp

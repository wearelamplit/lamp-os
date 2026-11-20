// Liveness of the OSC palette stream. A palette marks it live; the target's
// stale window without one marks it stale. The controller holds paint at Off
// while stale and keeps paint mode, so the next palette resumes painting on
// its own. Loop task only.

#pragma once

#include <cstddef>
#include <cstdint>

#include <cstring>

#include "osc/osc_listener.hpp"  // kOscPaletteMaxColors
#include "osc/osc_role.hpp"
#include "osc/osc_target.hpp"
#include "paint/current_palette.hpp"

namespace wisp {

class OscFreshness {
 public:
  // Straylight's UDP feed is change-only with a full restate every 10 s, so a
  // still palette arrives only on the restate. Too close to 10 s and lamps
  // pulse home on every restate cycle.
  static constexpr uint32_t kStaleMs = 30000;

  // Records a palette arrival. True on the stale -> live edge.
  bool onPalette(uint32_t nowMs) {
    lastRxMs_ = nowMs;
    const bool edge = !live_;
    live_ = true;
    return edge;
  }

  // Any other packet from the feed. Keeps a held palette live: over UDP the
  // palette rides last in each restate burst and is the datagram lwIP drops
  // when its queue overflows, while the rest of the feed still arrives.
  // Never makes a stale stream live.
  void onFeed(uint32_t nowMs) {
    if (live_) lastRxMs_ = nowMs;
  }

  // True on the live -> stale edge after `target`'s stale window with no
  // palette. A Maestro target never stales, so its last palette holds until
  // reset().
  bool tick(uint32_t nowMs, const OscTarget& target) {
    const uint32_t staleMs = oscTargetStaleMs(target);
    if (!live_ || staleMs == 0 || nowMs - lastRxMs_ < staleMs) return false;
    live_ = false;
    return true;
  }

  void reset() { live_ = false; }
  bool live() const { return live_; }

 private:
  bool live_ = false;
  uint32_t lastRxMs_ = 0;
};

// The last OSC palette applied and the role it came from, so a repeat only
// refreshes liveness. Every channel counts, white included.
class OscPaletteMemo {
 public:
  // Holds `colours` and `role`, true when either differs from what was held.
  // More than kOscPaletteMaxColors is refused (false) and the held palette
  // stays.
  bool hold(const PaletteColor* colours, size_t count, OscRole role) {
    if (count > kOscPaletteMaxColors) return false;
    const size_t bytes = count * sizeof(PaletteColor);
    if (held_ && role == role_ && count == count_ &&
        std::memcmp(colours, last_, bytes) == 0) {
      return false;
    }
    std::memcpy(last_, colours, bytes);
    count_ = count;
    role_ = role;
    held_ = true;
    return true;
  }

  void clear() { held_ = false; }

 private:
  PaletteColor last_[kOscPaletteMaxColors];
  size_t count_ = 0;
  OscRole role_ = OscRole::Primary;
  bool held_ = false;
};

// True when no channel, white included, is above zero (NaN included), so the
// palette would paint lamps black. Straylight sends zeros for a role with
// nothing bound to it.
inline bool isBlankPalette(const PaletteColor* colours, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    const PaletteColor& c = colours[i];
    if (c.r > 0.0f || c.g > 0.0f || c.b > 0.0f || c.w > 0.0f) return false;
  }
  return true;
}

}  // namespace wisp

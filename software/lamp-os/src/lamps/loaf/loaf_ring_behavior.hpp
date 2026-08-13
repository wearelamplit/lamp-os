#pragma once
#include <cstdint>
#include <vector>

#include "core/animated_behavior.hpp"
#include "util/color.hpp"

namespace lamp { namespace loaf {

// Renders one ring surface as a seamless circular hue sweep through the user's
// own colors at full brightness. Interpolating hue (not linear RGB) keeps every
// pixel bright and fully saturated, so gamma can't crush the dim inter-stop
// mixes to off; the sweep closes the loop (last hue back to the first) so the
// wrap is continuous. When revMs_ != 0 the whole ring rotates once per revMs_.
//
// isBase selects which surface's user stops to read (base vs shade); the
// stops come live from Config, so a picker edit rebuilds the ring.
//
// When companionRevMs != 0 the ring watches the nearby view each control tick
// and targets that faster period while another loaf is near, easing back to
// the base period when it leaves. The period is eased (not stepped) and phase
// accumulates continuously, so a speed change never jumps the rotation.
class LoafRingBehavior : public AnimatedBehavior {
 public:
  LoafRingBehavior(FrameBuffer* fb, bool isBase, uint32_t revMs,
                   uint32_t companionRevMs = 0);

  void control() override;
  void draw() override;

  // Set the revolution period the ring eases toward. 0 = static.
  void setRevolutionMs(uint32_t ms) { targetRevMs_ = ms; }

 private:
  void rebuild();

  bool isBase_;
  uint32_t baseRevMs_;             // period with no loaf companion near (0 = static)
  uint32_t companionRevMs_;        // period while a loaf peer is near (0 = never)
  uint32_t targetRevMs_;           // period draw() eases toward
  float curRevMs_;                 // eased current period
  float phase_ = 0.0f;             // accumulated rotation, [0, 1)
  uint32_t lastDrawMs_ = 0;
  std::vector<Color> stops_;       // last-seen user stops (rebuild trigger)
  std::vector<Color> ring_;        // closed-loop gradient, one Color per pixel
};

}}  // namespace loaf, namespace lamp

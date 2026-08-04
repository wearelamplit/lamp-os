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
class LoafRingBehavior : public AnimatedBehavior {
 public:
  LoafRingBehavior(FrameBuffer* fb, bool isBase, uint32_t revMs);

  void control() override;
  void draw() override;

 private:
  void rebuild();

  bool isBase_;
  uint32_t revMs_;                 // 0 = static (no rotation)
  std::vector<Color> stops_;       // last-seen user stops (rebuild trigger)
  std::vector<Color> ring_;        // closed-loop gradient, one Color per pixel
};

}}  // namespace loaf, namespace lamp

#pragma once
#include <cstdint>
#include <vector>

#include "core/animated_behavior.hpp"
#include "util/color.hpp"

namespace lamp { namespace loaf {

// Renders one ring surface as a seamless circular gradient of the user's own
// colors. A plain linear gradient leaves a hard seam where the ring's last
// pixel meets its first; this closes the loop (interpolating the last stop
// back to the first) so the wrap is continuous. When revMs_ != 0 the whole
// gradient rotates once per revMs_ around the ring.
//
// isBase selects which surface's user stops to read (base vs shade); the
// stops come live from Config, so a picker edit rebuilds the gradient.
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

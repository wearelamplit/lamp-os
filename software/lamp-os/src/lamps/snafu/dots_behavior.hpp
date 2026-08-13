#pragma once
#include <vector>

#include "config/config_types.hpp"
#include "core/animated_behavior.hpp"
#include "util/color.hpp"
#include "util/fast_rng.hpp"

namespace lamp { namespace snafu {

// Segment-aware base-layer scene for the snafu shade. Each FrameBuffer segment
// draws its own palette (config.shade.segments[k].colors) into its own slice of
// the logical buffer. No cross-segment bleed. Builds the sampled scene once
// per cycle in control(); draw() crossfades old→new. Pauses while the wisp
// holds an override so the aurora wash shows through cleanly.
class DotsBehavior : public AnimatedBehavior {
 public:
  static constexpr uint32_t kSceneFrames = 1440;        // ~48 s scatter crossfade
  static constexpr uint32_t kAmbientHoldFrames = 300;   // ~10 s hold between re-scatters (also after an edit)
  static constexpr uint32_t kBorrowSceneFrames = 420;   // ~7 s drift crossfade in borrow mode
  static constexpr uint32_t kBorrowHoldFrames  = 480;   // ~8 s hold between borrow re-scatters; must stay >= kBorrowSceneFrames or the fade snaps

  DotsBehavior(FrameBuffer* fb, const ShadeSettings& cfg,
               uint32_t inFrames = kSceneFrames)
      : AnimatedBehavior(fb, inFrames, /*inAutoPlay=*/true), cfg_(&cfg) {}

  void draw() override;
  void control() override;

  // Wears the peer palette for durationFrames, then auto-reverts. No-op if
  // both stop-lists are empty.
  void borrowColors(const std::vector<Color>& baseStops,
                    const std::vector<Color>& shadeStops,
                    uint32_t durationFrames);

 private:
  const ShadeSettings* cfg_;
  bool sceneChange_ = false;
  std::vector<Color> prev_, cur_;
  // Snapshot of the palettes cur_ was built from. A mismatch means the user
  // edited a color, so the scene snaps to it immediately (live preview) instead
  // of drifting in on the slow ambient crossfade.
  std::vector<std::vector<Color>> lastColors_;
  // Per-segment pixel scatter. Stable across color edits so a palette change
  // recolors the dots in place; only the ambient cycle re-rolls it.
  std::vector<std::vector<uint16_t>> perm_;
  // Frames held since the last scatter. Gates the ambient re-scatter and is
  // reset on a color edit so editing recolors in place without shuffling.
  uint32_t sinceShuffle_ = 0;
  FastRng rng_;

  bool borrowActive_ = false;
  uint32_t borrowElapsed_ = 0;
  uint32_t borrowDuration_ = 0;
  std::vector<Color> borrowBase_, borrowShade_;
  // Own-palette equivalent of cur_ during borrow, melt target for draw()'s
  // second-half crossfade. Rebuilt only alongside cur_, never per frame.
  std::vector<Color> ownMelt_;

  // Full-buffer scene: each segment's slice filled from its own palette,
  // arranged by perm_.
  std::vector<Color> buildScene();
  std::vector<Color> buildSceneWith(bool useBorrow);
  bool anyMultiColor() const;
  bool colorsChanged();
  void ensurePerm();
  void reshuffle();
};

}}  // namespace snafu, namespace lamp

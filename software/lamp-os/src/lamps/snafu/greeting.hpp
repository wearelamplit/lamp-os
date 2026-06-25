#pragma once
#include <string>
#include "core/animated_behavior.hpp"
#include "components/network/nearby_lamps.hpp"
#include "util/color.hpp"

namespace lamp { namespace snafu {

// Replaces built-in SocialBehavior for SnafuLamp. Watches NearbyLamps for
// new arrivals via firstSeenMs edge detection; on first sighting plays a
// glitch phase then fades in the arriving peer's base color, holds, then
// fades out. Ported from legacy snafu.py::GlitchedSocialGreeting.
//
// Uses NearbyLamp::firstSeenMs instead of Python's `await network.arrived()`.
// Edge condition: if (peer.firstSeenMs >= lastTickMs_) the peer just appeared.
class Greeting : public AnimatedBehavior {
 public:
  Greeting(FrameBuffer* inFb)
    : AnimatedBehavior(inFb, /*inFrames=*/300, /*inAutoPlay=*/false) {}

  void draw() override;
  void control() override;

 private:
  static constexpr uint32_t kGlitchFrames = 23;
  static constexpr uint32_t kEaseFrames   = 60;
  static constexpr uint32_t kFadeInFrames = kGlitchFrames + kEaseFrames;

  // Glitch gradient: precomputed once on first control() from
  // (0,45,200,0) → (180,0,60,0) matching legacy snafu.py::GlitchedSocialGreeting.
  std::vector<Color> glitchColors_;
  uint32_t glitchOffset_ = 0;

  Color arrivedColor_;
  std::string lastGreetedBdAddr_;
  uint32_t lastTickMs_ = 0;
};

} }  // namespace snafu, namespace lamp

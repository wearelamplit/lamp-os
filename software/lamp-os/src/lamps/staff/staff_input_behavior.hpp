#pragma once

#include <cstdint>

#include "components/input/button.hpp"
#include "components/input/touch.hpp"
#include "config/config.hpp"
#include "core/animated_behavior.hpp"
#include "lamps/staff/unlock_combo.hpp"

namespace staff {

// Drives every local staff gesture off the injected input FSMs. A logic-only
// AnimatedBehavior: it stays STOPPED (contributes no pixels) so the compositor
// ticks control() each frame without drawing. Edge events (bound in the ctor)
// mutate mode state; control() runs the continuous held gestures (mood scrub,
// pad brightness ramps) via the BehaviorContext facade.
//
// The top pad trims the shade surface and the bottom pad trims the base
// surface via BehaviorContext::setSurfaceBrightness; the stoke click drives
// master brightness and clears both trims on the way to full.
class StaffInputBehavior : public lamp::AnimatedBehavior {
 public:
  StaffInputBehavior(lamp::FrameBuffer* fb, lamp::Config& config,
                     lamp::Button* stoke,
                     lamp::Touch* topPad, lamp::Touch* bottomPad);

  void control() override;
  void draw() override {}  // logic-only; never draws (stays STOPPED)

 private:
  void onStoke(lamp::ButtonEvent e);
  void onTopPad(lamp::TouchEvent e);
  void restoreConfiguredColors();

  lamp::Config& config_;
  lamp::Button* stoke_;
  lamp::Touch* topPad_;
  lamp::Touch* bottomPad_;

  UnlockCombo unlock_;

  uint16_t moodHue_;
  bool moodActive_ = false;
  bool stoked_ = false;
  bool scrubbing_ = false;
  uint8_t configuredBrightness_;
  uint32_t lastScrubMs_ = 0;
};

}  // namespace staff

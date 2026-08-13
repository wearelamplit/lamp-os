#pragma once
#include <functional>
#include <string>
#include <vector>

#include "behaviors/greetable.hpp"       // GreetingState
#include "core/animated_behavior.hpp"
#include "util/color.hpp"

namespace lamp { class ExpressionManager; struct PeerView; }

namespace lamp { namespace lioness {

// Arrival greeting for the Lions strip. Starts STOPPED (inAutoPlay=false), so
// the compositor skips its draw() while idle and the ambient base scene shows
// through. On a new near arrival (latest-wins) it snaps all three lions to the
// newcomer's peer.baseColor and breathes kPulses times (pulseEnvelope), eases back into the
// ambient pixels drawn beneath it this frame, and fires a GlitchyExpression on
// the shade. Registered via b.add only.
class LionsGreetingBehavior : public AnimatedBehavior {
 public:
  explicit LionsGreetingBehavior(FrameBuffer* baseFb);

  void control() override;
  void draw() override;

  void startGreeting(const lamp::PeerView& peer);
  lamp::GreetingState greetingState() const;

  void setExpressionManager(lamp::ExpressionManager* e) { expr_ = e; }
  void setOnGreetingChangeCallback(std::function<void()> fn) { onGreetingChange_ = std::move(fn); }

 private:
  int lionsSegIndex_ = -1;
  Color greetColor_;
  std::string greetLampId_;
  bool greetingWasActive_ = false;

  lamp::ExpressionManager* expr_ = nullptr;
  std::function<void()> onGreetingChange_;
};

}}  // namespace lioness, namespace lamp

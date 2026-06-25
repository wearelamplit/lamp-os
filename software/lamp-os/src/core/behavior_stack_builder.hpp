// software/lamp-os/src/core/behavior_stack_builder.hpp
#pragma once
#include <vector>
#include "core/animated_behavior.hpp"

namespace lamp {

class BehaviorStackBuilder {
 public:
  void add(AnimatedBehavior* b) { behaviors_.push_back(b); }
  const std::vector<AnimatedBehavior*>& behaviors() const { return behaviors_; }
 private:
  std::vector<AnimatedBehavior*> behaviors_;
};

}  // namespace lamp

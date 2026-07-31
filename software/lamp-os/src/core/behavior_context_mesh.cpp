#include "core/behavior_context.hpp"

#include "components/network/mesh/mesh_link.hpp"
#include "core/arrival_notifier.hpp"

namespace lamp {

void BehaviorContext::onArrival(std::function<void(const PeerView&)> cb) {
  arrivalNotifier.onArrival(std::move(cb));
}

}  // namespace lamp

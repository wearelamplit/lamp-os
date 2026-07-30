#include "core/behavior_context.hpp"

#include "components/network/mesh/mesh_link.hpp"
#include "components/network/protocol/bid.hpp"
#include "core/arrival_notifier.hpp"

namespace lamp {

void BehaviorContext::requestGreeting() {
  if (!meshLink) return;
  meshLink->sendBid(lamp_protocol::BID_GREETING);
}

void BehaviorContext::onArrival(std::function<void(const PeerView&)> cb) {
  arrivalNotifier.onArrival(std::move(cb));
}

}  // namespace lamp

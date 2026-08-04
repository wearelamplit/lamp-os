#include "core/behavior_context.hpp"

#include <Arduino.h>

#include "components/network/mesh/lamp_roster.hpp"
#include "config/config.hpp"
#include "core/personality_engine.hpp"

extern lamp::Config config;

namespace lamp {

void BehaviorContext::forEachArrival(
    uint32_t maxAgeMs, const std::function<bool(const PeerView&)>& cb) {
  if (!lampRoster) return;
  RosterEntry arrival;
  if (!lampRoster->bestUngreetedArrival(
          maxAgeMs, millis(),
          [](const RosterEntry&) { return true; }, arrival)) {
    return;
  }
  PeerView v = PeerView::from(arrival);
  if (cb(v)) lampRoster->acknowledge(arrival.mac);
}

void BehaviorContext::forEachNearby(
    const std::function<bool(const PeerView&)>& cb) {
  if (!lampRoster) return;
  // Snapshot before invoking `cb`: a callback re-entering getNear/getMesh/getAll
  // fills a distinct buffer, so this snapshot stays valid across the walk.
  const std::vector<NearbyCopy>& near =
      lampRoster->snapshotNear(LAMP_PRUNE_TIME_MS);
  for (const NearbyCopy& e : near) {
    PeerView v = PeerView::make(e.name, e.mac, e.hasMac, e.baseColor,
                                e.shadeColor, e.lastRssi, e.variant);
    if (cb(v)) return;
  }
}

GreetingTuning BehaviorContext::greetingFor(const std::string& peerLampId) const {
  return personalityEngine.greetingFor(peerLampId);
}

uint8_t BehaviorContext::dispositionOf(const std::string& peerLampId) const {
  return ::config.getDisposition(peerLampId);
}

CrowdComposition BehaviorContext::crowd() const {
  return personalityEngine.crowdComposition();
}

float BehaviorContext::crowdWeight() const {
  return personalityEngine.smoothedCrowdWeight();
}

}  // namespace lamp

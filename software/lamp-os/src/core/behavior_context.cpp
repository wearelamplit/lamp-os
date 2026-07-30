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
  for (const RosterEntry& e : lampRoster->getNear(LAMP_PRUNE_TIME_MS)) {
    PeerView v = PeerView::from(e);
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

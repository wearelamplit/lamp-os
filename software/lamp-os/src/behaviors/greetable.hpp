#pragma once

#include <string>

namespace lamp {

struct NearbyLamp;  // fwd-decl — full include is only needed by implementors.

// Snapshot of the active greeting. Populated while an animation plays;
// cleared when it stops. peerBdAddr is the canonical uppercase colon-hex
// BD_ADDR of the greeted peer (empty when idle). kind is a stable short
// label: "warm", "reserved", "snub" (SocialBehavior) or "glitch" (snafu).
struct GreetingState {
  bool        active      = false;
  std::string peerBdAddr;
  std::string kind;
};

// Implemented by any behavior that can play a greeting for a specific peer
// on demand. Two real implementations: SocialBehavior (default) and
// snafu::Greeting. Inverted here so the framework dispatch never switches
// on lamp type.
struct Greetable {
  virtual void         triggerGreeting(const NearbyLamp& peer) = 0;
  virtual GreetingState greetingState() const = 0;
  virtual ~Greetable() = default;
};

}  // namespace lamp

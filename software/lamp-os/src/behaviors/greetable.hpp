#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lamp {

struct PeerView;  // fwd-decl; full definition lives in core/behavior_context.hpp.
class Color;

// Snapshot of the active greeting. Populated while an animation plays;
// cleared when it stops. peerLampId is the canonical uppercase colon-hex
// mac of the greeted peer (empty when idle). kind is a stable short
// label: "warm", "reserved", "snub" (SocialBehavior), "glitch" (snafu), or
// "pulse" (Lioness).
struct GreetingState {
  bool        active      = false;
  std::string peerLampId;
  std::string kind;
};

// Implemented by any behavior that can play a greeting for a specific peer
// on demand. Two real implementations: SocialBehavior (default) and
// snafu::Greeting. Inverted here so the framework dispatch never switches
// on lamp type.
struct Greetable {
  virtual void         triggerGreeting(const PeerView& peer) = 0;
  virtual GreetingState greetingState() const = 0;
  // Peer's base + shade color stops from a MSG_COLOR_INFO reply. Default
  // no-op: a greeting that doesn't render peer palettes ignores it.
  virtual void onColorInfo(const uint8_t srcMac[6],
                           const std::vector<Color>& baseStops,
                           const std::vector<Color>& shadeStops) {}
  virtual ~Greetable() = default;
};

}  // namespace lamp

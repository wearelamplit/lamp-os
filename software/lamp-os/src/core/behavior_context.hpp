#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "util/color.hpp"

namespace lamp {

// Defined in core/hw_config.hpp; the opaque declaration keeps this header
// dependency-light while letting the facade name it by value.
enum class Surface : uint8_t;

// Forward declarations keep this header dependency-light so it can be
// included from animated_behavior.hpp without dragging in compositor/manager
// definitions.
class Compositor;
class ConfiguratorBehavior;
class ExpressionManager;
class FrameBuffer;
class LampRoster;
class MeshLink;
struct Greetable;
struct GreetingTuning;
struct CrowdComposition;
struct RosterEntry;

/**
 * Non-owning view of one roster peer handed to forEach* visitors. `name`
 * and `mac` point into the live roster entry and are valid only for the
 * duration of the callback. `lampId` is the canonical colon-hex mac (empty
 * when !hasMac). char-based identity keeps the roster's heavy header out of
 * behavior translation units and the 17-char address off the heap.
 */
struct PeerView {
  const char* name = "";
  const uint8_t* mac = nullptr;  // 6 bytes; valid only during the callback
  bool hasMac = false;
  Color baseColor;
  Color shadeColor;
  int8_t rssi = -127;
  char lampId[18] = {0};

  // Single conversion from a roster entry. `mac` aliases the entry, so the
  // view outlives it only as long as the entry does.
  static PeerView from(const RosterEntry& e);

  // Build from raw fields (the roster-snapshot iteration path, which holds a
  // stack copy rather than a RosterEntry reference). `name` and `mac` must
  // outlive the view.
  static PeerView make(const char* name, const uint8_t* mac, bool hasMac,
                       Color baseColor, Color shadeColor, int8_t rssi);
};

/**
 * Per-behavior context wired by the Compositor at register time.
 * The Compositor owns one instance; every behavior it registers points
 * at that single instance. Mutating a field (e.g. ExpressionManager
 * wiring up the frame buffer list after begin()) is observed by every
 * behavior without re-pointing.
 */
struct BehaviorContext {
  Compositor* compositor = nullptr;
  ExpressionManager* expressionManager = nullptr;
  // ExpressionManager populates [shade, base] in begin() so
  // Expression::shouldAffectBuffer() can route by target without a global.
  std::vector<FrameBuffer*> expressionFrameBuffers;
  // Transient color overrides drive their target colors + fade
  // duration through ConfiguratorBehavior::beginFade() so per-pixel fade
  // stays a single source of truth in the configurator's draw loop. The
  // standard lamp wires these at setup; ColorOverride::bind() routes to
  // the right surface (base vs shade) at apply time.
  ConfiguratorBehavior* baseConfigurator = nullptr;
  ConfiguratorBehavior* shadeConfigurator = nullptr;
  // Mesh identity surface for custom behaviors. Wired by the framework
  // during boot before any AnimatedBehavior::control() runs. Behaviors
  // must null-check (consistent with existing field policy;
  // expressionManager nullability is the precedent).
  LampRoster* lampRoster = nullptr;
  // Active greeting behavior, or null when the lamp has none. The triggerGreet
  // handler routes here so dispatch never switches on lamp type.
  Greetable* greeting = nullptr;
  // Mesh send surface for custom behaviors. Wired at boot alongside lampRoster.
  // Behaviors must null-check (same field policy as lampRoster/greeting).
  MeshLink* meshLink = nullptr;

  // Hand the strongest-RSSI ungreeted near arrival to `cb`. `cb` returns true
  // when it delivered a greeting; the roster acknowledges that peer. A false
  // return leaves the peer retriable next call (the un-acked arrival is the
  // retry token). No auto-ack on visitation. maxAgeMs bounds how recent a near
  // sighting counts as an arrival. One arrival per call; the next tick picks up
  // the next-strongest, so the ~60 Hz greeting path never snapshots the roster.
  void forEachArrival(uint32_t maxAgeMs,
                      const std::function<bool(const PeerView&)>& cb);

  // Visit currently-near peers, strongest-RSSI first. `cb` returns true to
  // stop early. Read-only; never acks.
  void forEachNearby(const std::function<bool(const PeerView&)>& cb);

  // Per-peer greeting waveform for `peerLampId` (colon-hex mac).
  GreetingTuning greetingFor(const std::string& peerLampId) const;
  // Stored disposition tier for `peerLampId` (3 = neutral default).
  uint8_t dispositionOf(const std::string& peerLampId) const;
  // Current near-crowd grouped by disposition.
  CrowdComposition crowd() const;
  // Smoothed weighted-crowd value before the per-mode floor/curve.
  float crowdWeight() const;

  // Physical author facade (per-tick, from input behaviors). setSolidColor is
  // a non-allocating in-place fill of both surface configurators (the mood
  // scrub hot path); setGradient uses the vector fade path (called rarely).
  // setBrightness routes through the user-brightness micro-fade so it stacks
  // with crowd-dim and the OTA pulse, never a raw strip write. Definitions
  // live in lamp.cpp alongside the configurators + brightness machinery.
  void setSolidColor(Color c);
  void setGradient(const std::vector<Color>& stops);
  void setBrightness(uint8_t level);
  // Per-surface brightness trim (0-100 percent) riding under master
  // brightness. Scales that surface down only, composing with the slider,
  // wisp override, and crowd-dim; 100 clears the trim.
  void setSurfaceBrightness(Surface surface, uint8_t level);

  // Register a push callback fired once per genuinely-new near peer. Attach-
  // once at boot; the callback runs on Core 1, receives a PeerView valid only
  // for the call, and filters by disposition itself. Dedup + re-arm live in
  // the framework's ArrivalNotifier, decoupled from the greeting ack.
  // Definitions of the three mesh-facade methods live in
  // core/behavior_context_mesh.cpp.
  void onArrival(std::function<void(const PeerView&)> cb);
};

}  // namespace lamp

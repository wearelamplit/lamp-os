#pragma once

#include <vector>

namespace lamp {

// Forward declarations — keep this header dependency-light so it can be
// included from animated_behavior.hpp without dragging in compositor/manager
// definitions.
class Compositor;
class ConfiguratorBehavior;
class ExpressionManager;
class FrameBuffer;
class NearbyLamps;
struct Greetable;

/**
 * @brief Per-behavior context wired by the Compositor at register time.
 *        Replaces the hidden singleton globals (globalCompositor,
 *        globalExpressionManager, expressionFrameBuffers) with an explicit
 *        pointer that the Compositor sets via AnimatedBehavior::setBehaviorContext
 *        the moment a behavior is registered.
 *
 *        The Compositor owns one instance; every behavior it registers points
 *        at that single instance. Mutating a field (e.g. ExpressionManager
 *        wiring up the frame buffer list after begin()) is observed by every
 *        behavior without re-pointing.
 */
struct BehaviorContext {
  Compositor* compositor = nullptr;
  ExpressionManager* expressionManager = nullptr;
  // Mirrors the previous `expressionFrameBuffers` extern: ExpressionManager
  // populates [shade, base] in begin() so Expression::shouldAffectBuffer()
  // can route by target without grabbing a global.
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
  // must null-check (consistent with existing field policy —
  // expressionManager nullability is the precedent).
  NearbyLamps* nearbyLamps = nullptr;
  // Active greeting behavior, or null when the lamp has none. The triggerGreet
  // handler routes here so dispatch never switches on lamp type.
  Greetable* greeting = nullptr;
};

}  // namespace lamp

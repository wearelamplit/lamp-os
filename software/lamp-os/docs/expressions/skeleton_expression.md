# Skeleton Expression Example

Minimal walkthrough for adding a new expression type to the firmware.
Pairs with `docs/expressions.md` (subsystem-level architecture and
the parameter contract) — read that first.

## Header (skeleton_expression.hpp)

```cpp
#pragma once

#include "expression.hpp"
#include "primitives.hpp"  // only if you use Window/Sparsity/Size

namespace lamp {

class SkeletonExpression : public Expression {
 private:
  Region window_;          // optional — opt in to the Window primitive
  uint32_t myTempoMs_ = 1000;

 public:
  using Expression::Expression;

  SkeletonExpression(FrameBuffer* inBuffer, uint32_t inFrames = 30)
      : Expression(inBuffer, inFrames) {
    allowedInHomeMode = true;
  }

  // Pure type properties — override only if your expression differs
  // from the default (false = coexists with the wisp's hold colour).
  bool disabledDuringWispOverride() const override { return false; }

  // Read your custom keys from the generic parameters map. The map is
  // std::map<std::string, uint32_t>; use lamp::getParam to read with a
  // default, or one of the primitive helpers (Region/Sparsity/parseSize)
  // for spatial knobs.
  void configureFromParameters(
      const std::map<std::string, uint32_t>& params);

  void draw() override;

 protected:
  void onTrigger() override;
  void onUpdate() override;   // optional
  void onComplete() override; // optional
};

}  // namespace lamp
```

## Implementation (skeleton_expression.cpp)

```cpp
#include "skeleton_expression.hpp"

namespace lamp {

void SkeletonExpression::configureFromParameters(
    const std::map<std::string, uint32_t>& params) {
  // Custom tempo — your own param key, your own default.
  myTempoMs_ = lamp::getParam(params, "myTempo", 1000);

  // Spatial primitive — opt in only if your expression has a Window.
  const uint16_t pc = (fb && fb->pixelCount > 0)
      ? static_cast<uint16_t>(fb->pixelCount) : 0;
  window_ = Region::fromParameters(params, pc);
}

void SkeletonExpression::onTrigger() {
  // Snapshot palette + any state you need for this trigger.
}

void SkeletonExpression::onUpdate() {
  // Per-frame state advance — runs every loop tick while PLAYING.
  // Don't allocate here; allocate in onTrigger().
}

void SkeletonExpression::draw() {
  if (!shouldAffectBuffer()) { nextFrame(); return; }

  for (uint16_t i = window_.posMin; i <= window_.posMax; ++i) {
    fb->buffer[i] = getRandomColor();  // your effect goes here
  }

  nextFrame();
}

void SkeletonExpression::onComplete() {
  // Optional — runs the tick after animationState returns to STOPPED.
}

}  // namespace lamp
```

## Registering with the factory

Open `software/lamp-os/src/expressions/expression_factory.cpp` and add
two things:

1. An include near the other type includes:

```cpp
#include "skeleton_expression.hpp"
```

2. A dispatch branch in `makeExpression`:

```cpp
} else if (type == "skeleton") {
  auto e = std::make_unique<SkeletonExpression>(buffer, 30);
  e->configure(colors, intervalMin, intervalMax, target);
  e->configureFromParameters(parameters);
  expr = std::move(e);
}
```

That's the only place in firmware that has to know about your type.
**Do not edit `expression_manager.cpp`** — the manager is type-agnostic
and the factory is the type registry.

## Parameter contract

Custom parameters live in the generic
`std::map<std::string, uint32_t>` that ExpressionConfig owns. Read them
in `configureFromParameters` via:

- `lamp::getParam(params, "key", defaultValue)` — for arbitrary per-type
  knobs (tempo, duration, intensity, etc.)
- `lamp::Region::fromParameters(params, pixelCount)` — for the Window
  primitive (`posMin` / `posMax`)
- `lamp::Sparsity::fromParameters(params, windowSize)` — for the
  Count + Wander primitive (`count` / `wander`)
- `lamp::parseSize(params, windowSize, defaultValue)` — for the Size
  primitive (`size`)

Every primitive helper clamps to safe bounds before returning, so
draw() never sees an index that would walk past `fb->buffer`.

Parameter keys go on the wire and into NVS unchanged. Pick stable names —
prefix with your expression's name if there's any risk of collision
(`pulseSpeed`, not `speed`), reuse the shared primitive keys (`posMin`,
`posMax`, `count`, `wander`, `size`) where they apply.

## App-side metadata

In `software/lamp-app-flutter/lib/features/lamp_shell/domain/expression_meta.dart`,
add an `ExpressionTypeMeta` entry to the `all` list. `key` must match
the factory's type string verbatim. `defaultParameters` must match the
firmware defaults so a fresh entry behaves identically to a no-params
instance.

If your expression has parameter UI (sliders, switches), add a branch
to the type-switch in
`software/lamp-app-flutter/lib/features/lamp_shell/presentation/widgets/expression_params_panel.dart`,
reusing the shared widget builders (`_buildPositionRange`,
`_buildCount`, `_buildWander`, `_buildSize`) for the primitive knobs.

## Key points

1. **`shouldAffectBuffer()` first** — respects target (shade/base/both)
   and the wisp-override gate.
2. **`nextFrame()` always** — advances `frame` and handles completion.
   Calling it twice is not safe; calling it zero times stalls the
   animation.
3. **Compose, don't take over** — expressions draw AFTER the
   configurator. Your buffer writes are the final visible state for
   the pixels you touch. Pixels you don't touch keep whatever the
   earlier layers (idle, fade_in) wrote.
4. **No allocation in `onUpdate` / `draw`** — they run every loop tick.
   Allocate in `onTrigger` and reuse.
5. **Continuous expressions override `control()`** — the base class's
   default `control()` auto-triggers on interval and tears down on
   STOPPED. Continuous expressions (Breathing, Drifty) stay PLAYING
   forever and own the wisp-override gate themselves.

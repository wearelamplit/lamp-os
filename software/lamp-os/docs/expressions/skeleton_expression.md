# Skeleton Expression Example

Minimal walkthrough for adding a new expression type to the firmware.
Pairs with `docs/dev/expressions.md` (subsystem-level architecture and
the parameter contract) — read that first.

## Header (skeleton_expression.hpp)

```cpp
#pragma once

#include "expression.hpp"

namespace lamp {

class SkeletonExpression : public Expression {
 private:
  uint32_t myTempoMs_ = 1000;   // a custom per-type knob

 public:
  SkeletonExpression(FrameBuffer* inBuffer, uint32_t inFrames = 30)
      : Expression(inBuffer, inFrames) {
    allowedInHomeMode = true;
  }

  // Pure type property — override only if your expression differs from the
  // default (false = coexists with the wisp's hold colour).
  bool disabledDuringWispOverride() const override { return false; }

  // Read your custom keys out of the generic parameters map (the map is
  // std::map<std::string, uint32_t>; read each key with parameters.find()).
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

The base class already gives you `colors` (the configured palette,
`std::vector<Color>`), `getRandomColor()`, a per-instance `rng` (`FastRng`),
`fb` (your `FrameBuffer*`), `shouldAffectBuffer()`, and `nextFrame()`.

## Implementation (skeleton_expression.cpp)

```cpp
#include "skeleton_expression.hpp"

namespace lamp {

void SkeletonExpression::configureFromParameters(
    const std::map<std::string, uint32_t>& params) {
  // Custom tempo — your own param key, your own default. Read each key
  // with find() and fall back to a default when it's absent.
  auto it = params.find("myTempo");
  myTempoMs_ = (it != params.end()) ? it->second : 1000;
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

  for (int i = 0; i < fb->pixelCount; ++i) {
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

Open `software/lamp-os/src/expressions/expression_manager.cpp` and add two
things to the `makeExpression()` static factory:

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

`makeExpression()` is the one place in firmware that knows the full
expression-type list — it lives in `expression_manager.cpp` itself (there is
no separate factory translation unit).

## Parameter contract

Custom parameters live in the generic `std::map<std::string, uint32_t>` that
`ExpressionConfig` owns. Read them in `configureFromParameters` with
`params.find("key")`, falling back to a default when the key is absent
(integer-only — store floats as fixed-point and document the units).

Parameter keys go on the wire and into NVS unchanged. Pick stable names —
prefix with your expression's name if there's any risk of collision
(`pulseSpeed`, not `speed`). If a key is a dedicated top-level field rather
than a free parameter, it must be added to the reserved-keys skip-chain in
`config_codec.cpp::fromJson` **and** the app's `_reservedKeys` set in
`sections.dart` (see `docs/dev/expressions.md`).

## App-side metadata

In `software/lamp-app-flutter/lib/features/lamp_shell/domain/expression_meta.dart`,
add an `ExpressionTypeMeta` entry to the `all` list. `key` must match the
factory's type string verbatim. `defaultParameters` must match the firmware
defaults so a fresh entry behaves identically to a no-params instance.

If your expression has parameter UI (sliders, switches), add a `case` to the
`switch (type)` in
`software/lamp-app-flutter/lib/features/lamp_shell/presentation/widgets/expression_params_panel.dart`
returning a per-type widget (the shipped ones are `_BreathingParams`,
`_PulseParams`, `_ShiftyParams`, `_GlitchyParams`).

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
   STOPPED. A continuous expression (e.g. `BreathingExpression`) stays
   PLAYING forever and, if it wants to honour the wisp-override flag,
   checks `isWispCurrentlyOverriding()` itself.

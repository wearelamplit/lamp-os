# Expressions

This doc is for developers modifying the lamp's expressions subsystem or adding a new expression type.

## Subsystem map

Expressions are firmware-side animations the lamp auto-triggers on its own cadence, independent of the app being connected. The list is persisted in NVS as part of `Config::expressions` and round-trips through the app via `CHAR_EXPRESSION_SECTION` (read) and `CHAR_SETTINGS_BLOB` (write).

Files:

| File | Purpose |
|---|---|
| `software/lamp-os/src/expressions/expression.hpp` | Base class + lifecycle contract |
| `software/lamp-os/src/expressions/expression.cpp` | `control()` driver, wisp-override gate |
| `software/lamp-os/src/expressions/expression_manager.{hpp,cpp}` | Owns the list, factory in `makeExpression()`, cascade dedup, mesh recv path |
| `software/lamp-os/src/expressions/expression_factory.{hpp,cpp}` | `makeExpression` type dispatch — the only firmware file that knows the full expression-type list |
| `software/lamp-os/src/expressions/primitives.{hpp,cpp}` | Shared Window / Sparsity / Size readers used by primitive-aware expressions |
| `software/lamp-os/src/expressions/{breathing,glitchy,pulse,shifty}_expression.{hpp,cpp}` | The four shipped subclasses |
| `software/lamp-os/src/config/config_types.hpp` | `ExpressionConfig` struct (persisted form) |
| `software/lamp-os/src/config/config.cpp` | JSON serialisation + parsing; reserved-keys filter |
| `software/lamp-app-flutter/lib/features/control/domain/sections.dart` | App-side `ExpressionConfig` mirror; same reserved-keys filter |
| `software/lamp-app-flutter/lib/features/lamp_shell/domain/expression_meta.dart` | Per-type metadata (picker labels, default parameters, default wisp-gate flag) |
| `docs/mesh-api.md` (MSG_EVENT section) | Cascade wire format + stagger semantics + dedup window |

## `Expression` base class contract

`Expression` derives from `AnimatedBehavior` and is plugged into the `Compositor`, which ticks every registered behaviour every loop iteration. Subclasses override these:

- `void onTrigger()` — **required**. Called once when `trigger()` fires (auto-interval, chain, or manual test). Read `colors[]`, `target`, the per-instance parameter map, and any subclass state. Schedule the first frame by setting `animationState = PLAYING` (or `PLAYING_ONCE`).
- `void onUpdate()` — optional. Called every loop tick while `animationState == PLAYING || PLAYING_ONCE`. Mutate the frame buffer for the current pixel/frame. Set `animationState = STOPPED` when done.
- `void onComplete()` — optional. Called the tick after `animationState` transitions back to `STOPPED`. Use it to restore any state you snapshotted in `onTrigger` (most expressions hand the buffer back to the configurator's render and don't need this).
- `void control()` — overridable but not required. The base class implementation handles the auto-trigger cadence, the wisp-override gate, `onUpdate` dispatch, and `onComplete` dispatch. Subclasses that override `control()` (e.g. `BreathingExpression`, which is continuous and never enters `STOPPED`) take on the responsibility of those four behaviours themselves.

## Frame buffer + targets

`target` is a bitmask: `TARGET_SHADE = 1`, `TARGET_BASE = 2`, `TARGET_BOTH = 3`. The manager builds **one Expression instance per surface**: a `target=3` config produces two `Expression` instances (one bound to `shadeBuffer`, one to `baseBuffer`), each with its own state. They auto-trigger in the same loop tick, with `RecentCascade` dedup keeping a single mesh-cascade fan-out per logical trigger.

`Expression::fb` is the `FrameBuffer*` the instance writes to. Don't capture `shadeBuffer` / `baseBuffer` from the manager — use the one already wired on `this`.

## Adding a new expression type — minimum viable diff

1. **New subclass** in `software/lamp-os/src/expressions/foo_expression.{hpp,cpp}`. Derive from `Expression`, implement `onTrigger()` (and `onUpdate()` if continuous). Look at `glitchy_expression.cpp` for a brief-flash pattern; `breathing_expression.cpp` for a continuous always-running pattern.
2. **Factory arm** in `expression_factory.cpp::makeExpression()`. Add the `#include` for your type's header and an `else if (type == "foo")` branch that constructs the subclass, calls `configure(colors, intervalMin, intervalMax, target)`, then `configureFromParameters(parameters)`. **Do not edit `expression_manager.cpp`** — the manager is type-agnostic.
3. **Reserved keys** — if your subclass takes type-specific parameters, **add the keys to BOTH** the firmware parser at `config.cpp::Config::Config()` (the long `keyStr == "..."` chain that skips reserved fields) **and** the app-side `_reservedKeys` set in `sections.dart::ExpressionConfig`. Without both, your params leak into the generic `parameters` map and round-trip in a way that surprises later readers.
4. **App-side metadata** in `expression_meta.dart::ExpressionTypeMeta.all`. Add an entry with `key`, `name`, `icon`, `tagline`, `description`, `defaultParameters` (must match firmware), and `defaultDisabledDuringWispOverride` (see below).
5. **Param UI** (optional) in `lib/features/lamp_shell/presentation/widgets/expression_params_panel.dart` — add a case for your type if it has parameters that need sliders. The panel reads the keys verbatim; defaults fall back to `defaultParameters`.

That's it. No protocol bump, no NVS migration. The settings_blob path picks up the new fields on first save.

## Parameter contract

`ExpressionConfig::parameters` is a `std::map<std::string, uint32_t>`. Integer-only on purpose: keeps the NVS budget bounded, simplifies the JSON decoder, and matches what the existing UI (sliders, steppers) actually produces. If you need a float, store it as fixed-point (milliseconds, hundredths) and document the units in the parameter's call site.

The reserved-keys exclusion filter is what makes "spread the parameters into the top-level JSON object" round-trip cleanly. Every reserved field is its own top-level key with a dedicated decoder; everything else falls into `parameters`. The lists must match between firmware and app — keep them in sync at the same commit.

Constraint to know: existing parameters are read by name in the subclass via `getParameter(name, default)`. Choose names that won't collide with future top-level fields. Prefix with the expression's name if there's any ambiguity (e.g. `pulseSpeed`, not `speed`).

## Spatial primitives

Three shared helpers in `expressions/primitives.hpp` let expressions
opt into the same spatial vocabulary. Each reads from the generic
`parameters` map and clamps to safe bounds; subclasses never see invalid
pixel indices.

| Primitive | Param keys | Concept | Used by |
|---|---|---|---|
| `Region` | `posMin`, `posMax` | Zoning constraint — which pixels the effect is allowed to operate on | Drifty; Shifty (post-PR plan); Breathing (planned) |
| `Sparsity` | `count`, `wander` | How many independent points; do they re-pick positions per cycle | Drifty |
| `parseSize` | `size` | How many pixels each effect-point occupies | Drifty; Pulse |

Subclasses opt in from `configureFromParameters`:

```cpp
window_   = Region::fromParameters(params, fb->pixelCount);
sparsity_ = Sparsity::fromParameters(params, window_.size());
size_     = parseSize(params, window_.size(), defaultSize);
```

Convention: when a primitive is opted into, the corresponding param
keys MUST round-trip through the Flutter editor. Add UI for them in
`expression_params_panel.dart` via the shared widget builders
(`_buildPositionRange`, `_buildCount`, `_buildWander`, `_buildSize`).

## Trigger cadence

`Expression::control()` (in `expression.cpp:54`) checks `millis() > nextTriggerMs` and fires `trigger()` if so. After every fire `Expression::trigger()` sets `nextTriggerMs = millis() + rand_uint32_t(intervalMinMs, intervalMaxMs)`. Subclasses that override `control()` (breathing) are continuous and don't gate on `nextTriggerMs`.

`enabled = false` clears `autoTriggerEnabled` at load time, which suppresses the auto-trigger in `control()`. Manual `trigger()` from the test path still works.

## Wisp-override gate (`disabledDuringWispOverride`)

When true, the expression's auto-trigger is skipped while the wisp is actively overriding either surface. The check, in `expression.cpp::isWispCurrentlyOverriding()`, queries the two override globals:

```cpp
bool isWispCurrentlyOverriding() {
  if (baseColorOverride.isActive() &&
      baseColorOverride.activeSource() == lamp_protocol::OverrideSource::Wisp) return true;
  if (shadeColorOverride.isActive() &&
      shadeColorOverride.activeSource() == lamp_protocol::OverrideSource::Wisp) return true;
  return false;
}
```

Semantics:

- Gate fires at trigger time only. Expressions already in `PLAYING` keep running through their natural duration.
- Skipped triggers don't queue. The gate pushes `nextTriggerMs` forward by `intervalMinMs` so a long-running wisp hold doesn't accumulate a backlog of triggers that all fire the instant the wisp lets go.
- Manual trigger paths (the test button in the editor, mesh cascade arrivals via `triggerInvocation`) are **not gated** — deliberate operator/social actions outrank the gate.
- Defaults: `breathing` and `shifty` → true (they paint continuously and fight the wisp's hold). `glitchy` and `pulse` → false (brief flashes / waves coexist with a held wisp colour). Type-aware parse default lives in **both** `config.cpp` and `sections.dart` — keep them in sync.
- **Not user-editable.** The `disabledDuringWispOverride` toggle was removed from the expression editor UX (commits 3575f43 + 0c3c5b3). The per-type default above is now authoritative — operators can't override it. The field persists in `ExpressionConfig` for serialization compatibility, but the editor UI no longer surfaces it. Custom-lamp / power-user reintroduction is a future possibility but not currently planned.

## Mesh cascade integration

When `parameters["cascadeEnabled"] == 1`, a local trigger fans out a matching `MSG_EVENT` to every reachable peer, staggered by `parameters["cascadeStaggerMs"]`. The structural loop break is in `triggerInvocation` — remote-arrived triggers are dispatched to a transient one-shot Expression instance and **never cascade**. See `docs/mesh-api.md` (MSG_EVENT section) for the wire format, the gossip-relay rule, and the per-msgType DedupRing (64 slots).

## Testing

Host-side tests live in `software/lamp-os/test/native/` and run via `pio test -e native`. Existing coverage:

- `test_personality_engine.cpp` — auto-trigger cadence, enable/disable
- `test_transient_override.cpp` — the ColorOverride state machine the wisp gate consults
- `test_cascade_dedup.cpp` — `RecentCascade` ring keying and eviction

To add a host-side test for a new expression: instantiate it with a stub `FrameBuffer` (see `personality_engine_test.cpp` for the pattern), call `onTrigger()`, then assert on the buffer's pixel state. You don't need a real Compositor — `Expression::control()` is the only thing that requires one and you can drive `onUpdate()` directly in the test.

## Gotchas

- **Reserved-keys mismatch.** Adding a top-level field without updating both `config.cpp` and `sections.dart`'s `_reservedKeys` will leak the field into `parameters` map. Round-trips look fine but the field gets silently demoted on the next read.
- **`target` is a bitmask, not an enum.** 1=shade, 2=base, 3=both. Mixing up the bits compiles fine and produces "expression only paints half the lamp" symptoms.
- **The visible output is not your buffer.** Expressions paint into the configurator's frame buffer. If a `ColorOverride` is `Holding`, the strip shows the override fade, not your writes. Test in isolation (with the wisp off or `disabledDuringWispOverride` true) OR clear overrides manually before debugging.
- **No allocation in `onUpdate()`.** It runs every loop tick (every ~4-8 ms during PLAYING). Allocate in `onTrigger()`, reuse buffers across frames. The existing expressions follow this pattern; copy them.
- **Continuous expressions own the loop.** Subclasses that override `control()` (breathing) don't get the base class's wisp-override gate for free — they have to check `isWispCurrentlyOverriding()` themselves if they want to honour the flag. Today no continuous expression does this; if you add one that should, fold the check into your override.

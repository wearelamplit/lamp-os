# Expressions

This doc is for developers modifying the lamp's expressions subsystem or adding a new expression type.

## Subsystem map

Expressions are firmware-side animations the lamp auto-triggers on its own cadence, independent of the app being connected. The list is persisted in NVS as part of `Config::expressions` and round-trips through the app: the app reads the `expr` section via the page protocol (`CHAR_PAGE_CTRL`/`CHAR_PAGE_DATA`) and writes the full settings JSON via `CHAR_SETTINGS_BLOB`.

Files:

| File | Purpose |
|---|---|
| `software/lamp-os/src/expressions/expression.hpp` | Base class + lifecycle contract |
| `software/lamp-os/src/expressions/expression.cpp` | `control()` driver, wisp-override gate |
| `software/lamp-os/src/expressions/expression_manager.{hpp,cpp}` | Owns the entry list + one-shot transients, `makeExpression()` (registry-driven), cascade dedup, mesh recv path |
| `software/lamp-os/src/expressions/{glitchy,pulse,breathing,shifty,spotty}/*_expression.{hpp,cpp}` | The five shipped subclasses; each declares its own `ExpressionDescriptor` |
| `software/lamp-os/src/expressions/primitives.hpp` | Shared Zone/Points/Size/Scatter clamped helpers |
| `software/lamp-os/src/expressions/param_utils.hpp` | `getParam` — one-arg (descriptor keys, always present after `applyDefaults`) and two-arg (explicit fallback) lookups |
| `software/lamp-os/src/expressions/expression_schema.hpp` | `ExpressionDescriptor` / `ParamSpec` / `Bound` / `ColorSpec` / `RangeSpec` — the per-type schema each subclass declares |
| `software/lamp-os/src/expressions/expression_registry.{hpp,cpp}` | `ExpressionRegistry`: `add`/`remove`/`find`, `applyDefaults()`, `serializeCatalog()` (the `exprcat` wire JSON) |
| `software/lamp-os/src/core/lamp.cpp` | `Lamp::registerExpressions()` — the default set (`reg.add(T::descriptor())`); a variant overrides it |
| `software/lamp-os/src/config/config_types.hpp` | `ExpressionConfig` class (persisted form) |
| `software/lamp-os/src/config/config_codec.cpp` | JSON serialisation + parsing; the top-level-field skip chain (`keyStr == …` in `fromJson`) |
| `software/lamp-app-flutter/lib/features/control/domain/sections.dart` | App-side `ExpressionConfig` mirror; the matching `_reservedKeys` skip set |
| `software/lamp-app-flutter/lib/features/lamp_shell/domain/expression_catalog.dart` | App-side `exprcat` parse (descriptors, `Bound` resolution) |
| `software/lamp-app-flutter/lib/features/lamp_shell/presentation/widgets/expression_params_panel.dart` | Generic renderer — builds the editor from a descriptor |
| `software/lamp-app-flutter/lib/features/lamp_shell/domain/expression_presentation.dart` | Client-only presentation (icon, tagline) keyed by catalog id |
| `software/lamp-ui/src/components/expressions/` | Vue web UI: renders a subset of the catalog (`ExpressionConfig.vue`, `ExpressionsList.vue`, `catalog.ts`) |
| `docs/dev/networking.md` (MSG_EVENT section) | Cascade wire format + stagger semantics + dedup window |

## App editor — firmware-driven

The firmware is the single source of truth for the editor. On connect the app reads the **`exprcat`** page-section (alongside `lamp`/`base`/`shade`/`expr`/`home`) and renders every control generically from the returned descriptors — there is no hand-maintained per-type schema in the app. `expression_params_panel.dart` walks a descriptor and emits: the whole-strip/region toggle (optional zone), enum segmented controls, the zone range + live preview, int sliders (bounds resolved against the target strip's pixel count), the duration range, the interval range, and the cascade controls. Labels, ranges, units, and which controls exist all come from the catalog.

The editor shell (`expression_editor_screen.dart`) owns only colors + target; `expression_presentation.dart` supplies the client-side icon/tagline per type (keyed by catalog id — the only per-type thing still hand-authored, and it's presentation, not schema). The wisp-pause behaviour is a catalog field (`pausesWispOverride`), mirroring the firmware's `disabledDuringWispOverride()`.

### `exprcat` wire format

`{ "schemaVersion": 1, "expressions": [ <descriptor>… ] }`. Each descriptor (emitted by `ExpressionRegistry::serializeCatalog`, parsed by `expression_catalog.dart`):

- `id`, `name`, `continuous` (bool); `pausesWispOverride?` (bool, present only when true — mirrors the firmware's `disabledDuringWispOverride()`); `colors: {max, label?, help?, inheritsSurface?}`.
- `interval?` / `duration?`: `{min, max, step, unit, default: [lo, hi], label?, minKey?, maxKey?}`. `minKey`/`maxKey` name the instance keys the range writes; only `duration` carries them (into the **params map**). `interval` has none — the client writes it to the instance's **top-level** `intervalMin`/`intervalMax`.
- `zone?`: `{}` (present) or `{optional: true}` (whole-strip/region toggle). Absent = no zone.
- `excludeTargets?`: `[surface…]`; `defaultTarget?`: surface. Surface strings only, see **Target vocabulary**.
- `params`: `[ {key, type("int"|"enum"), label, min, max, step, default, unit?, invert?, leftLabel?, rightLabel?, requiresZoning?, options?: [{value, label, zoning?}]} ]`.

Within `params`, `max` and `default` are **structured `Bound`s**: a plain number (literal), `{"rel":"pixels"}` (= target surface pixel count), or `{"rel":"pixels","cap":N}` (= `min(pixelCount, N)`), resolved client-side against the target surface. `min` and `step` are plain ints. `colors.max` is a plain int, **not** a `Bound`.

### Zoning truth table

"Zoning active" is derived by the client (`expression_params_panel.dart::_zoningActive`), in precedence order:

1. **Zoning enum present** (any `params` entry with an option carrying `zoning:true`): zoning is active iff the currently-selected option's `zoning` is true. Shifty's `fillMode` is the case — selecting `Up`/`Down`/`Bloom` zones, `Uniform` doesn't.
2. Else **optional zone** (`zone.optional:true`): a whole-strip/region toggle drives it; zoning active iff the synthesized `fullStrip == 0` (region).
3. Else **plain zone** (`zone:{}`, no optional, no zoning enum): always zoned.

When zoning is active the client renders the zone (`posMin`/`posMax`) range control and any `requiresZoning:true` params; when inactive it hides both.

### ColorSpec state machine

`colors.max` is the palette cap. Client behavior:

- No color on the wire for a new instance → the client rolls a single random color on create (the firmware never ships a default palette).
- `inheritsSurface:true` → an **empty** palette is valid and means "follow the surface's live colors"; the min palette size is 0. Otherwise the min is 1 (at least one color).
- A user-set palette is any length in `[min, max]`. Firmware `getRandomColor()` picks per-trigger from whatever colors the instance carries, falling back to dim white (`kSafeFallbackColor`) when empty.

### Target vocabulary

Two distinct vocabularies:

- The persisted instance `target` is the `ExpressionTarget` bitmask: `1=shade`, `2=base`, `3=both`. One `Expression` instance is built per surface.
- `defaultTarget` / `excludeTargets` use **surface strings** from the firmware `Surface` enum: `shade`, `base`, `aux0`. `excludeTargets` is a type-level constraint (surfaces this expression can't run on); `defaultTarget` is a new-instance hint. No stock expression sets either today; both are honored end-to-end if a variant descriptor does.

### Client conventions (not in the schema)

Hard contract between firmware and every client, read by the firmware but deliberately absent from the descriptor — a client author has to know these:

- `fullStrip` — synthesized by the client from the optional-zone region toggle: `1` = whole strip, `0` = region. The firmware reads it (`getParam(parameters, "fullStrip", 1)`).
- `cascade` (`cascadeEnabled` / `cascadeStaggerMs`) — client-hardcoded controls, shown for `continuous:false` expressions in dev mode (and kept visible once set, so they stay editable). Firmware reads them in `maybeCascade`.

**Merge-patch on save.** Any client MUST edit a copy of the raw params map in place and set only the keys it touched — never rebuild the params from its rendered fields. Keys the client never surfaced (a zone hidden behind a toggle, another client's cascade config) would otherwise be dropped. This is a data-loss contract, not a nicety.

**Live zone preview.** Dragging a Zone slider writes a `test_zone_preview` action to `CHAR_EXPRESSION_TEST` (`{a, posMin, posMax, target, color}`). The lamp lights the selected zone in the expression's color and blanks the rest; releasing the slider restores normal rendering.

### Web subset degradation

The Vue web UI (`software/lamp-ui/src/components/expressions/`) renders a **subset**: colors, interval, duration, plain `int` sliders, and `enum` selects. It **drops** zoning (zone control + `requiresZoning` params) and the cosmetic `invert`/`leftLabel`/`rightLabel` decoration. Because it obeys the merge-patch contract above, an instance the Flutter app zoned round-trips through the web UI with its zone config intact — the web UI just doesn't show it. A subset client that rebuilt params from rendered fields would silently strip the app's zoning on the next save.

## `Expression` base class contract

`Expression` derives from `AnimatedBehavior` and is plugged into the `Compositor`, which ticks every registered behaviour every loop iteration. Subclasses override these:

- `void onTrigger()`, **required**. Called once when `trigger()` fires (auto-interval, chain, or manual test). Read `colors[]`, `target`, the per-instance parameter map, and any subclass state. Schedule the first frame by setting `animationState = PLAYING` (or `PLAYING_ONCE`).
- `void draw()`, the compositor's per-tick render hook (from `AnimatedBehavior`). Every shipped expression overrides `draw()` — it's where pixels actually get written, gated on `shouldAffectBuffer()`. `draw()` runs every tick regardless of `animationState`; guard your paint on the state yourself.
- `void onUpdate()`, optional. Called by `control()` every loop tick while `animationState == PLAYING || PLAYING_ONCE`. Advance per-frame animation state here. Set `animationState = STOPPED` when done.
- `void onComplete()`, optional. Called the tick after `animationState` transitions back to `STOPPED`. Use it to restore any state you snapshotted in `onTrigger` (most expressions hand the buffer back to the configurator's render and don't need this).
- `void control()`, overridable but not required. The base class implementation handles the auto-trigger cadence, the wisp-override gate, `onUpdate` dispatch, and `onComplete` dispatch. Subclasses that override `control()` (e.g. `BreathingExpression`, which is continuous and never enters `STOPPED`) take on the responsibility of those four behaviours themselves.

## Frame buffer + targets

`target` is a bitmask: `TARGET_SHADE = 1`, `TARGET_BASE = 2`, `TARGET_BOTH = 3`. The manager builds **one Expression instance per surface**: a `target=3` config produces two `Expression` instances (one bound to `shadeBuffer`, one to `baseBuffer`), each with its own state. They auto-trigger in the same loop tick, with `RecentCascade` dedup keeping a single mesh-cascade fan-out per logical trigger.

`Expression::fb` is the `FrameBuffer*` the instance writes to. Don't capture `shadeBuffer` / `baseBuffer` from the manager, use the one already wired on `this`.

## Adding a new expression type: minimum viable diff

1. **New subclass** in `software/lamp-os/src/expressions/foo/foo_expression.{hpp,cpp}` (each expression gets its own directory). Derive from `Expression`, override `draw()` (where you paint) and `onTrigger()`; add `onUpdate()`/`onComplete()`/`control()` as the effect needs. Implement `configureFromParameters(const std::map<std::string, uint32_t>&)` to read your params. Read them with the **one-arg** `getParam(parameters, "key")` — `applyDefaults` has already folded every descriptor key into the map, so a miss is a schema bug, not a missing preset. Look at `glitchy/glitchy_expression.cpp` for a brief-flash pattern, `breathing/breathing_expression.cpp` for a continuous always-running pattern, `spotty/spotty_expression.cpp` for an interval-triggered ambient effect that pauses under wisp override.
2. **Descriptor** — declare an inline `static constexpr ExpressionDescriptor` in `foo_expression.cpp` (see the shipped types) with id, name, `colors`, optional `interval`/`duration`, zone flags, `params`, and `.make = &makeExpr<FooExpression>`. Expose it via a `static const ExpressionDescriptor& descriptor()` accessor. Every wire field (and the whole editor) derives from this; there is no separate app-side schema. Return `pausesWispOverride`/`continuous` here rather than the app — and override `disabledDuringWispOverride()` on the class to match if you set `pausesWispOverride`.
3. **Register it** — add `reg.add(FooExpression::descriptor())` to `Lamp::registerExpressions` in `lamp.cpp`. `ExpressionManager::makeExpression` then finds the descriptor by id, builds the instance via `.make`, and folds defaults before `configureFromParameters`. No factory dispatch to edit.
4. **App presentation** (optional) — add an entry to `expression_presentation.dart` keyed by your `id` for the picker icon + tagline. Skip it and the type falls back to a generic icon and no tagline; every control still renders from the descriptor.

Type-specific params ride the generic `parameters` map automatically — you do **not** touch the reserved-key skip lists (those cover only the fixed top-level fields, see **Parameter contract**). That's it: no protocol bump, no NVS migration. The settings_blob path picks up the new fields on first save, and the app picks up the new controls the next time it reads `exprcat`.

### Custom-lamp override

A variant owns its own set. Put the subclass + descriptor under `software/lamp-os/src/lamps/<variant>/` and override `registerExpressions` in the variant's `Lamp` subclass: call `reg.add(MyExpression::descriptor())` for its own types and `reg.remove("glitchy")` (etc.) to drop a stock one. The framework `registerExpressions` in `lamp.cpp` is the default the variant replaces or extends; framework code never includes variant headers (see the variant-include hygiene rule in `CLAUDE.md`).

## Parameter contract

`ExpressionConfig::parameters` is a `std::map<std::string, uint32_t>`. Integer-only on purpose: keeps the NVS budget bounded, simplifies the JSON decoder, and matches what the UI (sliders, steppers, segmented enums) produces. If you need a float, store it as fixed-point (milliseconds, hundredths) and document the units at the parameter's call site.

Each expression instance serializes as a flat JSON object: a **fixed set of top-level fields** (`type`, `enabled`, `intervalMin`, `intervalMax`, `target`, `colors`) each with a dedicated decoder, plus every other key spread in from the `parameters` map. On read, a key is a top-level field iff it's in the skip chain — the `keyStr == …` list in `config_codec.cpp::fromJson` and the matching `_reservedKeys` set in `sections.dart`; everything else lands in `parameters`. `disabledDuringWispOverride` is in both skip lists too: it's a pure type-property (never NVS-loaded), and the entry just tolerates and drops it from old blobs.

Those skip lists are **fixed** — adding a per-type param does not touch them, since params flow through `parameters` by definition. You only edit both (at the same commit) if you add a genuinely new top-level field. A param key must not collide with a top-level field name; prefix with the expression's name if there's any ambiguity (e.g. `pulseSpeed`, not `speed`).

## Shared expression primitives

`primitives.hpp` provides four clamped ingress helpers shared across all expression types. Absent params produce identity behavior; present params clamp to valid range at `configureFromParameters` time. `windowSize` is `fb->pixelCount` at configure time.

| Primitive | Param key(s) | Clamp range | Absent default |
|---|---|---|---|
| Zone | `posMin`, `posMax` | [0, windowSize-1]; reversed bounds swap | 0 .. windowSize-1 (full strip) |
| Points | `count` | [1, windowSize] | per-expression (see table below) |
| Size | `size` | [1, windowSize] | per-expression |
| Scatter | `scatter` | [0, 100] | 0 (synchronized) |

**Identity invariant.** A config with no params set renders identically to one with all params at their defaults.

### Per-expression primitive support

| Expression | Zone | Points | Size | Scatter | Notes |
|---|---|---|---|---|---|
| Pulse | ✓ | — | ✓ (default 15) | — | |
| Glitchy | ✓ | ✓ (default 1) | ✓ (default 1) | — | `fullStrip=1` (default) glitches the whole strip; set `fullStrip=0` to glitch Zone/Points/Size points instead |
| Breathing | ✓ | ✓ (default 1) | ✓ (default windowSize) | ✓ | |
| Shifty | ✓ | — | — | — | See `fillMode` below |
| Spotty | ✓ | ✓ (default 3) | ✓ (default 4) | — | Wandering ambient points; `disabledDuringWispOverride` = true. Speed (`spotSpeed` 1..10 s) scales each spot's fade-in/hold/fade-out lifetime. |

### Shifty `fillMode`

`fillMode` controls the per-pixel wavefront order during both fade-to-color and fade-back transitions. Pixels outside the Zone are unaffected.

| Value | Behavior |
|---|---|
| 0 | Uniform — all pixels fade simultaneously (default) |
| 1 | Up — first pixel in zone leads, sweeps toward last |
| 2 | Down — last pixel leads, sweeps toward first |
| 3 | Bloom — center pixels lead on fade-in; outside-in on fade-back |

## Trigger cadence

`Expression::control()` checks `millis() > nextTriggerMs` and fires `trigger()` if so. After every fire the schedule is reset with `nextTriggerMs = millis() + rng.range(intervalMinMs, intervalMaxMs)` (`rng` is the per-instance `FastRng`). Subclasses that override `control()` (breathing) are continuous and don't gate on `nextTriggerMs`.

`enabled = false` clears `autoTriggerEnabled` at load time, which suppresses the auto-trigger in `control()`. Manual `trigger()` from the test path still works.

## Wisp-override gate (`disabledDuringWispOverride`)

When true, the expression's auto-trigger is skipped while the wisp is actively overriding either surface. The check, in `expression.cpp::isWispCurrentlyOverriding()`, queries the two override globals:

```cpp
bool isWispCurrentlyOverriding() {
  if (lamp::overrides.base.isActive() &&
      lamp::overrides.base.activeSource() == lamp_protocol::OverrideSource::Wisp) return true;
  if (lamp::overrides.shade.isActive() &&
      lamp::overrides.shade.activeSource() == lamp_protocol::OverrideSource::Wisp) return true;
  return false;
}
```

Semantics:

- Gate fires at trigger time only. Expressions already in `PLAYING` keep running through their natural duration.
- Skipped triggers don't queue. The gate pushes `nextTriggerMs` forward by `intervalMinMs` so a long-running wisp hold doesn't accumulate a backlog of triggers that all fire the instant the wisp lets go.
- The *cadence* push-forward above is auto-trigger only. But every trigger path — auto, the editor test button, and mesh cascade arrivals via `triggerInvocation` — still runs through `trigger()` → `shouldAffectBuffer()`, which returns false (paints nothing) while `disabledDuringWispOverride()` *and* a wisp hold are both active. So a breathing/shifty cascade that lands mid-wisp-hold is suppressed too; glitchy/pulse (gate=false) always paint.
- Defaults: `breathing`, `shifty`, and `spotty` → true (they paint over the surface the wisp holds). `glitchy` and `pulse` → false (brief flashes / waves coexist with a held wisp colour). The value is a pure type-property: each class's `disabledDuringWispOverride()` override is authoritative, and the descriptor's `pausesWispOverride` mirrors it onto the wire. It is never parsed from config — `config_codec.cpp` and `sections.dart` only skip the key if an old blob carries it.
- **Not user-editable.** No editor control surfaces this — the per-type default is authoritative, operators can't override it. The field is skipped on parse and never emitted for a new instance; a variant that wants per-instance control would add it as a real top-level field.

## Mesh cascade integration

When `parameters["cascadeEnabled"] == 1`, a local trigger fans out a matching `MSG_EVENT` to every reachable peer, staggered by `parameters["cascadeStaggerMs"]`. The structural loop break is in `triggerInvocation`, remote-arrived triggers are dispatched to a transient one-shot Expression instance and **never cascade**. See `docs/dev/networking.md` (MSG_EVENT section) for the wire format, the gossip-relay rule, and the per-msgType DedupRing (64 slots).

## Testing

Host-side tests are per-folder PlatformIO suites under `software/lamp-os/test/` (each `test_<name>/` dir is its own suite; there is no single `test/native/` path). Run them with `npm run lamp:test`. Existing coverage:

- `test/test_personality_engine/personality_engine.cpp`, auto-trigger cadence, enable/disable
- `test/test_transient_override/transient_override.cpp`, the ColorOverride state machine the wisp gate consults
- `test/test_cascade_dedup/cascade_dedup.cpp`, `RecentCascade` ring keying and eviction
- `test/test_expression_primitives/expression_primitives.cpp`, Zone/Points/Size/Scatter primitive helpers

To add a host-side test for a new expression: instantiate it with a stub `FrameBuffer` (see the personality-engine suite for the pattern), call the public `trigger()` to fire `onTrigger()`, then call `draw()` per frame and assert on the buffer's pixel state. You don't need a real Compositor — `Expression::control()` (the auto-cadence driver) is the only thing that requires one.

## Gotchas

- **Reserved-keys mismatch.** Adding a top-level field without updating both `config_codec.cpp` and `sections.dart`'s `_reservedKeys` will leak the field into the `parameters` map. Round-trips look fine but the field gets silently demoted on the next read. Per-type params never need this — only genuinely new top-level fields do.
- **`target` is a bitmask, not an enum.** 1=shade, 2=base, 3=both. Mixing up the bits compiles fine and produces "expression only paints half the lamp" symptoms.
- **The visible output is not your buffer.** Expressions paint into the configurator's frame buffer. If a `ColorOverride` is `Holding`, the strip shows the override fade, not your writes. Test in isolation (with the wisp off or `disabledDuringWispOverride` true) OR clear overrides manually before debugging.
- **No allocation in `onUpdate()`.** It runs every loop tick (every ~4-8 ms during PLAYING). Allocate in `onTrigger()`, reuse buffers across frames. The existing expressions follow this pattern; copy them.
- **Continuous expressions own the loop.** Subclasses that override `control()` (breathing) don't get the base class's wisp-override gate for free, they have to check `isWispCurrentlyOverriding()` themselves if they want to honour the flag. Today no continuous expression does this; if you add one that should, fold the check into your override.

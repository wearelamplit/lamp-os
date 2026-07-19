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
| `software/lamp-os/src/expressions/primitives.hpp` | Shared Zone/Points/Size clamped helpers + `resolveZone` (whole-strip/region toggle → Zone, shared by every zonable expression) + `pulseWidthFromPercent` (pulse `size` percent → capped fade radius) + `glitchBlockPlan` (glitchy scatter level→distinct grain blocks) + `usableSections` (breathing bands that fit the zone at ≥ `kMinSectionPx`) + `edgeTaper` (edge-taper weight: flat interior, curve-parameterized taper near the ends; `TaperCurve::Linear` or `Quadratic`) + `randomPermutation` (Fisher–Yates fill of a 0..n-1 order array over an injected rng; breathing's random band order) |
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
- `params`: `[ {key, type("int"|"enum"), label, min, max, step, default, unit?, invert?, leftLabel?, rightLabel?, help?, requiresZoning?, options?: [{value, label, zoning?}]} ]`.

Within `params`, `max` and `default` are **structured `Bound`s**: a plain number (literal), `{"rel":"pixels"}` (= target surface pixel count), or `{"rel":"pixels","cap":N}` (= `min(pixelCount, N)`), resolved client-side against the target surface. `min` and `step` are plain ints. `colors.max` is a plain int, **not** a `Bound`.

### Zoning truth table

The zone is a universal **whole-strip/region toggle** on every zonable expression: it selects which part of the strip the effect paints, honored in every mode. "Zoning active" is derived by the client (`expression_params_panel.dart::_zoningActive`), in precedence order:

1. **Zoning enum present** (any `params` entry with an option carrying `zoning:true`): zoning is active iff the currently-selected option's `zoning` is true. No shipped expression uses this rung; the app keeps the mechanism, and it stays a valid encoding.
2. Else **optional zone** (`zone.optional:true`): the whole-strip/region toggle drives it; zoning active iff the synthesized `fullStrip == 0` (region). Every zonable shipped expression is on this rung.
3. Else **plain zone** (`zone:{}`, no optional, no zoning enum): always zoned.

When zoning is active the client renders the zone (`posMin`/`posMax`) range control and any `requiresZoning:true` params; when inactive it hides both. Firmware resolves the toggle via `resolveZone(parameters, window)` in `primitives.hpp`: `fullStrip=1` (default) spans the whole strip and ignores any stale `posMin`/`posMax` left behind in Region mode; `fullStrip=0` honors them.

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

- `void onTrigger()`, **required**. Called once when `trigger()` fires (auto-interval, chain, or manual test) — `trigger()` returns false and skips it when the buffer routing or wisp gate rejects the start. Read `colors[]`, `target`, the per-instance parameter map, and set up subclass state (`frames`, `frame`, allocations). Don't set `animationState` here — `trigger()` calls `playOnce()` right after `onTrigger()` returns, overwriting anything you set.
- `void draw()`, the compositor's render hook (from `AnimatedBehavior`). Every shipped expression overrides `draw()` — it's where pixels actually get written, gated on `shouldAffectBuffer()`. The compositor calls it once per flush window (~16 ms) while the behavior isn't `STOPPED`; `STOPPED` behaviors are never drawn.
- `void onUpdate()`, optional. Called by `control()` once per flush window while `animationState == PLAYING || PLAYING_ONCE`. Advance animation state here. Set `animationState = STOPPED` when done.
- `void onComplete()`, optional. Called the tick after `animationState` transitions back to `STOPPED`. Use it to restore any state you snapshotted in `onTrigger` (most expressions hand the buffer back to the configurator's render and don't need this).
- `void control()`, overridable but not required. The base class implementation handles the auto-trigger cadence, the wisp-override gate, `onUpdate` dispatch, and `onComplete` dispatch. Subclasses that override `control()` take on the responsibility of those four behaviours themselves; the continuous ones (breathing, spotty) route through the protected `Expression::continuousControl()` helper for the first two.

### Timing convention: wall clock, not frames

The compositor's flush cadence (`MINIMUM_FRAME_DRAW_TIME_MS`) is not a
stable timebase — it runs near its target rate steady but dips during BLE
coex windows — so **no user-facing duration is counted in frames**. Every labeled
duration (glitch duration, shifty fade + hold, spot lifetime, breath cycle,
pulse transit) derives from `millis()` deltas, breathing's `updateBreathPhase`
being the reference pattern. The frame counter exists only as the
`AnimatedBehavior` completion plumbing, driven by two idioms from
`primitives.hpp`:

- `rewindBeforeExhaust(frame, frames)` right before `nextFrame()` keeps the
  counter from ever ending an animation whose end belongs to wall-clock (or
  wave-position) state — steady-state continuous instances rewind forever.
- Setting `frames = frame + 1` concludes: the next `nextFrame()` flips
  `PLAYING_ONCE` → `STOPPED` and increments `currentLoop`, so `onComplete`
  fires through the normal base-class path.

Deadline comparisons use the wrap-safe `timeReached(now, deadline)` from
`primitives.hpp`, never `millis() > deadline`. Glitchy additionally guarantees
at least one painted frame even when its duration is shorter than a flush
window.

## Frame buffer + targets

`target` is a bitmask: `TARGET_SHADE = 1`, `TARGET_BASE = 2`, `TARGET_BOTH = 3`. The manager builds **one Expression instance per surface**: a `target=3` config produces two `Expression` instances (one bound to `shadeBuffer`, one to `baseBuffer`), each with its own state. They auto-trigger in the same loop tick, with `RecentCascade` dedup keeping a single mesh-cascade fan-out per logical trigger.

`Expression::fb` is the `FrameBuffer*` the instance writes to. Don't capture `shadeBuffer` / `baseBuffer` from the manager, use the one already wired on `this`.

## Adding a new expression type: minimum viable diff

1. **New subclass** in `software/lamp-os/src/expressions/foo/foo_expression.{hpp,cpp}` (each expression gets its own directory). Derive from `Expression`, override `draw()` (where you paint) and `onTrigger()`; add `onUpdate()`/`onComplete()`/`control()` as the effect needs. Implement `configureFromParameters(const std::map<std::string, uint32_t>&)` to read your params. Read them with the **one-arg** `getParam(parameters, "key")` — `applyDefaults` has already folded every descriptor key into the map, so a miss is a schema bug, not a missing preset. Look at `glitchy/glitchy_expression.cpp` for an interval-triggered brief-flash pattern, `breathing/breathing_expression.cpp` for a continuous always-running pattern, `spotty/spotty_expression.cpp` for a continuous effect with independent per-point lifecycles that pauses under wisp override.
2. **Descriptor** — declare the descriptor data as `inline constexpr` in `foo_expression.hpp` (see the shipped types: `kFooDescriptorData`, make-less) with id, name, `colors`, optional `interval`/`duration`, zone flags, and `params`; in the `.cpp`, compose the registered descriptor with `withMake(kFooDescriptorData, &makeExpr<FooExpression>)` and expose it via a `static const ExpressionDescriptor& descriptor()` accessor. The split is a native-test seam: `test_builtin_descriptors` registers the header data directly (the `.make` factory can't link without Arduino), so a descriptor change fails the pinned catalog instead of drifting. Every wire field (and the whole editor) derives from this; there is no separate app-side schema. Return `pausesWispOverride`/`continuous` here rather than the app — and override `disabledDuringWispOverride()` on the class to match if you set `pausesWispOverride`.
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

`primitives.hpp` provides three clamped ingress helpers shared across all expression types. Absent params produce identity behavior; present params clamp to valid range at `configureFromParameters` time. `windowSize` is `fb->pixelCount` at configure time.

| Primitive | Param key(s) | Clamp range | Absent default |
|---|---|---|---|
| Zone | `posMin`, `posMax` | [0, windowSize-1]; reversed bounds swap | 0 .. windowSize-1 (full strip) |
| Points | `count` | [1, windowSize] | per-expression (see table below) |
| Size | `size` | [1, windowSize] | per-expression |

**Identity invariant.** A config with no params set renders identically to one with all params at their defaults.

### Per-expression primitive support

| Expression | Zone | Points | Size | Notes |
|---|---|---|---|---|
| Pulse | ✓ | — | ✓ (`size` %) | `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. `size` is a **percent** of the zone, not pixels: the fade radius is `pulseWidthFromPercent()` in `primitives.hpp`, capped below the full zone (so even at max the wave still visibly travels) with a small pixel floor. Wave transit is wall-clock (`pulseSpeed` scaled per pixel); the pulse ends when the wave exits the zone, never on the frame counter |
| Glitchy | ✓ | — | — | `scatter` (always active) sets grain and density. Its lowest level is a solid static fill of the active region held for the duration; higher levels scatter into progressively finer, sparser flecks (per-level density/grain in `kGlitchScatter`, `primitives.hpp`). Blocks occupy **distinct** grain slots (`slotCount = region / grain`, `blocksWanted = round(density% of slots)`), so realized density is exact rather than collision-capped. `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. Each active frame repaints from the saved background, so scattered levels re-roll into a stable dancing density; the solid level reads as a steady fill. `durationMin`/`durationMax` are milliseconds of wall clock; every glitch paints ≥1 frame. The grain-block plan derives from `glitchBlockPlan()` in `primitives.hpp` |
| Breathing | ✓ | — | — | `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. The whole zone always breathes at full coverage. `breathSpeed` runs between a fast floor (faster reads as hectic) and a slow ceiling. `sections` splits the zone into N contiguous equal bands; the bands fire in a **random staggered order** so the breath hops around the strip instead of sweeping it. `onTrigger`/`configure` resolves `usableSections_` and a Fisher–Yates permutation (`sectionOrder_`) over the expression's `rng`, fixed per instance (re-rolls on retrigger). Each band's phase offset is `sectionOrder_[band] * kBreathStaggerFrac` (`primitives.hpp`), a controlled overlap where the next breath-order section begins its fade-in as the previous nears the end of its fade-out; 1 = uniform whole-zone breath. `usableSections()` in `primitives.hpp` clamps N down so every band spans at least a minimum pixel width. Steady-state breathing never restarts; phase accrues from `millis()` deltas indefinitely. The zone's outer edges are soft: per-pixel intensity is scaled by an `edgeTaper()` run over a virtual region a couple pixels wider with the offset shifted in one, putting the darkest step off-screen so the outermost real pixel reads the brighter second step (both ends shift symmetrically; interior stays full). Timing is driven by `breathPhase`; the taper only weights the spatial per-pixel intensity |
| Shifty | ✓ | — | — | See `fillMode` below. Fades (`fadeDuration`) and the hold (`shiftDurationMin/Max`) are pure `millis()` deadlines; the frame counter cannot end a fade or a hold |
| Spotty | ✓ | ✓ | ✓ (Small↔Large slider) | `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. Continuous wandering ambient points; `disabledDuringWispOverride` = true. Each spot fades in/holds/fades out (equal thirds), then respawns at a new random position and color; initial phases are randomly staggered so spots don't pulse in unison. `spotSpeed` (inverted slow↔fast) selects a per-spot lifetime range: each spot rolls a random lifetime in `spotLifeBounds(spotSpeed)`, whose `lo`/`hi` interpolate independently between a fire end and a stars end (bounds in `spotty_expression.cpp`). The wide, low fire band makes the fast end read like fire — mostly rapid pops with occasional lingers; the narrow, high stars band is slow and gentle. The spot's pixel width is the size value directly; `edgeTaper(k, size, size/2, Linear)` handles even and odd widths symmetrically (a symmetric taper at both edges, single-pixel-accurate for even and odd sizes). |

### Shifty `fillMode`

The Zone is toggle-driven (`fullStrip=1` whole strip, `fullStrip=0` region), independent of `fillMode`. `fillMode` controls the per-pixel wavefront order within the zone during both fade-to-color and fade-back transitions. Pixels outside the Zone are unaffected in every mode, Uniform included (its fade/hold loops iterate `zone_.posMin..zone_.posMax`).

| Value | Behavior |
|---|---|
| 0 | Uniform — all zone pixels fade simultaneously (default) |
| 1 | Up — first pixel in zone leads, sweeps toward last |
| 2 | Down — last pixel leads, sweeps toward first |
| 3 | Bloom — center pixels lead on fade-in; outside-in on fade-back |

A directional mode staggers each pixel's fade start by up to `fadeDuration/2`, so the transition holds `FADING_*` for `fadeDuration + maxOffset` (up to `1.5× fadeDuration`) to let the last-staged edge pixel reach the target before the state flips. Uniform has no offset and flips at exactly `fadeDuration`.

## Trigger cadence

`Expression::control()` checks `timeReached(millis(), nextTriggerMs)` (wrap-safe) and fires `trigger()` if so. After every fire the schedule is reset with `nextTriggerMs = millis() + rng.range(intervalMinMs, intervalMaxMs)` (`rng` is the per-instance `FastRng`). Subclasses that override `control()` (breathing, spotty) are continuous and don't gate on `nextTriggerMs`.

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

Cascade is a dev/testing tool, not a user-facing feature: the app exposes the toggle only in dev mode, on purpose — a fleet of user lamps flashing in sync is visual noise, and only a few specifically controlled lamps ship with it. When `parameters["cascadeEnabled"] == 1`, a local trigger fans out a matching `MSG_EVENT` to every reachable peer, staggered by `parameters["cascadeStaggerMs"]`. Continuous descriptors never cascade — a cascaded long-running expression would override other lamps' behavior, and they retrigger at boot, settings upsert, and wisp release anyway — so `maybeCascade` gates them out, matching the app's hidden toggle. The structural loop break is in `triggerInvocation`, remote-arrived triggers are dispatched to a transient one-shot Expression instance and **never cascade**. A transient whose `trigger()` is rejected (wisp hold on a gate=true type) is not retained: it neither occupies the compositor nor blocks the same-sender coalesce check. See `docs/dev/networking.md` (MSG_EVENT section) for the wire format, the gossip-relay rule, and the per-msgType DedupRing.

## Expression mirror

The social echo the fleet ships (rather than cascade, which is dev-only). Every local fire of a triggered (non-`continuous`) expression announces a `MSG_EVENT` (see `ExpressionManager::emitEvent`; continuous types are gated out for the same reason cascade gates them). On receipt, `SocialEchoObserver` (registered on `ExpressionObserverRegistry`) rolls a disposition + social-mode weighted chance to replay that exact expression a short delay later:

- `rate% = kMirrorBasePct[disposition] * kMirrorModeFactorX10[mode] / 10`, clamped 0..100. Only Fond (4) / Smitten (5) ever mirror; Salty/Wary/Neutral and unknown peers (disposition < 4) are 0. The warmer the disposition and the more extroverted the mode, the higher the chance (grid in `behaviors/social_echo.hpp`).
- Introvert-only cooldown (`kIntrovertMirrorCooldownMs`) between mirrors so an introvert doesn't chatter; Ambivert/Extrovert have none.
- The replay is scheduled into a small pending buffer and fired from `tick()` via `triggerInvocation`, which suppresses cascade — so two warm lamps never echo each other into a loop.
- Skipped entirely during OTA and while a local Test/preview is active.

All feel numbers (`kMirrorBasePct`, `kMirrorModeFactorX10`, cooldown, delay floor + jitter) live in `behaviors/social_echo.hpp` and are bench-tunable.

## Testing

Host-side tests are per-folder PlatformIO suites under `software/lamp-os/test/` (each `test_<name>/` dir is its own suite; there is no single `test/native/` path). Run them with `npm run lamp:test`. Existing coverage:

- `test/test_personality_engine/personality_engine.cpp`, auto-trigger cadence, enable/disable
- `test/test_transient_override/transient_override.cpp`, the ColorOverride state machine the wisp gate consults
- `test/test_cascade_dedup/cascade_dedup.cpp`, `RecentCascade` ring keying and eviction
- `test/test_expression_primitives/expression_primitives.cpp`, Zone/Points/Size primitive helpers, `timeReached` wrap safety, `rewindBeforeExhaust`
- `test/test_builtin_descriptors/builtin_descriptors.cpp`, the exprcat wire JSON pinned against the production header-defined descriptor data
- `test/test_glitchy_timing/glitchy_timing.cpp`, glitchy's millis-driven duration gate (≥1 painted frame, wrap-safe deadline)
- `test/test_glitchy_coverage/glitchy_coverage.cpp`, `glitchBlockPlan` scatter→grain-block math (solid sentinel, grain-1 at max, exact per-level density, monotonic sparsity, empty/undersized region)
- `test/test_transient_lifetime/transient_lifetime.cpp`, transient GC backstop + never-started-transient rejection
- `test/test_social_echo/social_echo.cpp`, expression-mirror rate grid, disp<4 early-out, introvert cooldown, replay scheduling window + fire, emitEvent continuous-gate

To add a host-side test for a new expression: instantiate it with a stub `FrameBuffer` (see the personality-engine suite for the pattern), call the public `trigger()` to fire `onTrigger()`, then call `draw()` per frame and assert on the buffer's pixel state. You don't need a real Compositor — `Expression::control()` (the auto-cadence driver) is the only thing that requires one.

## Gotchas

- **Reserved-keys mismatch.** Adding a top-level field without updating both `config_codec.cpp` and `sections.dart`'s `_reservedKeys` will leak the field into the `parameters` map. Round-trips look fine but the field gets silently demoted on the next read. Per-type params never need this — only genuinely new top-level fields do.
- **`target` is a bitmask, not an enum.** 1=shade, 2=base, 3=both. Mixing up the bits compiles fine and produces "expression only paints half the lamp" symptoms.
- **The visible output is not your buffer.** Expressions paint into the configurator's frame buffer. If a `ColorOverride` is `Holding`, the strip shows the override fade, not your writes. Test in isolation (with the wisp off or `disabledDuringWispOverride` true) OR clear overrides manually before debugging.
- **No allocation in `onUpdate()`.** It runs once per flush window (~16 ms) for the whole time an instance is PLAYING. Allocate in `onTrigger()`, reuse buffers across frames. The existing expressions follow this pattern; copy them.
- **Continuous expressions own the loop.** Subclasses that override `control()` (breathing, spotty) don't get the base class's wisp-override gate for free. Route the override through the protected `Expression::continuousControl()` helper: it applies the wisp gate every tick, auto-retriggers when `STOPPED` (never for transients, which `gcTransients()` must reap), and returns true while suppressed so the caller can react. On that path a steady-state instance freezes its wall-clock state for a clean resume (breathing resets `lastBreathUpdateMs`), but a **transient must keep its completion progress advancing** while the paint is suppressed — breathing keeps calling `updateBreathPhase()`, spotty keeps aging its spots — or it never reaches `STOPPED` and squats on the compositor until the transient GC backstop.

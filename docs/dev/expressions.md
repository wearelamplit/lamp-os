# Expressions

This doc is for developers modifying the lamp's expressions subsystem or adding a new expression type.

## Subsystem map

Expressions are firmware-side animations the lamp auto-triggers on its own cadence, independent of the app being connected. The list is persisted in NVS as part of `Config::expressions` and round-trips through the app: the app reads the `expr` section via the page protocol (`CHAR_PAGE_CTRL`/`CHAR_PAGE_DATA`) and writes the full settings JSON via `CHAR_SETTINGS_BLOB`.

Files:

| File | Purpose |
|---|---|
| `software/lamp-os/src/expressions/expression.hpp` | Base class + lifecycle contract |
| `software/lamp-os/src/expressions/expression.cpp` | `control()` driver, wisp-override dim (`wispDimScale`) |
| `software/lamp-os/src/expressions/expression_manager.{hpp,cpp}` | Owns the entry list + one-shot transients, `makeExpression()` (registry-driven), cascade dedup, mesh recv path |
| `software/lamp-os/src/expressions/{glitchy,pulse,breathing,shifty,spotty,shimmer}/*_expression.{hpp,cpp}` | The shipped subclasses; each declares its own `ExpressionDescriptor` |
| `software/lamp-os/src/expressions/primitives.hpp` | Shared Zone/Points/Size clamped helpers + `resolveZone` (whole-strip/region toggle → Zone, shared by every zonable expression) + `pulseWidthFromPercent` (pulse `size` percent → capped fade radius) + `glitchBlockPlan` (glitchy scatter level→distinct grain blocks) + `usableSections` (breathing bands that fit the zone at ≥ `kMinSectionPx`) + `edgeTaper` (edge-taper weight: flat interior, curve-parameterized taper near the ends; `TaperCurve::Linear` or `Quadratic`) + `randomPermutation` (Fisher–Yates fill of a 0..n-1 order array over an injected rng; breathing's random band order) |
| `software/lamp-os/src/expressions/param_utils.hpp` | `getParam` — one-arg (descriptor keys, always present after `applyDefaults`) and two-arg (explicit fallback) lookups |
| `software/lamp-os/src/expressions/expression_schema.hpp` | `ExpressionDescriptor` / `ParamSpec` / `Bound` / `ColorSpec` / `RangeSpec` — the per-type schema each subclass declares |
| `software/lamp-os/src/expressions/expression_registry.{hpp,cpp}` | `ExpressionRegistry`: `add`/`remove`/`find`, `applyDefaults()`, `serializeCatalog()` (the `exprcat` wire JSON) |
| `software/lamp-os/src/core/lamp_behaviors.cpp` | `Lamp::registerExpressions()` — the default set (`reg.add(T::classDescriptor())`); a variant overrides it |
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

The editor shell (`expression_editor_screen.dart`) owns only colors + target; `expression_presentation.dart` supplies the client-side icon/tagline per type (keyed by catalog id — the only per-type thing still hand-authored, and it's presentation, not schema). Wisp-dim (`wispDimFloor()` below `1.0`) is a pure firmware type-property with no catalog field or app affordance; a wisp-yielding expression's editor stays fully usable while a wisp holds the surface.

### `exprcat` wire format

`{ "schemaVersion": 1, "expressions": [ <descriptor>… ] }`. Each descriptor (emitted by `ExpressionRegistry::serializeCatalog`, parsed by `expression_catalog.dart`):

- `id`, `name`, `continuous` (bool); `colors: {max, label?, help?, inheritsSurface?}`.
- `advanced` (bool, optional, default false) — expression is offered in the app only when advanced mode is on. Absent = standard. Additive; no `schemaVersion` bump.
- `interval?` / `duration?`: `{min, max, step, unit, default: [lo, hi], minGap?, label?, minKey?, maxKey?}`. `minKey`/`maxKey` name the instance keys the range writes; only `duration` carries them (into the **params map**). `interval` has none — the client writes it to the instance's **top-level** `intervalMin`/`intervalMax`. `minGap` (present only when non-zero) is the minimum spread the pair must keep between lo and hi, same unit as `min`/`max`. Firmware clamps it authoritatively in `makeExpression` (a client that ignores `minGap` can't commit a too-narrow range); the app/web dual-thumb controls push the opposing thumb so the pair never closes within `minGap`.
- `zone?`: `{}` (present) or `{optional: true}` (whole-strip/region toggle). Absent = no zone.
- `excludeTargets?`: `[surface…]`; `defaultTarget?`: surface. Surface strings only, see **Target vocabulary**.
- `params`: `[ {key, type("int"|"enum"), label, min, max, step, default, unit?, invert?, leftLabel?, rightLabel?, help?, requiresZoning?, options?: [{value, label, zoning?, group?}]} ]`. `group` (Type family, e.g. `"Overshoot"`) is present on `easing`'s options; see **Motion** below.

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

- `void onTrigger()`, **required**. Called once when `trigger()` fires (auto-interval, chain, or manual test) — `trigger()` returns false and skips it when the buffer routing rejects the start (wrong surface, or the operator's color editor is open). Read `colors[]`, `target`, the per-instance parameter map, and set up subclass state (`frames`, `frame`, allocations). Don't set `animationState` here — `trigger()` calls `playOnce()` right after `onTrigger()` returns, overwriting anything you set.
- `void draw()`, the compositor's render hook (from `AnimatedBehavior`). Every shipped expression overrides `draw()` — it's where pixels actually get written, gated on `shouldAffectBuffer()`. The compositor calls it once per flush window (~16 ms) while the behavior isn't `STOPPED`; `STOPPED` behaviors are never drawn.
- `void onUpdate()`, optional. Called by `control()` once per flush window while `animationState == PLAYING || PLAYING_ONCE`. Advance animation state here. Set `animationState = STOPPED` when done.
- `void onComplete()`, optional. Called the tick after `animationState` transitions back to `STOPPED`. Use it to restore any state you snapshotted in `onTrigger` (most expressions hand the buffer back to the configurator's render and don't need this).
- `void control()`, overridable but not required. The base class implementation handles the auto-trigger cadence, `onUpdate` dispatch, and `onComplete` dispatch. Subclasses that override `control()` take on the responsibility of those behaviours themselves; the continuous ones (breathing, spotty, shimmer) route through the protected `Expression::continuousControl()` helper for the auto-retrigger. Pulse overrides `control()` too but is dual-mode: in continuous mode it routes through `continuousControl()`, in trigger mode it falls back to the base auto-interval cadence.

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

1. **New subclass** in `software/lamp-os/src/expressions/foo/foo_expression.{hpp,cpp}` (each expression gets its own directory). Derive from `Expression`, override `draw()` (where you paint) and `onTrigger()`; add `onUpdate()`/`onComplete()`/`control()` as the effect needs. Implement `configureFromParameters(const std::map<std::string, uint32_t>&)` to read your params. Read them with the **one-arg** `getParam(parameters, "key")` — `applyDefaults` has already folded every descriptor key into the map, so a miss is a schema bug, not a missing preset. Look at `glitchy/glitchy_expression.cpp` for an interval-triggered brief-flash pattern, `breathing/breathing_expression.cpp` for a continuous always-running pattern, `spotty/spotty_expression.cpp` for a continuous effect with independent per-point lifecycles that dims under wisp override.
2. **Descriptor** — declare the descriptor data as `inline constexpr` in `foo_expression.hpp` (see the shipped types: `kFooDescriptorData`, make-less) with id, name, `colors`, optional `interval`/`duration`, zone flags, and `params`; in the `.cpp`, compose the registered descriptor with `withMake(kFooDescriptorData, &makeExpr<FooExpression>)` and expose it via a `static const ExpressionDescriptor& classDescriptor()` accessor. The split is a native-test seam: `test_builtin_descriptors` registers the header data directly (the `.make` factory can't link without Arduino), so a descriptor change fails the pinned catalog instead of drifting. Every wire field (and the whole editor) derives from this; there is no separate app-side schema. Return `continuous` here rather than the app — and override `wispDimFloor()` on the class (a value below `1.0`) if the type should dim, not pause, under a wisp hold.
3. **Register it** — add `reg.add(FooExpression::classDescriptor())` to `Lamp::registerExpressions` in `lamp_behaviors.cpp`. `ExpressionManager::makeExpression` then finds the descriptor by id, builds the instance via `.make`, and folds defaults before `configureFromParameters`. No factory dispatch to edit.
4. **App presentation** (optional) — add an entry to `expression_presentation.dart` keyed by your `id` for the picker icon + tagline. Skip it and the type falls back to a generic icon and no tagline; every control still renders from the descriptor.

Type-specific params ride the generic `parameters` map automatically — you do **not** touch the reserved-key skip lists (those cover only the fixed top-level fields, see **Parameter contract**). That's it: no protocol bump, no NVS migration. The settings_blob path picks up the new fields on first save, and the app picks up the new controls the next time it reads `exprcat`.

### Custom-lamp override

A variant owns its own set. Put the subclass + descriptor under `software/lamp-os/src/lamps/<variant>/` and override `registerExpressions` in the variant's `Lamp` subclass: call `reg.add(MyExpression::classDescriptor())` for its own types and `reg.remove("glitchy")` (etc.) to drop a stock one. The framework `registerExpressions` in `lamp_behaviors.cpp` is the default the variant replaces or extends; framework code never includes variant headers (see the variant-include hygiene rule in `CLAUDE.md`).

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
| Glitchy | ✓ | — | — | `scatter` (always active) sets grain and density. Its lowest level is a solid static fill of the active region held for the duration; higher levels scatter into progressively finer, sparser flecks (per-level density/grain in `kGlitchScatter`, `primitives.hpp`). Blocks occupy **distinct** grain slots (`slotCount = region / grain`, `blocksWanted = round(density% of slots)`), so realized density is exact rather than collision-capped. `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. Each active frame repaints from the saved background, so scattered levels re-roll into a stable dancing density; the solid level reads as a steady fill. `durationMin`/`durationMax` are milliseconds of wall clock; every glitch paints ≥1 frame. The grain-block plan derives from `glitchBlockPlan()` in `primitives.hpp`. The interval spans 10 min (`600 s`) to 5 h (`18000 s`) with a 30 min (`1800 s`) `minGap` between `intervalMin` and `intervalMax` |
| Breathing | ✓ | — | — | `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. The whole zone breathes together. `breathSpeed` runs between a fast floor (faster reads as hectic) and a slow ceiling. A multi-color palette advances to the next random color at the bottom of each breath (the dark trough), so the swap is unseen. Steady-state breathing never restarts; phase accrues from `millis()` deltas indefinitely. The zone's outer edges are soft: per-pixel intensity is scaled by an `edgeTaper()` run over a virtual region a couple pixels wider with the offset shifted in one, putting the darkest step off-screen so the outermost real pixel reads the brighter second step (both ends shift symmetrically; interior stays full). Timing is driven by `breathPhase`; the taper only weights the spatial per-pixel intensity |
| Shifty | ✓ | — | — | See `fillMode` below. `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. Fades (`fadeDuration`) and the hold (`shiftDurationMin/Max`) are pure `millis()` deadlines; the frame counter cannot end a fade or a hold. Marked `continuous` but each shift cycle ends (fade in, hold, fade back), then re-triggers after a random gap in `intervalMin`/`intervalMax` (top-level, the base-class trigger schedule) so the drift is unpredictable in timing |
| Shimmer | ✓ | — | — | Continuous shimmer (persisted id `flicker`); `wispDimFloor` = 0.3 (dims under wisp). No palette: shimmer modulates the lamp's own underlying colour, so a red lamp sweeps maroon→red→orange→yellow, a teal lamp cyan-hot to indigo-cold, all from heat (`.colors.max = 0`, no picker in the app). `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. The `fire` enum (0–3: Twinkle / Coals / Candle / Campfire) selects the `FireStyle`: rest level, heat targets, smooth wind gusts, a fast `flutterAmp` brightness flutter, and the two warmth knobs `warmthSwing` (hue slide toward yellow at peak / maroon at floor; 0 = pure brightness) and `whiteHot` (W engagement at the hot tip) (`shimmer_math.hpp`). `warmthModulate` derives the per-pixel colour from the anchor in channel space (no HSV round-trip): heat at the style's `restLevel` leaves the anchor unchanged, above it brightens + slides green toward red, below it darkens + drops green/blue faster than red; a black anchor stays black. Per-cell heat approaches its rolled target on `millis()` deltas with a wind offset, so the effect runs indefinitely off wall clock, not the frame counter. Candle/Campfire add a per-frame global brightness random-walk in `[-flutterAmp,+flutterAmp]` (`advanceFlutter`) for a rapid turbulent flutter on top of the slow sway; Twinkle/Coals set `flutterAmp` 0 (calm) |
| Spotty | ✓ | ✓ | ✓ (Small↔Large slider) | `fullStrip=1` (default) spans the whole strip; `fullStrip=0` scopes to the Zone. Continuous wandering ambient points; `wispDimFloor` = 0.3 (dims under wisp). Each spot fades in/holds/fades out (equal thirds), then respawns at a new random position and color; initial phases are randomly staggered so spots don't pulse in unison. `spotSpeed` (inverted slow↔fast) selects a per-spot lifetime range: each spot rolls a random lifetime in `spotLifeBounds(spotSpeed)`, whose `lo`/`hi` interpolate independently between a fire end and a stars end (bounds in `spotty_expression.cpp`). The wide, low fire band makes the fast end read like fire — mostly rapid pops with occasional lingers; the narrow, high stars band is slow and gentle. The spot's pixel width is the size value directly; `edgeTaper(k, size, size/2, Linear)` handles even and odd widths symmetrically (a symmetric taper at both edges, single-pixel-accurate for even and odd sizes). |

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

`Expression::control()` checks `timeReached(millis(), nextTriggerMs)` (wrap-safe) and fires `trigger()` if so. After every fire the schedule is reset with `nextTriggerMs = millis() + rng.range(intervalMinMs, intervalMaxMs)` (`rng` is the per-instance `FastRng`). Subclasses that override `control()` (breathing, spotty, shimmer) are continuous and don't gate on `nextTriggerMs`. Pulse overrides `control()` as well: in continuous mode it skips `nextTriggerMs` and auto-retriggers immediately, in trigger mode it keeps this interval schedule.

`enabled = false` clears `autoTriggerEnabled` at load time, which suppresses the auto-trigger in `control()`. Manual `trigger()` from the test path still works.

## Opacity (`opacity` param)

Every expression carries a shared `opacity` int param (10–100%, default 100, step 5) that caps how strong the effect gets at its peak. `Expression::configureOpacity()` clamps it into `opacityPct_`; `wispDimScale()` (below) folds `opacityPct_/100.0f` in as the `userOpacity` term, so opacity composes with wisp-dim rather than adding a second multiply. At `opacity=100` with no wisp present, `wispDimScale()` returns `1.0f` and the draw is byte-identical to a config that omits the param — the identity invariant every expression relies on.

The Flutter app groups the opacity slider with the Motion picker in a single "Motion & Appearance" editor card (`expression_params_panel.dart`): the picker shows only for descriptors carrying an `easing` param, opacity shows for every descriptor. The web editor auto-renders opacity as a generic slider and does not carry the motion picker.

## Motion (`easing` param)

`easing` stays a single flat integer on the wire (`software/lamp-os/src/util/easing.hpp` `enum class Easing`), append-only so no migration is ever needed. Values `0..16` are concrete curves; `17` is Random, which has no curve of its own — `Expression::configureEasing()` resolves it to a concrete value immediately, and `trigger()` re-rolls it on every fire, so each cycle gets a fresh motion.

| Family | In | Out | In-out |
|---|---|---|---|
| Linear | — | — | `0` |
| Smooth | `4` (Swell) | `3` (Settle) | `1` (Smooth) |
| Snap | `5` | `6` | `7` |
| Float | — | — | `2` |
| Overshoot | `8` | `9` | `10` |
| Spring | `11` | `12` | `13` |
| Bounce | `14` | `15` | `16` |
| Random | — | — | `17` |

Family × Direction is a **presentation-only** regrouping via each catalog option's `group` field (`kEasingOptions` in `expression_schema.hpp`): the pre-existing values `1`/`3`/`4` (Smooth/Settle/Swell) keep their exact wire meaning, just relabeled as the In/Out/In-out cells of the "Smooth" family. A client that ignores `group` still gets a flat, working option list. `kEasingOptions`' array order is the family display order (calm-first: Linear, Smooth, Snap, Float, Overshoot, Spring, Bounce, Random) and drives the app's Motion grid.

The Flutter Motion picker (`motion_picker.dart`) presents this as two selectors over the same flat `easing` value: a Direction row (In/Out/In-out, built from the union of direction labels across multi-direction families) above a fixed 4-wide Motion grid of one tile per family (8 families, 2 rows), sparkline redrawing live as Direction changes. `easing_curves.dart`'s `groupEasing`/`resolveEasing`/`describeEasing` do the decompose/compose; a singleton family (Linear, Float, Random) has one direction, so its sparkline doesn't change across Direction and Direction has no effect on its resolved value. A legacy catalog with no `group` degrades to one family per option and the Direction row hides.

Snap/Overshoot/Spring/Bounce are built from a shared `detail::outExpo`/`outBack`/`outElastic`/`outBounce` out-curve plus `easeIn`/`easeInOut` direction transforms (`easing.hpp`). Overshoot, Spring, and Bounce intentionally leave `applyEasing`'s output outside `[0,1]` mid-travel (the "past the mark, ease back" and "hop" shapes require it) — every call site that casts the eased value into a bounded integer clamps first: `spotBlendPercent` clamps to `[0,100]` (`spotty_math.hpp`), breathing's intensity clamps via `clamp01()` before its `uint32_t` cast (`breathing_expression.cpp`), and `easeStep()` clamps its result to `[0,dur]` for any duration-scaling caller. The app's `easing_curves.dart` is a lockstep Dart port of the same curve math, used only for the Motion picker's sparkline preview.

## Wisp-override dim (`wispDimFloor`)

An expression that would fight the wisp's hold colour dims instead of pausing. It keeps running continuously; its per-pixel contribution is scaled by `opacityTarget(true, opacityPct_/100.0f, wispDimFloor(), w) == clamp01(opacity * (1 + (wispDimFloor()-1)*w))`, where `w` is the compositor's eased wisp presence for that surface (`Compositor::wispPresence(base)`). `Expression::wispDimScale()` computes that scale; each expression multiplies its output by the returned scale where it writes into the buffer (`mixColorWeight(fb->buffer[i], painted, scale)`).

The same `w` drives the wisp composite in `Compositor::compositeWisp()`, which ticks it before expressions draw. So at full presence (`w=1`) the expression paints at its floor, and as the wisp eases home on release (`w→0`) the scale returns to `1.0` in lockstep with the wisp fade — the expression reappears as one smooth reveal rather than on a separate crossfade whose rate the fading wisp would expose.

Semantics:

- `wispDimFloor()` is a pure type-property, `1.0` by default (ignores the wisp, always full). `breathing`, `shifty`, `spotty`, and `shimmer` return `0.3` (dim-and-blend under the wisp). `glitchy` and `pulse` keep the `1.0` default; brief flashes / waves read fine over a held wisp colour.
- Expressions never pause on the wisp. Auto-trigger, cadence, and every trigger path (editor test button, mesh cascade via `triggerInvocation`) run unchanged; only the drawn contribution dims. The one draw gate left in `shouldAffectBuffer()` is the operator's open color editor. At scale `1.0` (no wisp) the output is byte-identical to a no-override draw.
- The app never greys or disables a wisp-yielding type's editor; the catalog carries no flag for it. A type dims under a wisp hold and stays fully editable the whole time.
- **Not user-editable.** No editor control surfaces the floor — the per-type default is authoritative.

## Mesh cascade integration

Cascade is a dev/testing tool, not a user-facing feature: the app exposes the toggle only in dev mode, on purpose — a fleet of user lamps flashing in sync is visual noise, and only a few specifically controlled lamps ship with it. When `parameters["cascadeEnabled"] == 1`, a local trigger fans out a matching `MSG_EVENT` to every reachable peer, staggered by `parameters["cascadeStaggerMs"]`. Continuous descriptors never cascade — a cascaded long-running expression would override other lamps' behavior, and they retrigger at boot, settings upsert, and wisp release anyway — so `maybeCascade` gates them out, matching the app's hidden toggle. The structural loop break is in `triggerInvocation`, remote-arrived triggers are dispatched to a transient one-shot Expression instance and **never cascade**. A transient whose `trigger()` is rejected (wrong buffer, or the operator's color editor is open) is not retained: it neither occupies the compositor nor blocks the same-sender coalesce check. See `docs/dev/networking.md` (MSG_EVENT section) for the wire format, the gossip-relay rule, and the per-msgType DedupRing.

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
- `test/test_transient_override/transient_override.cpp`, the ColorOverride state machine that drives per-surface wisp presence
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
- **The visible output is not your buffer.** Expressions paint into the configurator's frame buffer, which the compositor then composites the wisp layer over. While a wisp holds a surface, a dimming expression (`wispDimFloor` < 1.0) contributes only at its floor, so the strip mostly shows the wisp colour, not your writes. Test with the wisp off, or clear overrides manually, before debugging.
- **No allocation in `onUpdate()`.** It runs once per flush window (~16 ms) for the whole time an instance is PLAYING. Allocate in `onTrigger()`, reuse buffers across frames. The existing expressions follow this pattern; copy them.
- **Continuous expressions own the loop.** Subclasses that override `control()` (breathing, spotty, shimmer, and pulse in continuous mode) route through the protected `Expression::continuousControl()` helper: it auto-retriggers when `STOPPED` (never for transients, which `gcTransients()` must reap). Spot/breath state advances on the wall clock every frame; a transient keeps its completion progress moving so it still reaches `STOPPED` instead of squatting on the compositor until the transient GC backstop. A transient preview loops `kPreviewCycles` cycles (`primitives.hpp`) before completing, via the shared `Expression::previewCycleComplete()` counter, so the operator sees it move.

## Appendix: new-expression skeleton

A minimal copy-paste type, matching the shipped pattern (registry-driven, no
factory dispatch). Follows the four steps in **Adding a new expression type**
above.

`foo/foo_expression.hpp`:

```cpp
#pragma once

#include "expressions/expression.hpp"
#include "expressions/expression_schema.hpp"

namespace lamp {

// Make-less descriptor data the .cpp composes via withMake(); native-test
// seam so test_builtin_descriptors pins the production catalog.
inline constexpr ParamSpec kFooParams[] = {
  { .key = "fooTempo", .kind = ParamKind::Int, .label = "Tempo",
    .min = 100, .max = 4000, .def = 1000, .unit = "ms" },
  kOpacityParam,
};
inline constexpr ExpressionDescriptor kFooDescriptorData{
  .id     = "foo",
  .name   = "Foo",
  .colors = { .max = 4, .label = "Colors" },
  .params = kFooParams,
};

class FooExpression : public Expression {
 public:
  FooExpression(FrameBuffer* inBuffer, uint32_t inFrames = 30)
      : Expression(inBuffer, inFrames) {}

  static const ExpressionDescriptor& classDescriptor();     // for reg.add()
  const ExpressionDescriptor& descriptor() const override;  // instance accessor

  void configureFromParameters(
      const std::map<std::string, uint32_t>& parameters) override;
  void draw() override;

 protected:
  void onTrigger() override;

 private:
  uint32_t tempoMs_ = 1000;
};

}  // namespace lamp
```

`foo/foo_expression.cpp`:

```cpp
#include "expressions/foo/foo_expression.hpp"

#include "expressions/param_utils.hpp"

namespace lamp {

namespace {
constexpr ExpressionDescriptor kFooDescriptor =
    withMake(kFooDescriptorData, &makeExpr<FooExpression>);
}  // namespace

const ExpressionDescriptor& FooExpression::classDescriptor() { return kFooDescriptor; }
const ExpressionDescriptor& FooExpression::descriptor() const { return kFooDescriptor; }

void FooExpression::configureFromParameters(
    const std::map<std::string, uint32_t>& parameters) {
  // applyDefaults has folded every descriptor key into the map, so the
  // one-arg getParam always finds it — a miss is a schema bug.
  tempoMs_ = getParam(parameters, "fooTempo");
  configureOpacity(parameters);
}

void FooExpression::onTrigger() {
  // Snapshot palette + per-trigger state (allocations go here, not draw()).
}

void FooExpression::draw() {
  if (!shouldAffectBuffer()) { nextFrame(); return; }
  for (int i = 0; i < fb->pixelCount; ++i) {
    fb->buffer[i] = getRandomColor();   // your effect
  }
  nextFrame();
}

}  // namespace lamp
```

Register it in `Lamp::registerExpressions` (`core/lamp_behaviors.cpp`), or a
variant's override, with `reg.add(FooExpression::classDescriptor())`. Add
`onUpdate()` / `onComplete()` / `control()` as the effect needs, override
`wispDimFloor()` (a value below `1.0`) if it should dim rather than pause under a
wisp hold, and — optionally — a picker icon/tagline in `expression_presentation.dart`
keyed by `id`. The app renders every control generically from the descriptor;
there is no per-type app schema to touch.

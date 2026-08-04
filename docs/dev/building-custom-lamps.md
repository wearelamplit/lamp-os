# Building a custom lamp

A custom lamp is a `Lamp` subclass compiled into its own per-variant binary. It
declares its hardware, masks the built-ins it wants to replace, and adds its own
behaviors. There are four levels of involvement, in increasing order:
**configure** (a `defaults()` blob), **subclass** (`Lamp` override hooks),
**call** (author verbs on `BehaviorContext`), **utils** (the color / gradient /
math helpers).

This page is the author narrative and the on-ramp. The lamp-to-lamp API you
react with (`BehaviorContext`, `PeerView`, arrivals) is its own reference,
[`lamp-social-api.md`](lamp-social-api.md); the runtime underneath (compositor,
dual-core split, power governor, boot invariants) is
[`lamp-framework.md`](lamp-framework.md). Three shipped variants are the worked
examples:

- **`lamps/snafu/`** — the **social** reference, and the default walk-through
  through this page. Custom visuals on three fanned dot-strips, an arrival
  greeting driven by `forEachArrival`, and a masked-off `SocialBehavior`. The
  common case for a custom lamp: its own look, its own greeting, no physical
  inputs.
- **`lamps/lioness/`** — the **multi-strip social ambient** reference (walked
  through below). Three fixed strips, peer-color mirroring across zones of a
  shared strip, and a two-object always-on-ambient + one-shot-greeting split.
- **`lamps/staff/`** — the **physical-input** reference. Declares
  `HwConfig.inputs` (a button + touch pads) and drives gestures off them (§6).
  In development — read it for the input pattern, not as a proven shipping lamp.

`lamps/standard/` is the minimal baseline: two strips, all features on, no
custom behaviors.

## The three-lifetime split

The one idea that organizes the whole API: author code splits by **when it
runs**, not by one god-object. A behavior holds a `BehaviorContext*`, never a
`Lamp*`, so the surface you reach depends on the lifetime you're in.

| Lifetime | Where | What lives here |
|---|---|---|
| **Implement** | `Lamp` subclass override hooks | `HwConfig` (ctor), `featuresEnabled()`, `registerExpressions()`, `defaults()`, `createBehaviors()` |
| **Attach** | inside `createBehaviors()`, once at boot | input-driver callbacks (`Button::setCallback`), `onArrival()`, greeting-state provider, `b.add(behavior)` |
| **Call** | per-tick, from a behavior's `control()`/`draw()` | the `BehaviorContext` verbs: `setSolidColor`, `setBrightness`, `dispositionOf`, `forEachArrival`, … |

Keep the split straight and the rest follows: you can't call `setBrightness`
from a constructor (no context yet), and you don't re-register an `onArrival`
callback every frame (it's attach-once).

## 1. Declare your hardware — `HwConfig`

Your subclass passes an `HwConfig` to the `Lamp` base constructor. It is a POD:
strips, inputs, and two power numbers (`core/hw_config.hpp`).

```cpp
SnafuLamp() : Lamp(HwConfig{
  .strips = {
    {.role=Surface::Shade, .pin=14, .byteOrder=ByteOrder::GRBW, .pixelCount=16, .name="Small Dots"},
    {.role=Surface::Shade, .pin=27, .byteOrder=ByteOrder::GRBW, .pixelCount=12, .name="Medium Dots"},
    {.role=Surface::Shade, .pin=26, .byteOrder=ByteOrder::GRBW, .pixelCount=9,  .name="Big Dots"},
    {.role=Surface::Base,  .pin=12, .byteOrder=ByteOrder::GRBW, .pixelCount=24, .name="Stem", .broadcast=1, .reversed=true},
  },
  .maxBrightness = 230,
  .supplyBudgetMa = 1400,
}) {}
```

**`StripSpec`** — one physical NeoPixel run. `role` is `Surface::Shade` or
`Surface::Base` (the two logical surfaces every lamp has); a role may fan
several strips (snafu's three dot rings all share `Shade`, its `Stem` is the
lone `Base`). `pixelCount=0` means "resolve from `Config` at runtime" — the core
roles do this; a fixed-geometry strip like snafu's dots states its own count.
Exactly one strip per role may set `broadcast=1`: that's the representative
segment whose color the lamp advertises to peers (snafu's `Stem`).
`reversed=true` when pixel 0 is the far end of the winding.

**`InputSpec`** — one button or capacitive pad. snafu declares none, and most
lamps don't: physical inputs are the staff specialization, so the `.inputs`
field and its driver are §6. `id` is a variant-chosen handle you look up later;
touch-tuning fields are ignored for buttons.

**`validateHwConfig(hw)`** is a fatal gate `Lamp::setup()` runs before it sizes
any buffer. It rejects: no strip for a role, a duplicate pin, `Σ pixelCount > 255`
per role, more than one `broadcast=1`, a zero supply budget, and — the part
that matters once you add inputs — **an input pin colliding with another input
pin OR any strip pin**. A shared line can't drive a NeoPixel and read a gesture
at once. A malformed config halts the lamp with a visible blink rather than
mis-initing silently.

**Variant-include hygiene (hard rule).** Framework code (`core/`, `components/`,
`behaviors/`, `expressions/`, …) must never `#include` a `lamps/*/` header. Pin
numbers, pixel counts, brightness caps — every variant-specific constant — ride
into the framework through this `HwConfig` POD and `Config::Defaults`, never a
cross-include. The framework receives values; it never reaches into a variant.

## 2. Subclass the lamp + register the variant

### The override hooks

```cpp
class SnafuLamp : public Lamp {
 protected:
  Features featuresEnabled() const override { return Features::All; }
  void registerExpressions(ExpressionRegistry& reg) override;
  Config::Defaults defaults() const override;
  void createBehaviors(BehaviorStackBuilder& b) override;
};
```

- **`createBehaviors(BehaviorStackBuilder&)`** (required) — assemble the
  behavior list and do all attach-once wiring. `b.add(behavior)` appends to the
  stack; `compositor.addBaseBehavior(behavior)` puts one at the base scene
  layer (snafu's dots). This is where input callbacks and `onArrival` get bound.
- **`featuresEnabled() → Features`** — a bitmask of built-ins the framework
  should construct (`core/lamp_features.hpp`). `Features::All` keeps everything;
  mask a bit out to replace that built-in with your own. snafu drops three:

  ```cpp
  return Features::All
    & ~Features::SocialBehavior      // replaced by snafu::Greeting
    & ~Features::DefaultExpressions  // snafu owns its own visuals
    & ~Features::WebApp;             // snafu configures over BLE
  ```

- **`registerExpressions(ExpressionRegistry&)`** — populate the expression
  catalog (§5). Call `Lamp::registerExpressions(reg)` first to keep the shared
  built-ins, or skip it to define a wholly custom set.
- **`defaults() → Config::Defaults`** — the **configure** level: first-boot
  name, colors, per-surface pixel counts, `colorsEditable` flags, multi-segment
  role seeds, and `setup=true` for a fixed install that ships curated colors and
  skips the random first-boot roll. Full field list in `config/config.hpp`; the
  color-slot / multi-segment mechanics are in
  [`lamp-framework.md`](lamp-framework.md#color-slots).

Hardware is *not* an override — it rides the constructor (§1).

**`colorsEditable`.** A per-surface flag for a lamp whose color is fixed by
design. When `false`, the app hides that surface's color picker. Set it in
`defaults()` (`shadeColorsEditable` / `baseColorsEditable`); it is firmware-owned,
read by the app from the lamp's JSON, and not updatable via a settings-blob
write. An absent field defaults to `true`.

### Register the variant

A variant is compile-time identity: exactly one `LAMP_BUILD_VARIANT_<TYPE>` is
defined per binary, and `build_src_filter` excludes the other variants' sources,
so a `standard` binary can only ever instantiate `StandardLamp`. Register a new
one by editing two files in lockstep, then adding a build env.

**1. `src/lamp_variants.hpp`** — four `#ifdef` sites keyed on the same flag:

```cpp
// conditional include
#ifdef LAMP_BUILD_VARIANT_<TYPE>
#include "lamps/<name>/<name>_lamp.hpp"
#endif

// extend the exactly-one invariant
#if (defined(LAMP_BUILD_VARIANT_STANDARD) + defined(LAMP_BUILD_VARIANT_SNAFU) \
     + defined(LAMP_BUILD_VARIANT_<TYPE>)) != 1
#error "Exactly one LAMP_BUILD_VARIANT_* must be defined (check platformio.ini env)"
#endif

// createCompiledLamp() arm
#elif defined(LAMP_BUILD_VARIANT_<TYPE>)
  return std::make_unique<<Name>Lamp>();

// compiledLampType() arm
#elif defined(LAMP_BUILD_VARIANT_<TYPE>)
  return "<name>";
```

**2. `platformio.ini`** — an `[env:upesy_wroom_<name>]` block that extends
`env_base_upesy`, defines `-D LAMP_BUILD_VARIANT_<TYPE>=1`, sets
`custom_lamp_variant = <name>` (read by `inject_firmware_channel.py` /
`sign_firmware.py` to build the `{type}-{channel}` OTA gate), and adds a
`build_src_filter` arm including `lamps/<name>/`. Without that env there is no
way to compile the variant.

`main.cpp` writes `compiledLampType()` to the NVS `lampType` key on every boot,
so the persisted value is a mirror of the compiled-in identity, never an input
to it. Boot resolution detail: [`lamp-framework.md`](lamp-framework.md#variant-resolution-at-boot).

## 3. Call the author verbs — `BehaviorContext`

Inside a behavior, `behaviorContext()` returns the `BehaviorContext*`. **Every
service pointer on it is nullable — null-check before you dereference.** The
full surface (roster views, social reads, greeting seam, mesh send) is the
[`lamp-social-api.md`](lamp-social-api.md) reference; this section covers the
paint / brightness verbs any behavior uses to drive its surfaces.

```cpp
void MyBehavior::control() {
  auto* ctx = behaviorContext();
  if (!ctx) return;
  ctx->setSolidColor(lamp::colorFromHue(hue_));   // non-allocating
  ctx->setBrightness(80);
}
```

**Color.** Two verbs, different heap cost:

- `setSolidColor(Color)` — a non-allocating in-place fill of both surface
  configurators. This is the hot path: reach for it on anything that repaints
  every frame, where an allocation would fragment the heap.
- `setGradient(const std::vector<Color>& stops)` — the vector fade path. Fine
  for a rare repaint, wrong for a per-frame one. Allocates.

Reach for `setSolidColor` on anything continuous; `setGradient` only when a
multi-stop gradient genuinely changes and rarely.

**Brightness.** Two axes, both composing (never a raw strip write — a raw write
would fight crowd-dim, the OTA pulse, and the wisp override):

- `setBrightness(uint8_t level)` — master brightness, through the user-brightness
  micro-fade. Stacks with crowd-dim and the OTA pulse.
- `setSurfaceBrightness(Surface, uint8_t level)` — a 0–100 percent per-surface
  trim riding *under* master. Scales one surface down without touching the
  other; `100` clears the trim.

**Social reads** (per-tick, by value, allocate nothing): `dispositionOf(lampId)`
(1–5, 3 = neutral), `greetingFor(lampId)` (the per-peer waveform),
`crowd()` (composition by disposition), `crowdWeight()` (smoothed scalar). Depth
in [`personality-signals.md`](personality-signals.md).

**Heap discipline.** The lamp heap is tight and fragmented
([`embedded-heap.md`](embedded-heap.md)). The `forEach*` visitors and the scalar
reads allocate nothing; `setSolidColor` is in-place; `setGradient` allocates.
The roster arrival scan (`bestUngreetedArrival`) is an in-place scan under the
roster mutex and allocates nothing. Prefer the non-allocating verb on any
per-frame path.

## 4. React to the social fabric

Noticing a peer and greeting it is the lamp-to-lamp API, its own reference:
[`lamp-social-api.md`](lamp-social-api.md). In short — `forEachArrival` (pull,
ack-coupled, one greet per call) drives a custom greeting like snafu's;
`onArrival` (push, attach-once, framework-deduped) drives a side reaction like
the staff's friend-bloom. The social reads (`dispositionOf` / `greetingFor` /
`crowd` / `crowdWeight`) are on the same `BehaviorContext`.

### React to who's around — `PeerView::variant`

Every `PeerView` handed to `forEachNearby` / `forEachArrival` / `onArrival`
carries `variant`, a `lamp_protocol::LampVariant` (`Unknown, Standard, Snafu,
Staff, Lioness, Loaf`). It rides the mesh as an additive HELLO TLV, so a
behavior can react to *what kind* of lamp a peer is. Legacy / BLE-only peers
read `Unknown`. `variantName(v)` gives the display string.

Loaf's base ring is the worked example: it spins slowly on its own and speeds
up while another loaf is nearby, easing back when it leaves.

```cpp
void LoafRingBehavior::control() {
  // ... rebuild the ring from live config ...
  if (companionRevMs_ != 0 && context_) {
    bool loafNear = false;
    context_->forEachNearby([&](const PeerView& p) {
      if (p.variant == lamp_protocol::LampVariant::Loaf) { loafNear = true; return true; }
      return false;
    });
    setRevolutionMs(loafNear ? companionRevMs_ : baseRevMs_);  // eased in draw()
  }
}
```

The trigger is presence, not a one-shot greeting: it re-evaluates every control
tick and self-corrects when the peer leaves the nearby set. Calling
`forEachNearby` per frame is fine — the framework caches the nearby snapshot for
a 1 s freshness window, so a per-tick call does not re-lock or re-sort the
roster; don't roll your own throttle. Ramp the *value* you change (here the
revolution period), not the phase, so the animation eases instead of snapping.

## 5. Custom internal expressions

`registerExpressions(ExpressionRegistry&)` populates the expression catalog, and
you have two moves. **Extend** the shared editable set (call
`Lamp::registerExpressions(reg)` first, then add yours), or **replace** it
wholesale (skip the base call and register your own). snafu replaces it: it
masks `Features::DefaultExpressions` in `featuresEnabled()`, then registers its
own four in the variant hook — **not** the shared built-ins (their 6-descriptor
catalog is pinned by a native test):

```cpp
void SnafuLamp::registerExpressions(ExpressionRegistry& reg) {
  reg.add(GlitchyExpression::classDescriptor());
  reg.add(PulseExpression::classDescriptor());
  reg.add(SpottyExpression::classDescriptor());
  reg.add(ShimmerExpression::classDescriptor());
}
```

These are shared expression types (`expressions/`); registering them here offers
them in the app's editable catalog for this variant.

**Internal descriptors.** An expression your variant *fires* but never wants in
that editable catalog is an **internal** descriptor. Set `internal = true` on
the `ExpressionDescriptor` (`expressions/expression_schema.hpp`); it stays
registry-backed so `triggerInvocation` can fire it by id, but
`ExpressionRegistry::serializeCatalog` skips it, so it never reaches the app.
`bloom` (`expressions/bloom/bloom_expression.hpp`) is the shipped example: a
one-shot white-ward luminance swell carrying `.internal = true`, fired
programmatically as a transient (staff fires it from a friend-arrival
side-reaction, §4). It reads the live buffer each frame and lerps every pixel
toward white by one eased scalar (integer per-channel, no per-pixel float), so
it composites *over* a greeting as a luminance add instead of repainting the
hue. The general expression-authoring contract is [`expressions.md`](expressions.md).

## 6. Input hardware (optional)

Most lamps have no physical inputs — skip this unless yours does.

`HwConfig.inputs` builds one `input::InputSource` per spec at boot
(`components/input/input_driver.cpp`). Each source ticks itself once per loop,
immediately before the compositor, on Core 1 — **never call `delay()`** in a
handler; it stalls the render and the mesh. You don't tick them; you look one up
by id and bind to its FSM. Two concrete FSMs sit behind a source, reached
without RTTI via `asButton()` / `asTouch()`
(`components/input/input_source.hpp`):

- **`Button`** (`components/input/button.hpp`) — edge events
  `ButtonEvent::{Click, DoubleClick, LongPressStart, LongPressStop}` through a
  callback, plus pollable `isHeld()` / `heldMs(now)` for ramps.
- **`Touch`** (`components/input/touch.hpp`) — a capacitive pad; events
  `TouchEvent::{Tap, HoldStart, Release}`, plus `isHeld()` / `heldMs(now)`.
  Touched means the raw read drops **below** the boot-calibrated baseline. The
  driver calibrates *after* RF is up, so the baseline captures a radio-live
  ambient; RF bursts couple noise into a pad, absorbed by hysteresis.

**The seam, minimally.** Declare one button in the `HwConfig` (§1) with a
variant-chosen id:

```cpp
enum InputId : uint8_t { kPower = 1 };
// … in the HwConfig .inputs list:
.inputs = { {.id=kPower, .type=InputType::Button, .pin=19} },
```

Then in `createBehaviors` (attach-once), look it up and bind a callback:

```cpp
if (auto* src = inputById(kPower)) {
  if (auto* btn = src->asButton()) {
    btn->setCallback([this](lamp::ButtonEvent e) {
      if (e == lamp::ButtonEvent::Click) toggle();
    });
  }
}
```

That's the whole general contract: declare a spec, resolve
`inputById(id)->asButton()` (or `->asTouch()`), then react to events or poll
`isHeld()`. `inputById` returns null for an undeclared id and `asButton()` /
`asTouch()` return null on a type mismatch — null-check both, since you're
crossing from the variant into framework-built objects.

**See also — a complex real-world example.** staff layers a whole gesture
vocabulary on this seam: a stoke-button brightness toggle, a long-press mood
scrub over the non-allocating solid fill, per-pad brightness ramps off `heldMs`,
and a two-step unlock combo gating the touch gestures
(`lamps/staff/staff_input_behavior.cpp`, `lamps/staff/unlock_combo.hpp`). Those
gestures are idiosyncratic and staff is still in development — read them for the
pattern, not as a proven shipping lamp.

## Worked example: Lioness

A locked, fixed-install variant (`lamps/lioness/`) — the multi-strip social
reference. Three strips: `Shade` (pin14, 36px), `Base "Main"` (pin4, 32px,
`broadcast=1`), `Base "Lions"` (pin5, 18px), the last split by
`evenZones(3, 18)` into three 6px lion zones.

Two behaviors, split by lifecycle. `LionsAmbientBehavior` is the
always-`PLAYING` base scene (`compositor.addBaseBehavior()` only), owning just the
Lions segment's pixel range (`fb->segments[k].offset`) — the base
`ConfiguratorBehavior` draws first and covers the whole buffer from
`defaults()`, so `Main` is left untouched underneath. It's also the sole
`Greetable` (needs `onColorInfo` to cache peers' base-color stops for the zone
gradients), and delegates `triggerGreeting()`/`greetingState()` to
`LionsGreetingBehavior`, which starts `STOPPED` (`b.add()` only) and draws its
pulse over whatever the ambient behavior wrote this frame.

Each tick, `LionsAmbientBehavior::control()` calls `LionsDirector::tick()` to
assign nearby peers to the three zones. The nearby lamps are an ordered list and
the three lions are a sliding window that walks it: each lion advances on its
own staggered deadline (`kStaggerPeriodMs`, phase-offset `kStaggerPhaseMs`
apart) to the next lamp not shown by the other two, so over time every nearby
lamp gets shown, the trio stays distinct, and no two lions switch on the same
tick (0 → all idle / mirror `Main`; 1 → all three show it; 2 → two lions show
the pair, the third holds idle; ≥3 → the walk). It pulls stops via
`MSG_COLOR_QUERY`/`onColorInfo` and renders with the pure `renderLions()`
(`lions_scene.cpp`), crossfading a switched zone over `kCrossfadeFrames` with
`util/fade`. `LionsGreetingBehavior::startGreeting()` snaps all three lions to
the newcomer's `peer.baseColor`, breathes `kPulses` times via the pure
`pulseEnvelope()` (`lions_greeting.hpp`), eases into the ambient pixels
underneath, and fires the shared `GlitchyExpression` on the shade
(`triggerInvocation(..., broadcast=false)` — already in the base catalog, no
internal descriptor needed). `LionsDirector` / `renderLions` / `pulseEnvelope`
are pure and frame-driven, each with its own native test.

One item wants a hardware pass: `maxBrightness=230` / `supplyBudgetMa=1400`
are copied from snafu's `HwConfig`, not measured against Lioness's 86-pixel
draw.

## 7. Build, test, and ship

**Build (dev).** A local build is always the **dev** channel: unsigned,
`LAMP_DEBUG` on, no signing key needed.

```sh
VARIANT=<name> npm run lamp:build
VARIANT=<name> PORT=/dev/cu.usbserial-XXX npm run lamp:flash
```

A dev binary boots and meshes normally, but its mesh role is **limited**: it is
an OTA island. Unsigned, it carries no LSIG footer and its distributor
self-disables at boot, so it never sources an OTA and never interoperates with
signed peers' firmware distribution — the `{type}-{channel}` gate isolates dev
from beta/stable in both directions. Fine for bench-testing your lamp's own
behavior; not a fleet-ready binary. The channel model (dev / beta / stable, and
what each implies) is in [`CLAUDE.md`](../../CLAUDE.md) under **Build + test**.

Watch a dev build over serial with `npm run lamp:tap` (reset-safe); the
bracket-tagged log catalog is [`debug-instruments.md`](debug-instruments.md).

**Ship (signed).** To get a build that works fully on the mesh, upstream your
variant: open a PR. Once merged, every push to `dev` builds a signed release,
and CI publishes the full `distribution.bin` (bootloader + partitions + firmware
+ SPIFFS) to GitHub Releases. Install it over USB with the download-and-flash
task, which pulls that signed image — no build, no key:

```sh
VARIANT=<name> npm run lamp:flash:release
```

Signing is CI's job; there is no local sign-and-flash task. The keys, channel
gating, and release flow are all detailed in [`CLAUDE.md`](../../CLAUDE.md). A
fully independent fork running its own separate fleet signs with its own ed25519
key (`scripts/gen_firmware_keys.py`), which cryptographically isolates its OTAs
from every other fleet.

## Stability matrix

Third-party variants may rely on these headers:

| Header | Guarantee |
|---|---|
| `core/lamp.hpp` | STABLE — the 3 virtuals (`defaults`, `featuresEnabled`, `createBehaviors`) are versioned API |
| `core/hw_config.hpp` | STABLE — POD, additive fields only |
| `core/lamp_features.hpp` | STABLE — flag values frozen |
| `core/behavior_stack_builder.hpp` | STABLE — use the const `behaviors()` accessor |
| `core/animated_behavior.hpp` | STABLE — base class for custom behaviors |
| `core/behavior_context.hpp` | STABLE-ADDITIVE — new nullable pointers may be added; existing ones won't be removed |
| `core/personality_engine.hpp` | STABLE — personality gate for expression suppression |
| `components/network/mesh/lamp_roster.hpp` | STABLE — read by custom behaviors |
| `config/config.hpp` | STABLE — read-only surface for custom behaviors |

Everything else is internal and may change without notice — the pending-slot and
override aggregates (`core/pending_slot_aggregate.hpp`,
`core/override_aggregate.hpp`), the built-in behaviors under `behaviors/`, and
the mesh dispatch (`components/network/mesh/mesh_link.hpp`).

## Testing a contributed variant

Native tests (`npm run lamp:test`) can't link Arduino, NimBLE, or
Adafruit_NeoPixel. Use the inline-mirror pattern: re-implement just the
predicate or emit logic in the test and assert on its output. Examples:
`test/test_hw_config_validate/` (the `validateHwConfig()` predicate),
`test/test_lamp_type_emit/` (the `lampType` JSON line), and
`test/test_section_emit_colors_editable/` (the `colorsEditable` field shape).

The minimum for a contributed variant is a boot test,
`test/test_<variant>_lamp_boot/<variant>_lamp_boot.cpp`, that constructs the
`HwConfig` POD inline (no Arduino) and calls `validateHwConfig()` to pin the pin
layout and surface roles against the physical hardware.

---

**Cross-references**

- [`lamp-social-api.md`](lamp-social-api.md) — the lamp-to-lamp API: the full
  `BehaviorContext` surface, arrivals, greeting state.
- [`lamp-framework.md`](lamp-framework.md) — the runtime internals underneath.
- [`social.md`](social.md) — how greetings and crowd-dim fit together.
- [`personality-signals.md`](personality-signals.md) — the social signals a
  custom lamp reacts to.
- [`expressions.md`](expressions.md) — writing a new expression type.
- [`utilities.md`](utilities.md) — the color / gradient / fade / RNG toolbox the
  verbs and recipes are built from.
- [`embedded-heap.md`](embedded-heap.md) — the heap discipline behind
  "prefer the non-allocating verb".
- [`debug-instruments.md`](debug-instruments.md) — the bracket-tagged serial log
  catalog for watching a dev build over `npm run lamp:tap`.

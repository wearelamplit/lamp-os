# Lamp framework internals

The runtime a custom lamp plugs into: the single-instance model, the dual-core
split, the compositor and brightness pipeline, and how a variant resolves at
boot. This is the FW-internals reference. If you're **authoring** a lamp, start
at [`building-custom-lamps.md`](building-custom-lamps.md) (the task guide) and
[`lamp-social-api.md`](lamp-social-api.md) (the lamp-to-lamp API); you rarely
need this page. The code wins ties; update this doc when it doesn't.

## Overview

`lamp::Lamp` is the base class for ESP32-WROOM smart-lamp implementations. It
wires the app-facing contract (BLE GATT characteristics for real-time settings,
the JSON section protocol for NVS personality storage), mesh discovery,
pending-slot multitasking, color/brightness overrides, and a pluggable behavior
stack. It does not include visual algorithms — those are an `AnimatedBehavior`
subclass's domain. The complete app/mesh wire format is
[`networking.md`](networking.md).

Variant identity is entirely compile-time: an env's `-D LAMP_BUILD_VARIANT_*`
flag both picks which variant's sources compile in (paired with
`build_src_filter`) and, via `#ifdef` in `lamp_variants.hpp`, selects what
`createCompiledLamp()` and `compiledLampType()` return. The binary IS a variant;
there is no runtime registry lookup. The register-a-variant recipe is in
[`building-custom-lamps.md`](building-custom-lamps.md#register-the-variant).

## Single-instance invariant

The framework assumes ONE `Lamp` subclass instance per firmware binary — the
compiled-in variant returned by `createCompiledLamp()`. This lets compositor,
config, frame buffers, NimBLE handles, and aggregate state (pendingSlots,
overrides) live at file scope without coordination overhead. Two `Lamp`
instances in one binary is unsupported.

Practical consequence: framework-level state in `core/lamp.cpp` is
file-scope-static, not `Lamp` private members. Single-instance IS the design.

## Color slots

Every lamp has two **core** slots, always present:

- **Shade** (primary surface): its `colors` feed the BLE advertisement and the
  shade field in HELLO. Configured via `Config::Defaults::shadeColor` and
  `shadePx`; app visibility controlled by `shadeColorsEditable`.
- **Base** (secondary surface): its `colors` drive peer greeting handshakes.
  Configured via `Config::Defaults::baseColor` and `basePx`; app visibility
  controlled by `baseColorsEditable`.

A slot with 2 or more colors renders a still gradient; with rotation on (below)
they flow round a closed loop. First-boot
randomization only touches surfaces whose variant left `baseColor` / `shadeColor`
empty in `Config::Defaults`; a non-empty default survives randomization
unchanged.

### Colour rotation

A single-segment slot can rotate: its colours, closed into a loop (last colour
blending back to the first), flow along the surface at one constant speed, one
loop every `periodS` seconds (5 to 120), toward `dir` (`Up` or `Down`, physically
up or down on that surface). The defaults are per role: off, 40 s, Down on the
shade and Up on the base, so on a standard lamp both flow toward the seam
(`ShadeSettings::kDefaultRotation`, `BaseSettings::kDefaultRotation`). A slot
with one colour, or with other than one segment, never rotates.

- **Who can rotate.** A variant opts in with `Config::Defaults::rotationSupported`
  (only `StandardLamp` sets it). The flag gates behaviour, never data: the
  config keeps a single-segment role's `rotate` through load, save and the web
  document whatever the flag says, and every scene writer reads the role's
  `renderedRotation()`, which is the default (off) where the role does not
  `offersRotation()`. The raw `rotation` field is read only by the codec, the
  colour-write apply layer and that accessor.
- **Which way is up.** The configurator is built with its `Surface` and maps
  `dir` onto buffer order with the layout in
  [`expressions.md`](expressions.md#frame-buffer--targets): index 0 at the shade's
  bottom and the base's top, so Up runs away from index 0 on the shade and
  toward it on the base.
- **Live and saved.** A colour write's object form sets rotation in RAM config
  and renders it at once (`buildRoleScene`, then `ConfiguratorBehavior::showScene`);
  `CHAR_COMMIT` persists it with the colours and re-seeds the saved scene
  (`FrameBuffer::defaultColors` plus `defaultRotation`, set together by
  `refreshDefaultScene` and at boot). A saved scene that rotates keeps rotating
  from boot; an abandoned preview crossfades back to it after
  `CONFIGURATOR_WEBSOCKET_TIMEOUT_MS`.
- **OTA quiet mode.** On quiet entry the compositor returns both configurators'
  rotation to position 0 (`resetRotation()`), the frame quiet mode settles each
  surface to, so a stretch that ends without a reboot resumes from it.
- **Raw content never rotates.** The zone marker, `setGradient` / `setSolidColor`
  and other `beginFade` writes draw unshifted; a zone preview released on a
  rotating role resumes rotating from where the marker stopped it, and an
  expression Test that completes restores the role scene, so it keeps rotating.

### Multi-segment roles

A role's colors can be a single scalar (`baseColor` / `shadeColor` + `basePx` /
`shadePx`) or split into named segments. A variant declares segments via
`Config::Defaults::baseSegments` / `shadeSegments` (a `std::vector<SegmentDefault>`
of `{name, px, "csv,of,hex,colors"}`); an empty vector takes the single-segment
scalar path. Snafu drives its three shade dot-strips and the `Stem` base entirely
through segments, one palette per segment, rendered by the segment-aware
`DotsBehavior`.

`HwConfig.strips` lists every physical NeoPixel strip; each entry carries a
`role` (`Surface::Shade` or `Surface::Base`), pin, byte order, pixel count, and
optional `name` / `broadcast` / `reversed` flags. Multiple strips can share a
role (as the snafu shade does), and one `broadcast=1` strip per role is the
representative for advertisement / HELLO.

## Variant resolution at boot

The compiled-in variant is authoritative. `compiledLampType()` returns the name
baked in by `-D LAMP_BUILD_VARIANT_*`, and `main.cpp` writes it to the NVS
`lampType` key on every boot, overwriting a stale value left by a previous
cross-variant reflash:

```cpp
const char* compiled = lamp::compiledLampType();
if (config.loadLampType() != compiled) config.setLampType(compiled);
g_lamp = lamp::createCompiledLamp();
```

NVS `lampType` is a persisted mirror, not an input to resolution: the firmware
never reads it to decide what to build, only writes it. Its purpose is to expose
the variant over the BLE `lampType` section field so the app can fetch the
matching per-variant OTA binary. There is no post-flash provisioning step — a
reflash to the other variant's env switches identity on the next boot. A
cross-variant OTA can't land the wrong image anyway: the `{type}-{channel}` LSIG
gate drops a mismatched binary before it flashes.

## Lifecycle: Core 0 vs Core 1

The ESP32 runs dual cores. Which runs where is critical for safe multitasking:

- **Core 0** (NimBLE + WiFi host tasks): all BLE GATT callbacks, ESP-NOW mesh
  receive handler.
- **Core 1** (Arduino loop task): main loop, Compositor ticks,
  `AnimatedBehavior::control()` / `draw()`.

### Pending slot hand-off

Async work arriving on Core 0 (e.g. a BLE write from the app) must not block or
allocate directly on the host task. Queue it with
`lamp::pendingSlots.X.post(callback)` for Core 1 to drain in `Lamp::tick()`.

Rules for custom behaviors:

- **Safe on Core 1:** all `control()` / `draw()` code, heap allocation, NVS
  reads/writes, Compositor state mutations.
- **Not on Core 1:** NimBLE APIs (owned by Core 0), mesh operations (Core 0 task).
- **Not on Core 0** (inside BLE callbacks or ESP-NOW handlers): heap allocation,
  blocking calls, direct Compositor/Lamp mutation — queue to pending slots
  instead.

### Home mode

"Home mode" is the lamp's idle/resting state, dimmed and quiet. The Compositor
tracks it (`Compositor::setHomeMode(bool)`) and gates each behavior via two
virtual queries on `AnimatedBehavior`: `isSocialBehavior()` (true on
`SocialBehavior`) and `homeModeExpressionId()` (the expression type id string on
`Expression` subclasses, `nullptr` otherwise). The suppression policy is pushed
by `reapplyHomeModeState()` via `Compositor::setHomeModePolicy()` and comes from
`HomeModeSettings::socialDisabled` and `HomeModeSettings::disabledExpressionTypes`.
The render rule is `homeModeTick` (`core/home_mode_gate.hpp`): Run, Skip (an
expression of a home-disabled type) or Cancel (a social behavior while social
is off and no app greeting plays); behaviors matching neither query run. The OTA offer scan (`tickOtaOffers`) runs in
`Lamp::tick` outside the compositor, so home mode never stops a lamp offering
updates.

While home mode is active with `socialDisabled` set, the lamp also ignores what
other lamps push onto it. One pure decision, `homeModeBlocksRemote(homeActive,
socialDisabled, source)` in `core/home_mode_gate.hpp`, read through
`Compositor::blocksRemote(source)`, gates every peer path. No lamp sends
another lamp settings over `MSG_CONTROL_OP`, which carries only wisp traffic
([`networking.md`](networking.md)), so that message needs no gate.

- `ExpressionManager::triggerInvocation` refuses a `TransientSource::Remote`
  invocation: a cascade (`MSG_COMMAND`, immediate or delayed) and an event
  mirror (`MSG_EVENT` through `SocialEchoObserver`). Nothing is booked for
  later either: while `ExpressionManager::ignoresPeers()` the command drain
  queues no delayed cascade and `SocialEchoObserver` books no mirror (its
  introvert cooldown untouched), so nothing fires once home mode or the
  preview ends. A retiring transient does not coalesce its sender's next
  invocation.
- `ExpressionManager::gcTransients` retires a Remote transient already running
  when home mode turns on: `Expression::retire()` ramps its contribution scale
  to 0 over `Expression::kRetireFadeMs` (1 s), then it is reaped. A transient of
  a home-disabled type, Local ones a peer caused included (the Lioness
  greeting's glitch), is reaped at once, since the compositor skips it and it
  would otherwise freeze and resume when home mode ends.
- A Local transient a peer caused just before home mode began (a staff bloom
  for a friend's arrival) is the lamp's own and plays out.
- A delayed cascade or echo mirror booked before home mode began is dropped
  when it begins, so nothing booked then fires after home mode ends.
- `BehaviorContext::forEachArrival` hands no arrival to a greeting (snafu's,
  the Lioness greeting), and `forEachNearby` visits no peer, so the room reads
  as empty: the Lions ambient scene shows no neighbour palettes and sends no
  colour queries, and the Loaf ring keeps its base speed. `crowd()` and
  `crowdWeight()` report an empty room too.
- `BehaviorContext::tickArrivals`, the framework arrival pass `Lamp::tick` runs
  each loop, acknowledges every roster arrival (the ack `SocialBehavior`'s
  own roster scan reads, on standard, loaf and staff) and marks the
  `onArrival` notifier's fired set without firing it (the staff bloom,
  `ArrivalPass::Silence`; the set has a slot for every roster peer, so any
  crowd is marked whole). Leaving home therefore greets no peer that was near
  all along, on every variant.
- Crowd-dim lifts: `CrowdDim` (`core/crowd_dim_fade.hpp`) aims the factor at
  1.0 and ramps there over 1 s (`kCrowdDimHomeFadeMs`) when the gate switches
  on or off, against the 80 ms an ordinary deadband commit takes; a commit
  while that ramp still runs keeps its remaining time.
  `Lamp::tick` reapplies brightness on every pass of a ramp and the one after
  it ends (`crowdDimFading()`), held off while the slider's own micro-fade runs
  and in OTA quiet mode; the landing pass waits out either hold.

The app's Home Mode page forces home mode on to preview it
(`Compositor::homePreview`). The preview blocks the same paths, but
`Compositor::discardsRemote(source)` is false during it: no arrival is
consumed and no running transient retired, and the arrival notifier holds
(`ArrivalPass::Hold`: departures re-arm, nothing is marked), so a peer that
leaves and returns during the preview fires afterwards as usual. Nothing is
booked for later either, so no peer effect lands once the app leaves the page.
What the preview stops while it shows stays stopped: a `SocialBehavior`
greeting it cuts off is cancelled and a snafu palette borrow ends, as at home.
The app's own greet button still plays during the preview: `SocialBehavior`
marks a `triggerGreeting` greeting (`playsAppGreeting()`) and
`homeModeSkipsSocial` lets the compositor run it until it ends. When it ends,
`SocialBehavior::control` picks and acknowledges no peer while `ignoresPeers()`,
and neither greeting takes peer colour info meanwhile, so the app's greeting in
the preview shows the peer's base colour without its palette, on standard and
snafu alike. Peer transients
already running are not faded during a preview.
`homeModeState` (`core/home_mode_logic.hpp`) decides home mode and the preview
from one read of the BLE client state: with the app connected, only its Home
Mode page turns home mode on, and that is the preview. `reapplyHomeModeState`
keeps that snapshot, and `calculateEffectiveHomeMode` (brightness, compositor
boot) returns it, so the brightness and the gate always agree.
`initBehaviors` runs `reapplyHomeModeState` right after `compositor.begin`, so
the policy comes from config from the first pass.

The lamp's own sources stay `TransientSource::Local` and always pass: the app's
Test button and its remote op over BLE (`CHAR_REMOTE_OP`, applied only on the
lamp it is written to). The wisp's paint, overrides and brightness are the
owner's controller, not a peer, and pass too. `disabledExpressionTypes` keeps
blocking its types from every source. A greeting already playing when home
mode turns on is not faded: the compositor calls `cancelGreeting()` on a
skipped `SocialBehavior` (STOPPED, frame 0, the app's greeting state told it
ended), so it does not resume when home mode ends, and snafu's and the Lioness
greetings finish their one pass. Snafu then borrows no peer palette: its dots
refuse a borrow while `ignoresPeers()` and drift an active one back to the own
palette from the scene on show: a re-scatter crossfade under way is resolved
into the starting scene, and a melt under way stays an overlay above the wisp
mix, as `draw()` paints it, easing out over the drift. Once the drift lands
the ambient crossfade returns to its own length (`kSceneFrames`); the greeting takes no
peer colour info meanwhile. A retiring glitch gives its
background back to the live scene as it fades (`Expression::retireScale`).

## Brightness path and the power governor

Every brightness writer funnels through `lamp::setAllStripsBrightness`:
`applyEffectiveBrightness()` (home-mode/user baseline → personality crowd-dim →
transient override → `calculateBrightnessLevel`), the slider micro-fade in
`Lamp::tick`, and the `apply::brightness*` helpers (BLE slider, mesh cascade,
settings_blob) all end there. The funnel latches the requested level and hands
`min(requested, governor ceiling)` to every strip driver, so the governor caps
every path with one `min()` and never touches the computed baseline.

The ceiling `calculateBrightnessLevel` scales against is not the raw variant
`HwConfig::maxBrightness` but `effectiveCeiling(hw_.maxBrightness,
config.lamp.brightnessCeiling)` — the variant cap narrowed by the user's Battery
Saver setting, floored at 1, cached in `s_hwMaxBrightness`.
`Lamp::recomputeEffectiveCeiling()` refreshes it at setup and on every
settings-blob write, so a new ceiling applies without a reboot.

The estimator (`core/power_governor.hpp`) prices the frame the drivers are about
to show. `fullDutyMa` gamma-sums each surface's FrameBuffer at a per-channel
full-duty draw (W counts only on 4-channel strips); `demandMa` scales that by
the NeoPixel `(level+1)/256` factor at the requested (pre-clamp) level and adds a
per-pixel idle draw. The compositor's `preFlushHook` (`governFrame`, `lamp.cpp`)
runs it once per drawn frame, after behaviors draw and before any pixel write, so
a clamp decision reaches the drivers ahead of that frame's `setPixelColor` loop
— `setBrightness` after `setPixelColor` destructively rescales the frame it was
meant to protect.

`PowerGovernor` compares demand against `HwConfig::supplyBudgetMa` minus a
reserve that widens while the radio is hot (OTA in either direction, quiet mode,
or a BLE client) or during the boot window and narrows when quiet. Any frame over
budget snaps the ceiling to the level that fits inside the same frame
(`senseFrame` returns true and `governFrame` re-mins the drivers before the
flush). While clamped, per-frame re-solves only move the ceiling down; recovery
goes through the release, paced inside `PowerGovernor::tick` (called from
`Lamp::tick`, which also advances the boot ramp), which returns the governor to
dormant once demand falls a set margin below budget — there the ceiling is 255
and the funnel `min()` is identity. The ceiling glides back up rather than
snapping, and every cold boot holds a reduced ceiling before ramping to full to
ride out supply inrush. Tuning constants live in `core/power_governor.cpp`; the
per-pixel draw constants in
`software/shared/led-common/src/lampos/led_power.hpp`. The
`qa/power-governor.md` runbook is the hardware pass.

## Build setup

Per-variant envs (`upesy_wroom_standard`, `upesy_wroom_snafu`,
`upesy_wroom_staff`) extend `env_base_upesy`. Each sets `-D LAMP_BUILD_VARIANT_<NAME>` and a
`build_src_filter` that compiles **only** that variant's `lamps/<variant>/`
sources; the other variant is never built. Each also declares
`custom_lamp_variant = <name>`, which `inject_firmware_channel.py` and
`sign_firmware.py` read (via `env.GetProjectOption()`) to build the
`{type}-{channel}` OTA gate string.

**SemVer source-of-truth.** The root `VERSION` file (`MAJOR.MINOR.PATCH`, one
line) is canonical for both lamp and wisp. `inject_version.py` (a PIO pre-build
hook wired in both components' `platformio.ini`) parses it and injects
`-D LAMP_FW_MAJOR/MINOR/PATCH`; `FIRMWARE_VERSION` / `FIRMWARE_VERSION_STR` are
derived from those macros. Bumping a release is a one-file edit (plus the
changelog, per [`CLAUDE.md`](../../CLAUDE.md)).

## File index

| File | Purpose |
|---|---|
| `software/lamp-os/src/core/lamp.hpp` | Base class: setup/tick entry points, hw config accessor |
| `software/lamp-os/src/core/lamp.cpp` | Implementation: BLE GATT wiring, mesh init, OTA health checks |
| `software/lamp-os/src/lamp_variants.hpp` | `createCompiledLamp()` + `compiledLampType()`, compile-time variant selection |
| `software/lamp-os/src/core/hw_config.hpp` | `HwConfig`, `StripSpec`, `Surface`, `ByteOrder` PODs + `validateHwConfig()` |
| `software/lamp-os/src/core/lamp_features.hpp` | `Features` bitmask enum for built-in behavior opt-in/out |
| `software/lamp-os/src/core/behavior_stack_builder.hpp` | `BehaviorStackBuilder` helper for registering behaviors |
| `software/lamp-os/src/core/behavior_context.hpp` | `BehaviorContext` struct + `PeerView`: service surface for behaviors |
| `software/lamp-os/src/core/animated_behavior.hpp` | `AnimatedBehavior` base class: control/draw interface |
| `software/lamp-os/src/core/frame_buffer.hpp/.cpp` | `FrameBuffer`: the per-surface pixel buffer `draw()` writes into |
| `software/lamp-os/src/core/pending_slot_aggregate.hpp/.cpp` | Core 0→1 hand-off mechanism for async work |
| `software/lamp-os/src/core/override_aggregate.hpp/.cpp` | Transient color/brightness overrides (colour watchdog 100 s, wisp keep-alive-held; brightness watchdog a separate 60 s) |
| `software/lamp-os/src/core/personality_engine.hpp/.cpp` | Personality gate for expression suppression + crowd-dim |
| `software/lamp-os/src/core/arrival_notifier.hpp/.cpp` | Push-notifies `onArrival` observers once per new near peer |
| `software/lamp-os/src/core/power_governor.hpp/.cpp` | Current estimator + supply-budget brightness governor |
| `software/lamp-os/src/core/compositor.hpp/.cpp` | `Compositor`: blends behavior layers, home-mode gate, dynamic add/remove |
| `software/lamp-os/src/lamps/standard/standard_lamp.hpp/.cpp` | Production fleet lamp (built-in social, expressions, idle) |
| `software/lamp-os/src/lamps/snafu/*` | Amanita mushroom lamp: social reference variant |
| `software/lamp-os/src/lamps/staff/*` | Physical reference variant (inputs, per-surface trim, bloom) |
| `software/lamp-os/src/main.cpp` | Unified entry point: mirrors compiled variant into NVS, instantiates it |
| `software/lamp-os/platformio.ini` | Per-variant envs extending `env_base_upesy`; SemVer build_flags |
| `scripts/sign_firmware.py` | Build-time firmware signer (CI) |
| `software/lamp-os/src/components/firmware/firmware_signature.cpp` | OTA-receive-time signature verifier |
| `software/lamp-os/src/components/firmware/firmware_pubkey.h` | Embedded public key (committed; fork-specific) |

## Cross-references

- [`building-custom-lamps.md`](building-custom-lamps.md) — authoring a variant.
- [`lamp-social-api.md`](lamp-social-api.md) — the `BehaviorContext` / peer API.
- [`expressions.md`](expressions.md) — the expression subsystem.
- [`networking.md`](networking.md) — the app + mesh wire format.
- [`../adrs/`](../adrs/README.md) — the *why* behind the mesh, OTA, and dual-core
  decisions.

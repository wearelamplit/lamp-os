# Lamp Framework

## Overview

`lamp::Lamp` is the base class for ESP32-WROOM smart-lamp implementations. It
provides the foundational contract between the Flutter control app and the
firmware: BLE GATT characteristics for real-time settings (brightness, color)
and the JSON section protocol for non-volatile personality storage (name, visual
behaviors, mood settings). The framework wires mesh discovery, pending-slot
multitasking, color/brightness overrides, and a pluggable behavior stack. It
does NOT include visual algorithms (animations, gradients, palette cycling), 
that's your `AnimatedBehavior` subclass's domain.

**Per-variant builds.** Each lamp variant has its own PIO environment
(`upesy_wroom_standard`, `upesy_wroom_snafu`). Variant identity is entirely
compile-time: the env's `-D LAMP_BUILD_VARIANT_*` flag both picks which
variant's sources compile in (paired with `build_src_filter`) and, via `#ifdef`
in `lamp_variants.hpp`, selects what `createCompiledLamp()` and
`compiledLampType()` return. The binary IS a variant; there is no runtime
registry lookup. Per-variant ed25519-signed binaries keep cross-variant and
cross-fork OTA cryptographically separated.

## Single-instance invariant

The framework assumes ONE Lamp subclass instance per firmware binary,
the compiled-in variant returned by `createCompiledLamp()` in
`lamp_variants.hpp`. This is deliberate, it lets compositor, config, frame buffers, NimBLE handles,
and aggregate state (pendingSlots, overrides) live at file scope without
coordination overhead. Two Lamp instances in one binary is unsupported.

Practical consequence: framework-level state in `core/lamp.cpp` is
file-scope-static, not `Lamp` private members. If you find a comment suggesting
"Lamp base will own this later," that's stale, single-instance IS the design.

## The contract (what your custom lamp must implement)

### BLE GATT and JSON section protocol

The app communicates with firmware via standard NimBLE GATT characteristics.
Two sections of the `LAMP_SECTION` characteristic carry personality settings and
color/expression data:

- **`lampSection`**, read/write JSON blob containing the lamp's identity
  (name, `colorsEditable` flags per surface, `lampType`, etc.).
- **`colorSection`**, page-protocol JSON subset for storing expressions,
  overrides, and visual configuration.

Refer to **[`docs/dev/networking.md`](networking.md)** for the complete wire-format
spec and all message types. The code is authoritative, update that doc when
behavior diverges.

### Protocol version lock

The wire carries a **receive range**, not a single version: a lamp broadcasts
`PROTOCOL_VERSION_EMIT` (`0x05`) but parses any frame in `[RX_MIN, RX_MAX]`
(`0x04`..`0x05`). The split lets the fleet *receive* a newer format before it
*emits* one — the safe path through a multi-version OTA wave. A frame whose
version falls outside the range is rejected, surfacing as a loud, diagnosable
failure (missing neighbors in the app's nearby-lamp list). Bump the version only
for a genuine parser-contract change — additive fields ride as TLVs (TLV-first
rule). See the protocol lock-in in `CLAUDE.md` and [`networking.md`](networking.md).

## Adding a custom lamp: 3-step recipe

### Step 1: Create `src/lamps/<name>/<name>.{hpp,cpp}`

Subclass `lamp::Lamp`, provide `HwConfig` in the constructor, override
`defaults()`, `featuresEnabled()`, and `createBehaviors()`. Add any custom
`AnimatedBehavior` subclasses in the same directory. Real example, 
`SnafuLamp`:

```cpp
// software/lamp-os/src/lamps/snafu/snafu_lamp.hpp
class SnafuLamp : public Lamp {
 public:
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

 protected:
  Features featuresEnabled() const override {
    return Features::All
      & ~Features::SocialBehavior      // replaced by snafu::Greeting
      & ~Features::DefaultExpressions  // snafu owns its own visuals
      & ~Features::WebApp;             // snafu configures over BLE
  }

  Config::Defaults defaults() const override {
    return {
      .name = "snafu",
      .setup = true,
      .baseSegments  = { {"Stem", 24, "#30078300,#64149600"} },
      .shadeSegments = {
        {"Small Dots",  16, "#962d0000,#097d0400,#003c7800,#78003200"},
        {"Medium Dots", 12, "#78100000,#a03c0000,#966e0000"},
        {"Big Dots",     9, "#9b000000,#612d2c00"},
      },
    };
  }

  void registerExpressions(ExpressionRegistry& reg) override {
    reg.add(GlitchyExpression::classDescriptor());
    reg.add(PulseExpression::classDescriptor());
    reg.add(SpottyExpression::classDescriptor());
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    if (!shadeFb()) return;
    dots_ = std::make_unique<snafu::DotsBehavior>(shadeFb(), config.shade);
    compositor.addBaseBehavior(dots_.get());
    greeting_ = std::make_unique<snafu::Greeting>(shadeFb());
    b.add(greeting_.get());
    greeting_->setDotsBehavior(dots_.get());
  }
  // ...
};
```

The shade role fans three physical dot strips (each its own palette, rendered
by the segment-aware `DotsBehavior`); the base role is the Stem broadcast
segment on the flat configurator. See
`software/lamp-os/src/core/hw_config.hpp` for the full `HwConfig` / `StripSpec`
POD definitions. See `core/lamp_features.hpp` for the `Features` bitmask enum.

### Step 2: Register the variant (two files, in lockstep)

This is a **per-variant build**: exactly one `LAMP_BUILD_VARIANT_<TYPE>` is
defined per binary, and `build_src_filter` excludes the other variants'
sources. A `standard` binary can only ever instantiate `StandardLamp`. Adding a
variant means updating two files together.

**1. `platformio.ini`** — a new `[env:upesy_wroom_<type>]` block that defines
`-D LAMP_BUILD_VARIANT_<TYPE>`, sets `custom_lamp_variant = <name>`, and adds a
`build_src_filter` arm including `lamps/<name>/`. Without that env there's no
way to compile the variant.

**2. `software/lamp-os/src/lamp_variants.hpp`** — four `#ifdef` sites keyed on
the same flag:

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

### Step 3: Build

```sh
npm run lamp:build              # standard variant
VARIANT=snafu npm run lamp:build   # any other variant
# (or directly: cd software/lamp-os && pio run -e upesy_wroom_<variant>)
```

Each variant has its own env, so `build_src_filter` compiles only that variant's
sources into its own binary; the compiled-in variant is the lamp's identity, and
NVS `lampType` is written from it at boot rather than consulted to pick it.

## Color slots

Every lamp has two **core** slots, always present:

- **Shade** (primary surface): its `colors` feed the BLE advertisement and the shade field in HELLO. Configured via `Config::Defaults::shadeColor` and `shadePx`; app visibility controlled by `shadeColorsEditable`.
- **Base** (secondary surface): its `colors` drive peer greeting handshakes. Configured via `Config::Defaults::baseColor` and `basePx`; app visibility controlled by `baseColorsEditable`.

A slot with exactly 1 color renders static; 2 or more animate.

First-boot randomization only touches surfaces whose variant left `baseColor` / `shadeColor` empty in `Config::Defaults`. A non-empty default survives randomization unchanged.

### Multi-segment roles

A role's colors can be a single scalar (`baseColor` / `shadeColor` + `basePx` / `shadePx`) or split into named segments. A variant declares segments via `Config::Defaults::baseSegments` / `shadeSegments` (a `std::vector<SegmentDefault>` of `{name, px, "csv,of,hex,colors"}`); an empty vector takes the single-segment scalar path. Snafu drives its three shade dot-strips (`Small Dots` / `Medium Dots` / `Big Dots`) and the `Stem` base entirely through segments, one palette per segment, rendered by the segment-aware `DotsBehavior`.

`HwConfig.strips` lists every physical NeoPixel strip; each entry carries a `role` (`Surface::Shade` or `Surface::Base`), pin, byte order, pixel count, and optional `name` / `broadcast` / `reversed` flags. Multiple strips can share a role (as the snafu shade does), and one `broadcast=1` strip per role is the representative for advertisement / HELLO.

## Provisioning a fresh hardware lamp

Pick the variant by flashing its env; the binary carries its identity.

```sh
pio run -e upesy_wroom_snafu -t upload --upload-port /dev/cu.usbserial-XXX
# or, from the repo root: VARIANT=snafu PORT=/dev/cu.usbserial-XXX npm run lamp:flash
```

There is no post-flash provisioning step. A reflash to the other variant's env
switches identity on the next boot (see below).

## Variant resolution at boot

The compiled-in variant is authoritative. `compiledLampType()` returns the
name baked in by `-D LAMP_BUILD_VARIANT_*`, and `main.cpp` writes it to the NVS
`lampType` key on every boot, overwriting a stale value left by a previous
cross-variant reflash:

```
const char* compiled = lamp::compiledLampType();
if (config.loadLampType() != compiled) config.setLampType(compiled);
g_lamp = lamp::createCompiledLamp();
```

NVS `lampType` is a persisted mirror, not an input to resolution: the firmware
never reads it to decide what to build, only writes it. Its purpose is to
expose the variant over the BLE `lampType` section field so the app can fetch
the matching per-variant OTA binary. A cross-variant OTA can't land it in the
wrong state anyway — the `{type}-{channel}` LSIG gate drops a mismatched image
before it flashes.

## BehaviorContext services

Every `AnimatedBehavior` receives a `BehaviorContext*` via
`setBehaviorContext()` before its first `control()` call. The context provides
read-only access to framework services:

```cpp
struct BehaviorContext {
  Compositor* compositor = nullptr;              // frame buffer state, behavior list
  ExpressionManager* expressionManager = nullptr;
  std::vector<FrameBuffer*> expressionFrameBuffers;   // [shade, base]
  ConfiguratorBehavior* baseConfigurator = nullptr;   // color/brightness fades
  ConfiguratorBehavior* shadeConfigurator = nullptr;
  LampRoster* lampRoster = nullptr;              // peer roster (near + mesh), RSSI
};
```

All service pointers are nullable. Always null-check before dereferencing.
Real example from `snafu::Greeting::control()`:

```cpp
void snafu::Greeting::control() {
  if (!context_ || !context_->lampRoster) {
    return;
  }
  // ... now safe to use context_->lampRoster
}
```

## Arrival edge detection

Detecting when a peer first appears maps to Python's
`await network.arrived()`. Use `getUngreetedArrivals()` + `acknowledge()`:

```cpp
for (const auto& p : context_->lampRoster->getUngreetedArrivals(/*maxAgeMs=*/5000)) {
  // peer arrived and hasn't been greeted yet
  context_->lampRoster->acknowledge(p.name);
}
```

`acknowledge()` marks the entry so it stops appearing in
`getUngreetedArrivals()` until the peer prunes out and returns.

## The colorsEditable flag

Per-section flag for a lamp whose surface color is fixed by design. When
`false`, the app hides the color picker for that surface. Set via
`Config::Defaults` in your lamp's `defaults()` override:

```cpp
Config::Defaults defaults() const override {
  return {
    .name                = "mylamp",
    .shadeColor          = "#78100000",
    .shadeColorsEditable = false,    // app hides the shade picker
    // ...
  };
}
```

The flag is firmware-owned, set at construction time, read by the app from the
lamp's JSON, and not updatable via settings-blob write. Absent field defaults to
`true` for backward compatibility with older firmware.

## OSS fork model + signing chain

Third parties fork the repo and add custom lamps without risk of cross-fleet OTA
attacks:

1. **Fork the repo** on GitHub (`git clone https://github.com/<your>/lamp-os.git`).

2. **Generate your keypair:**
   ```sh
   python3 scripts/gen_firmware_keys.py --force
   ```
   This generates an ed25519 keypair. The private key lands at
   `~/.lamp-os-firmware-key.bin`, never commit it. The public key is rewritten
   into `software/lamp-os/src/components/firmware/firmware_pubkey.h`, which you
   DO commit. Building without running this step first fails.

3. **Add your custom lamp** using the 3-step recipe above.

4. **Build + flash.** Your lamps validate OTAs against YOUR embedded pubkey.
   Firmware signed by the upstream repo (or any other fork) fails signature
   validation and is rejected.

This is the fork-isolation mechanism. Cross-fork OTA is cryptographically
impossible, no registry or allowlist needed. The signing chain enforces it.

## Stability matrix

### Stable API: third-party lamps may rely on

| Header | Stability guarantee |
|---|---|
| `core/lamp.hpp` | STABLE, the 3 virtuals (`defaults`, `featuresEnabled`, `createBehaviors`) are versioned API |
| `core/hw_config.hpp` | STABLE, POD, additive fields only |
| `core/lamp_features.hpp` | STABLE, flag values frozen; bits 2–4 unused. Do not reuse without consulting deployed clients |
| `core/behavior_stack_builder.hpp` | STABLE, use const `behaviors()` accessor |
| `core/animated_behavior.hpp` | STABLE, base class for custom behaviors |
| `core/behavior_context.hpp` | STABLE-ADDITIVE, new nullable pointers may be added; existing pointers won't be removed |
| `core/personality_engine.hpp` | STABLE, personality gate for expression suppression |
| `components/network/mesh/lamp_roster.hpp` | STABLE, read by custom behaviors |
| `config/config.hpp` | STABLE, read-only surface for custom behaviors |

### Internal headers: may change without notice

| Header | Why internal |
|---|---|
| `core/pending_slot_aggregate.hpp` | framework dual-core hand-off implementation |
| `core/override_aggregate.hpp` | framework transient-override state |
| `core/pending_json_slot.hpp` | JSON-typed variant of the pending slot |
| `core/pending_typed_slot.hpp` | typed variant of the pending slot |
| `behaviors/` | built-in `AnimatedBehavior` subclasses (SocialBehavior, FadeOutBehavior, KnockoutBehavior, FadeInBehavior, IdleBehavior, ConfiguratorBehavior), internal, subject to change |
| `components/network/mesh/mesh_link.hpp` | wire-protocol dispatch |

## Testing pattern for contributed variants

Native tests (`pio test -e native`) cannot link against Arduino, NimBLE, or
Adafruit_NeoPixel. Use the inline-mirror pattern: re-implement just the
predicate or emit logic in the test, verify against expected output. Existing
examples:

- `test/test_hw_config_validate/hw_config_validate.cpp`, validates
  `validateHwConfig()` predicate (empty surfaces, duplicate pins) without
  touching Arduino. (Pixel counts live in `Config::Defaults`, not `HwConfig`,
  so they aren't part of this predicate.)
- `test/test_lamp_type_emit/lamp_type_emit.cpp`, mirrors the `lampType` JSON
  emit line from `config.cpp::asLampJson`; verifies shape without linking
  `Config`.
- `test/test_section_emit_colors_editable/`, mirrors
  `BaseSettings`/`ShadeSettings` colorsEditable emit; verifies the JSON field
  shape.

For contributed variants, the minimum acceptable test:

```
test/test_<variant>_lamp_boot/<variant>_lamp_boot.cpp
```

Verify the `HwConfig` has the expected pin layout and surfaces match the
physical hardware. The test should construct the `HwConfig` POD inline (no
Arduino) and call `validateHwConfig()`.

## Key rotation

Losing the ed25519 private key (`~/.lamp-os-firmware-key.bin`) means the
deployed fleet can never accept new OTAs, the embedded pubkey is baked into
each lamp's flash and `firmware_signature.cpp` will reject anything signed with
the new key. Recovery requires USB-reflashing every lamp.

No in-protocol key rotation exists. Back up the private key to an encrypted
password manager entry immediately after running `gen_firmware_keys.py`.

## Crypto construction note

Firmware signing uses Ed25519 over SHA-256 of the signed region
("hash-then-sign"). This is not strictly RFC 8032 Ed25519 (which hashes
internally with SHA-512), but is sound in practice: SHA-256 collision resistance
is the security prerequisite, and that is well-established cryptographic art.
Relevant files: `scripts/sign_firmware.py` (signer),
`software/lamp-os/src/components/firmware/firmware_signature.cpp` (verifier).

## PIO build setup

Per-variant envs: `upesy_wroom_standard`, `upesy_wroom_snafu` (both extend
`env_base_upesy`). Each sets `-D LAMP_BUILD_VARIANT_<NAME>` and a
`build_src_filter` that compiles **only** that variant's `lamps/<variant>/`
sources, the other variant is never built. Each also declares
`custom_lamp_variant = <name>`, which `inject_firmware_channel.py` and
`sign_firmware.py` read (via `env.GetProjectOption()`) to build the
`{type}-{channel}` OTA gate string.

**SemVer source-of-truth.** The root `VERSION` file (`MAJOR.MINOR.PATCH`, one
line) is the canonical version for both lamp and wisp. `inject_version.py` (a
PIO pre-build hook wired in both components' `platformio.ini`) parses it and
injects `-D LAMP_FW_MAJOR/MINOR/PATCH`:

```
1.1.1
```

`FIRMWARE_VERSION` / `FIRMWARE_VERSION_STR` are derived from those macros.
Bumping a release = edit one file; no other file needs to change.

## Lifecycle: Core 0 vs Core 1

The ESP32 runs dual cores. Understanding which runs where is critical for safe
multitasking:

- **Core 0** (NimBLE + WiFi host tasks): all BLE GATT callbacks, ESP-NOW mesh
  receive handler.
- **Core 1** (Arduino loop task): main loop, Compositor ticks,
  `AnimatedBehavior::control()`/`draw()`.

### Pending slot hand-off

Async work arriving on Core 0 (e.g., a BLE write from the app) must not block
or allocate directly on the host task. Instead, use
`lamp::pendingSlots.X.post(callback)` to queue the work for Core 1 to drain in
`Lamp::tick()`.

### Rules for custom behaviors

**Safe on Core 1:**
- All `AnimatedBehavior::control()` and `draw()` code.
- Heap allocation, NVS reads/writes, Compositor state mutations.

**DO NOT do on Core 1:**
- Call into NimBLE APIs (owned by Core 0).
- Perform mesh operations (Core 0 task).

**DO NOT do on Core 0 (inside BLE callbacks or ESP-NOW handlers):**
- Allocate heap memory.
- Call blocking functions.
- Mutate Compositor or Lamp state directly, queue to pending slots instead.

### Home mode

"Home mode" is the lamp's idle/resting state, dimmed and quiet. The
Compositor tracks it (`Compositor::setHomeMode(bool)`) and gates each
behavior via two virtual queries on `AnimatedBehavior`:
`isSocialBehavior()` (returns `true` on `SocialBehavior`) and
`homeModeExpressionId()` (returns the expression type id string on
`Expression` subclasses, `nullptr` otherwise). The suppression policy
(which social and which expression ids to skip) is pushed by
`reapplyHomeModeState()` via `Compositor::setHomeModePolicy()` and comes
from `HomeModeSettings::socialDisabled` and
`HomeModeSettings::disabledExpressionTypes`. The render rule is
`!homeMode || !homeModeSkips(b)`; behaviors not matching either query
always draw.

## Brightness path and the power governor

Every brightness writer funnels through `lamp::setAllStripsBrightness`:
`applyEffectiveBrightness()` (home-mode/user baseline → personality
crowd-dim → transient override → `calculateBrightnessLevel`), the slider
micro-fade in `Lamp::tick`, and the `apply::brightness*` helpers (BLE
slider, mesh cascade, settings_blob) all end there. The funnel latches the
requested level and hands `min(requested, governor ceiling)` to every strip
driver, so the governor caps every path with one `min()` and never touches
the computed baseline.

The ceiling `calculateBrightnessLevel` scales against is not the raw variant
`HwConfig::maxBrightness` but `effectiveCeiling(hw_.maxBrightness,
config.lamp.brightnessCeiling)` — the variant cap narrowed by the user's
Battery Saver setting, floored at 1, cached in `s_hwMaxBrightness`.
`Lamp::recomputeEffectiveCeiling()` refreshes it at setup and on every
settings-blob write, so a new ceiling applies without a reboot.

The estimator (`core/power_governor.hpp`) prices the frame the drivers are
about to show. `fullDutyMa` gamma-sums each surface's FrameBuffer at a
per-channel full-duty draw (W counts only on 4-channel strips); `demandMa`
scales that by the NeoPixel `(level+1)/256` factor at the requested
(pre-clamp) level and adds a per-pixel idle draw. The compositor's
`preFlushHook` (`governFrame`, `lamp.cpp`) runs it once per drawn frame,
after behaviors draw and before any pixel write, so a clamp decision
reaches the drivers ahead of that frame's `setPixelColor` loop —
`setBrightness` after `setPixelColor` destructively rescales the frame it
was meant to protect.

`PowerGovernor` compares demand against `HwConfig::supplyBudgetMa` minus a
reserve that widens while the radio is hot (OTA in either direction, quiet
mode, or a BLE client) or during the boot window and narrows when quiet.
Any frame over budget at the level about to be written snaps the ceiling to
the level that fits inside the same frame (`senseFrame` returns true and
`governFrame` re-mins the drivers before the flush). While clamped,
per-frame re-solves only move the ceiling down; recovery goes through the
release, paced inside `PowerGovernor::tick` (called from `Lamp::tick`,
which also advances the boot ramp), which returns the governor to dormant
once demand falls a set margin below budget — there the ceiling is 255 and
the funnel `min()` is identity. The ceiling glides back up rather than
snapping, and every cold boot holds a reduced ceiling before ramping to
full to ride out supply inrush. Tuning constants (budget reserve, release
margin, glide + boot-ramp timing) live in `core/power_governor.cpp`; the
per-pixel draw constants in
`software/shared/led-common/src/lampos/led_power.hpp`. The
`qa/power-governor.md` runbook is the hardware pass.

## File index

| File | Purpose |
|---|---|
| `software/lamp-os/src/core/lamp.hpp` | Base class: setup/tick entry points, hw config accessor |
| `software/lamp-os/src/core/lamp.cpp` | Implementation: BLE GATT wiring, mesh init, OTA health checks |
| `software/lamp-os/src/lamp_variants.hpp` | `createCompiledLamp()` + `compiledLampType()`, compile-time variant selection |
| `software/lamp-os/src/core/hw_config.hpp` | `HwConfig`, `StripSpec`, `Surface`, `ByteOrder` PODs + `validateHwConfig()` |
| `software/lamp-os/src/core/lamp_features.hpp` | `Features` bitmask enum for built-in behavior opt-in/out |
| `software/lamp-os/src/core/behavior_stack_builder.hpp` | `BehaviorStackBuilder` helper for registering behaviors |
| `software/lamp-os/src/core/pending_slot_aggregate.hpp/.cpp` | Core 0→1 hand-off mechanism for async work |
| `software/lamp-os/src/core/override_aggregate.hpp/.cpp` | Transient color/brightness overrides (60 s watchdog release) |
| `software/lamp-os/src/core/personality_engine.hpp/.cpp` | Personality gate for expression suppression |
| `software/lamp-os/src/core/behavior_context.hpp` | `BehaviorContext` struct: service pointers for behaviors |
| `software/lamp-os/src/core/animated_behavior.hpp` | `AnimatedBehavior` base class: control/draw interface |
| `software/lamp-os/src/core/frame_buffer.hpp/.cpp` | `FrameBuffer`: the per-surface pixel buffer `draw()` writes into |
| `software/lamp-os/src/core/power_governor.hpp/.cpp` | Current estimator + supply-budget brightness governor |
| `software/lamp-os/src/core/compositor.hpp/.cpp` | `Compositor`: blends behavior layers, home-mode gate, dynamic add/remove |
| `software/lamp-os/src/lamps/standard/standard_lamp.hpp/.cpp` | Production fleet lamp (built-in social, expressions, idle) |
| `software/lamp-os/src/lamps/snafu/snafu_lamp.hpp/.cpp` | Amanita mushroom lamp: reference variant |
| `software/lamp-os/src/lamps/snafu/dots_behavior.hpp/.cpp` | Segment-aware base-layer scatter scene for the snafu shade; crossfades old→new per cycle, pauses under wisp override |
| `software/lamp-os/src/lamps/snafu/greeting.hpp/.cpp` | Peer-arrival watcher with glitch + fade response |
| `software/lamp-os/src/main.cpp` | Unified entry point: mirrors compiled variant into NVS, instantiates it |
| `software/lamp-os/platformio.ini` | Per-variant envs (`upesy_wroom_standard`/`_snafu`) extending `env_base_upesy`; SemVer build_flags |
| `scripts/gen_firmware_keys.py` | Per-fork ed25519 keypair generation |
| `scripts/sign_firmware.py` | Build-time firmware signer |
| `software/lamp-os/src/components/firmware/firmware_signature.cpp` | OTA-receive-time signature verifier (streams the image from flash before `esp_ota_set_boot_partition`) |
| `software/lamp-os/src/components/firmware/firmware_pubkey.h` | Embedded public key (committed; fork-specific) |

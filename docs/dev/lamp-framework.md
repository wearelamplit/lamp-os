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
(`upesy_wroom_standard`, `upesy_wroom_snafu`). A `build_src_filter` compiles
only that variant's `lamps/<variant>/` code, and a `-D LAMP_BUILD_VARIANT_*`
flag seeds the variant on a fresh NVS. At boot, `resolveLampType()` in
`main.cpp` reads `Config::lampType` from NVS (falling back to the build-flag
seed on first boot), then `createLampByType()` instantiates the matching
subclass. Per-variant ed25519-signed binaries keep cross-variant and cross-fork
OTA cryptographically separated.

## Single-instance invariant

The framework assumes ONE Lamp subclass instance per firmware binary,
selected at boot by the `core/lamp_variants.cpp` factory (per NVS `lampType`).
This is deliberate, it lets compositor, config, frame buffers, NimBLE handles,
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

Refer to **[`docs/dev/mesh-api.md`](mesh-api.md)** for the complete wire-format
spec and all message types. The code is authoritative, update that doc when
behavior diverges.

### Protocol version lock

`PROTOCOL_VERSION = 0x04`. Mixed-fleet
operation across protocol versions does not interoperate, peers reject frames
from mismatched versions, surfacing as a loud, diagnosable failure (missing
neighbors in the app's nearby-lamp list). Do not bump without coordinating a
re-flash of all 22 lamps in the deployed fleet.

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
    .surfaces = {
      {.id=Surface::Shade, .pin=12, .byteOrder=ByteOrder::GRBW},
      {.id=Surface::Base, .pin=14, .byteOrder=ByteOrder::GRBW},
    },
    .maxBrightness = 180,
  }) {}

 protected:
  Features featuresEnabled() const override {
    return Features::All
      & ~Features::SocialBehavior      // replaced by snafu::Greeting
      & ~Features::DefaultExpressions; // snafu owns its own visuals
  }

  Config::Defaults defaults() const override {
    return {
      .name              = "snafu",
      .baseColor         = "#300783",  // purple stem, user-editable
      .shadeColor        = "#781000",  // amanita red, picker hidden in app
      .baseColorsEditable  = true,
      .shadeColorsEditable = false,
    };
  }

  void createBehaviors(BehaviorStackBuilder& b) override {
    if (shadeFb()) {
      bgFade_     = std::make_unique<snafu::BackgroundFade>(shadeFb());
      paintSpots_ = std::make_unique<snafu::PaintSpots>(shadeFb(), 24, 32);
      greeting_   = std::make_unique<snafu::Greeting>(shadeFb());
      b.add(bgFade_.get());
      b.add(paintSpots_.get());
      b.add(greeting_.get());
    }
  }
  // ...
};
```

See `software/lamp-os/src/core/hw_config.hpp` for the full `HwConfig` /
`SurfaceSpec` POD definitions. See `core/lamp_features.hpp` for the `Features`
bitmask enum.

### Step 2: Register in the variant array

Edit `software/lamp-os/src/core/lamp_variants.cpp`, three lines:

```cpp
// 1. Add the include near the top
#include "lamps/<name>/<name>.hpp"

// 2. Add a factory inline function
inline std::unique_ptr<Lamp> make<Name>() {
  return std::make_unique<<Name>Lamp>();
}

// 3. Append to kLampVariants
constexpr std::array<NamedFactory, N+1> kLampVariants = {{
  {"standard", makeStandard},
  {"snafu",   makeSnafu},
  {"<name>",  make<Name>},  // ← add here
}};
```

`main.cpp` calls `createLampByType()` which iterates `kLampVariants`. No other
file needs to change.

### Step 3: Build

```sh
npm run lamp:build          # standard variant
npm run lamp:build:snafu    # snafu variant
# (or directly: cd software/lamp-os && pio run -e upesy_wroom_<variant>)
```

Each variant has its own env, so `build_src_filter` compiles only that variant's
sources into its own binary. The runtime factory still resolves the type from
NVS at boot.

## Provisioning a fresh hardware lamp

### Command-line (scripted provisioning)

```sh
pio run -e upesy_wroom_snafu -t upload --upload-port /dev/cu.usbserial-XXX
# or, from the repo root: npm run lamp:flash:snafu
```

The per-variant env declares `custom_lamp_variant = snafu`, which
`software/lamp-os/scripts/inject_initial_type.py` (a PIO pre-build hook) reads
via `env.GetProjectOption()` and turns into `-D LAMP_INITIAL_TYPE="snafu"`
(SCons `CPPDEFINES` tuple form, bypassing shell-quoting hazards). On first boot, `main.cpp` seeds
NVS with this value; subsequent OTAs preserve identity.

### Serial CLI (interactive)

Flash a stock binary without `LAMP_TYPE` set, and the lamp's NVS is empty: the
lamp listens 10 seconds on USB serial for `LAMP_TYPE=<name>\n`:

```sh
echo "LAMP_TYPE=snafu" > /dev/cu.usbserial-XXX
```

The value persists to NVS and the lamp reboots into the new variant. Subsequent
OTAs preserve identity. The CLI validates the name against `kLampVariants` and
rejects unknown types (with the known-names list printed to serial).

## Variant resolution at boot

`main.cpp`'s `resolveLampType()` priority order:

1. **NVS `lampType`**, set on a previous boot or via serial CLI
2. **`LAMP_INITIAL_TYPE` build flag**, first-boot seed injected from the env's
   `custom_lamp_variant`; persists to NVS on first use
3. **Serial CLI prompt**, 10-second window on USB serial when both above are
   absent
4. **Default `"standard"`**, canonical fallback

The resolved type is always logged: `[lamp] resolved lampType="X"`. On an
unknown type, `main.cpp` calls `haltVisible()`, an infinite LED blink, rather
than silently falling back. A lamp that boots healthy but drives wrong hardware
is the worst failure mode.

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
  NearbyLamps* nearbyLamps = nullptr;            // peer discovery, RSSI, proximity
};
```

All service pointers are nullable. Always null-check before dereferencing.
Real example from `snafu::Greeting::control()`:

```cpp
void snafu::Greeting::control() {
  if (!context_ || !context_->nearbyLamps) {
    lastTickMs_ = millis();
    return;
  }
  // ... now safe to use context_->nearbyLamps
}
```

## Arrival edge detection: firstSeenMs

Detecting when a peer first appears on the mesh maps to Python's
`await network.arrived()`. Use the 3-line pattern:

```cpp
for (const auto& p : context_->nearbyLamps->getReachableViaBle(/*maxAgeMs=*/5000)) {
  if (p.firstSeenMs >= lastTickMs_ && p.bdAddr != lastGreetedBdAddr_) {
    // peer just arrived, this fires exactly once per arrival
  }
}
lastTickMs_ = millis();
```

`firstSeenMs` is set once when the peer first enters the nearby-lamp registry
and stays locked there through subsequent RSSI refreshes. Comparing
`firstSeenMs >= lastTickMs_` fires exactly one tick when they arrive.
`lastTickMs_` is updated at the end of `control()` so the comparison doesn't
fire again next cycle.

## The colorsEditable flag

Per-section flag. When `false` (e.g., snafu's shade), the app hides the color
picker for that surface. Set via `Config::Defaults` in your lamp's `defaults()`
override:

```cpp
Config::Defaults defaults() const override {
  return {
    .name              = "snafu",
    .shadeColor        = "#781000",  // amanita red
    .shadeColorsEditable = false,    // app hides picker for shade
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
| `core/lamp_features.hpp` | STABLE, flag values frozen; bits 3–4 unused. Do not reuse without consulting deployed clients |
| `core/behavior_stack_builder.hpp` | STABLE, use const `behaviors()` accessor |
| `core/animated_behavior.hpp` | STABLE, base class for custom behaviors |
| `core/behavior_context.hpp` | STABLE-ADDITIVE, new nullable pointers may be added; existing pointers won't be removed |
| `core/personality_engine.hpp` | STABLE, personality gate for expression suppression |
| `components/network/nearby_lamps.hpp` | STABLE, read by custom behaviors |
| `config/config.hpp` | STABLE, read-only surface for custom behaviors |

### Internal headers: may change without notice

| Header | Why internal |
|---|---|
| `core/pending_slot_aggregate.hpp` | framework dual-core hand-off implementation |
| `core/override_aggregate.hpp` | framework transient-override state |
| `core/pending_json_slot.hpp` | JSON-typed variant of the pending slot |
| `core/pending_typed_slot.hpp` | typed variant of the pending slot |
| `behaviors/` | built-in `AnimatedBehavior` subclasses (SocialBehavior, FadeOutBehavior, KnockoutBehavior, FadeInBehavior, IdleBehavior, ConfiguratorBehavior), internal, subject to change |
| `components/network/show_receiver.hpp` | wire-protocol dispatch |

## Testing pattern for contributed variants

Native tests (`pio test -e native`) cannot link against Arduino, NimBLE, or
Adafruit_NeoPixel. Use the inline-mirror pattern: re-implement just the
predicate or emit logic in the test, verify against expected output. Existing
examples:

- `test/test_hw_config_validate/hw_config_validate.cpp`, validates
  `validateHwConfig()` predicate (empty surfaces, zero pixelCount, duplicate
  pins) without touching Arduino.
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
`env_base_upesy`). Each declares `custom_lamp_variant = <name>` and a
`build_src_filter` that compiles **only** that variant's `lamps/<variant>/`
sources, the other variant is never built.

**`inject_initial_type.py` hook** is wired as a PIO pre-build script
(`extra_scripts = pre:scripts/inject_initial_type.py` in `env_base_upesy`). It
reads the env's `custom_lamp_variant` option and appends
`-D LAMP_INITIAL_TYPE="<name>"` to `CPPDEFINES` using SCons tuple form, no
shell-quoting hazards.

**SemVer source-of-truth.** The three build_flags in `[env_base_upesy]` of
`platformio.ini` are the canonical version:

```ini
-D LAMP_FW_MAJOR=1
-D LAMP_FW_MINOR=0
-D LAMP_FW_PATCH=82
```

`FIRMWARE_VERSION_STR` is derived from these via preprocessor stringify.
Bumping a release = edit three integers; no other file needs to change.

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

"Home mode" is the lamp's idle/resting state, dimmed, quiet, no active
expressions, entered when nothing is going on. The Compositor tracks it
(`Compositor::setHomeMode(bool)`) and gates each behavior on the
`allowedInHomeMode` flag on `AnimatedBehavior` (default `true`): the
render rule is `!homeMode || allowedInHomeMode`, so by default a behavior
keeps drawing even while the lamp rests. Set `allowedInHomeMode = false`
to suppress your behavior during the idle/resting state, the framework
then skips its `draw()` until home mode releases.

## File index

| File | Purpose |
|---|---|
| `software/lamp-os/src/core/lamp.hpp` | Base class: setup/tick entry points, hw config accessor |
| `software/lamp-os/src/core/lamp.cpp` | Implementation: BLE GATT wiring, mesh init, OTA health checks |
| `software/lamp-os/src/core/lamp_variants.hpp` | `createLampByType()` + `knownLampTypes()` declarations |
| `software/lamp-os/src/core/lamp_variants.cpp` | `kLampVariants` array, append new variants here |
| `software/lamp-os/src/core/hw_config.hpp` | `HwConfig`, `SurfaceSpec`, `Surface`, `ByteOrder` PODs + `validateHwConfig()` |
| `software/lamp-os/src/core/lamp_features.hpp` | `Features` bitmask enum for built-in behavior opt-in/out |
| `software/lamp-os/src/core/behavior_stack_builder.hpp` | `BehaviorStackBuilder` helper for registering behaviors |
| `software/lamp-os/src/core/pending_slot_aggregate.hpp/.cpp` | Core 0→1 hand-off mechanism for async work |
| `software/lamp-os/src/core/override_aggregate.hpp/.cpp` | Transient color/brightness overrides (60 s watchdog release) |
| `software/lamp-os/src/core/personality_engine.hpp/.cpp` | Personality gate for expression suppression |
| `software/lamp-os/src/core/behavior_context.hpp` | `BehaviorContext` struct: service pointers for behaviors |
| `software/lamp-os/src/core/animated_behavior.hpp` | `AnimatedBehavior` base class: control/draw interface |
| `software/lamp-os/src/core/frame_buffer.hpp/.cpp` | `FrameBuffer`: the per-surface pixel buffer `draw()` writes into |
| `software/lamp-os/src/core/compositor.hpp/.cpp` | `Compositor`: blends behavior layers, home-mode gate, dynamic add/remove |
| `software/lamp-os/src/lamps/standard/standard_lamp.hpp/.cpp` | Production fleet lamp (built-in social, expressions, idle) |
| `software/lamp-os/src/lamps/snafu/snafu_lamp.hpp/.cpp` | Amanita mushroom lamp: reference variant |
| `software/lamp-os/src/lamps/snafu/background_fade.hpp/.cpp` | Shade palette-cycle behavior (12 scenes, 45 s per scene) |
| `software/lamp-os/src/lamps/snafu/paint_spots.hpp/.cpp` | Sub-region palette cycle (9 white spots, pixels 24–32) |
| `software/lamp-os/src/lamps/snafu/greeting.hpp/.cpp` | Peer-arrival watcher with glitch + fade response |
| `software/lamp-os/src/main.cpp` | Unified entry point: variant resolution + factory + FATAL halt |
| `software/lamp-os/scripts/inject_initial_type.py` | PIO pre-build hook for `LAMP_TYPE` provisioning |
| `software/lamp-os/platformio.ini` | Per-variant envs (`upesy_wroom_standard`/`_snafu`) extending `env_base_upesy`; SemVer build_flags |
| `scripts/gen_firmware_keys.py` | Per-fork ed25519 keypair generation |
| `scripts/sign_firmware.py` | Build-time firmware signer |
| `software/lamp-os/src/components/firmware/firmware_signature.cpp` | Boot-time signature verifier |
| `software/lamp-os/src/components/firmware/firmware_pubkey.h` | Embedded public key (committed; fork-specific) |

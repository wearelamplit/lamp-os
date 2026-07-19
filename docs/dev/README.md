# Lamp OS: Developer Handbook

The developer guide for the Lamp OS fleet. Start here if you're building lamp
firmware, hacking on the wisp, working on the control app, or authoring a custom
lamp. (For the project pitch, hardware build, and contribution flow, see the
[root README](../../README.md).)

## The three components

Everything lives under [`software/`](../../software/):

| Component | What it is | Build env |
|---|---|---|
| [`lamp-os/`](../../software/lamp-os/) | Lamp firmware (ESP32-WROOM): BLE GATT control for the app + ESP-NOW mesh between lamps | `upesy_wroom_standard` / `upesy_wroom_snafu` |
| [`wisp/`](../../software/wisp/) | Wisp infrastructure node (Seeed XIAO ESP32-C6): Aurora palette subscriber + mesh paint distributor + status beacon | `seeed_xiao_esp32_c6` |
| [`lamp-app-flutter/`](../../software/lamp-app-flutter/) | iOS/Android control app: talks BLE GATT to lamps; no direct mesh participation | (Flutter) |

The app talks BLE to lamps. The wisp reaches the app only by proxy, it
broadcasts over the mesh, lamps cache that, and the app reads it off a connected
lamp. The wisp is **USB-flash-only** (no OTA), so it must be re-flashed whenever
the protocol version bumps or it goes silently invisible on the mesh.

## Quickstart

All commands are npm tasks (run from the repo root, see
[`package.json`](../../package.json)); they wrap the underlying PlatformIO /
Flutter / adb calls so the scripts stay exercised.

```sh
# Lamp firmware
npm run lamp:test         # native unit tests
npm run lamp:build        # build (standard variant; VARIANT=snafu for others)
npm run lamp:flash        # flash a connected lamp (standard, beta channel)
                          # VARIANT / CHANNEL / PORT env params override each
npm run lamp:monitor      # serial monitor

# Wisp firmware
npm run wisp:build
npm run wisp:flash
npm run wisp:monitor

# Control app (Android device connected)
npm run app:test          # unit + widget tests
npm run app:analyze       # static analysis
npm run app:install       # build + adb install -r (no data wipe) + launch
npm run app:codegen       # regenerate freezed / riverpod / json
```

Native firmware tests run in CI and must stay green. Build
PlatformIO via `pip install platformio` (the npm tasks call `pio` under the
hood); the Flutter toolchain setup is in the [root README](../../README.md).

## Guides

### Dev environment
- [`environment/`](environment/README.md), toolchain setup, npm task catalog,
  flashing a lamp, and the emulator+bridge path for development without a phone.

### Architecture
- [`lamp-framework.md`](lamp-framework.md), the lamp's core runtime: behavior
  stack, compositor/overrides, the per-variant build model, and the single-
  instance + boot invariants.

### Conventions
- [`code-smells.md`](code-smells.md), a catalog of code smells as
  *heuristics* (with the "when it's actually fine" cases), to reason about
  refactors. Pairs with the comment policy and conventions in
  [`CLAUDE.md`](../../CLAUDE.md).

### Subsystems
- [`expressions.md`](expressions.md), the auto-triggered animation subsystem:
  how to write a new expression, the wisp-override gate, the testing pattern.
- [`social.md`](social.md), overview of how a lamp behaves around other lamps,
  greetings and crowd-dim, and how they fit together. **Start here** for the
  social system, then the two docs below.
- [`personality-greetings.md`](personality-greetings.md), disposition-driven
  greeting animations (how lamps acknowledge peers they meet on the mesh).
- [`personality-signals.md`](personality-signals.md), the signals a custom
  lamp can react to (crowd weight/composition, presence, time, etc.).
- [`utilities.md`](utilities.md), the mechanical helper toolbox
  expression/behavior authors call (color math, fades, randomness, the pixel
  buffer, peer queries, disposition lookups).

### Protocol reference
- [`networking.md`](networking.md), the authoritative wire-format spec for the
  ESP-NOW mesh **and** the BLE GATT link. **The code wins ties**, update this
  doc when it doesn't.

### Decisions
- [`../adrs/`](../adrs/README.md), the **Architecture Decision Records** — the
  significant, hard-to-reverse choices that shape the firmware (ESP-NOW mesh,
  OTA over mesh, the dual-core concurrency model, …) and the alternatives they
  rejected. Read these for the *why* behind the lock-ins.

### Custom lamps
Building your own variant or behaviour? Start with
[`lamp-framework.md`](lamp-framework.md) (the runtime you plug into) and
[`personality-signals.md`](personality-signals.md) (what you can react to), then
[`expressions.md`](expressions.md) for custom animations and
[`utilities.md`](utilities.md) for the helper toolbox they're built from. We'd love you to
upstream it, see [Contributing](../../README.md#contributing).

---

Design specs and audit reports aren't kept here, once a feature ships, the code
is the source of truth. For history, use `git log`.

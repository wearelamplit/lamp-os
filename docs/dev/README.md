# Lamp OS: Developer Handbook

The developer guide for the Lamp OS fleet. Start here if you're building lamp
firmware, hacking on the wisp, working on the control app, or authoring a custom
lamp. (For the project pitch, hardware build, and contribution flow, see the
[root README](../../README.md).)

## The big picture

Three components under [`software/`](../../software/) — the lamp firmware
(ESP32-WROOM), the wisp infrastructure node (Seeed XIAO ESP32-C6), and the
Flutter control app — linked by BLE GATT, an ESP-NOW mesh, and the wisp's Aurora
feed. The one-screen map of components, links, data flows, and where each
subsystem lives is [`architecture.md`](architecture.md). **Start there** for
orientation.

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

### Big picture
- [`architecture.md`](architecture.md), the three components, the links between
  them, the data flows, and where each subsystem lives.

### Custom lamps
Building your own variant or behaviour?
[`building-custom-lamps.md`](building-custom-lamps.md) is the author's guide —
the configure → subclass → call → utils narrative with snafu (social) and staff
(physical) as worked examples, plus the build/ship path.
[`lamp-social-api.md`](lamp-social-api.md) is the lamp-to-lamp API it reacts
with (`BehaviorContext`, `PeerView`, arrivals). Built something cool? Send
it back upstream, see [Contributing](../../README.md#contributing).

### Runtime internals
- [`lamp-framework.md`](lamp-framework.md), the lamp's core runtime: single-
  instance model, compositor, dual-core split, power governor, and the boot /
  variant-resolution invariants.

### Subsystems
- [`expressions.md`](expressions.md), the auto-triggered animation subsystem:
  the descriptor model, the wisp-override gate, the testing pattern, and the
  new-expression skeleton.
- [`social.md`](social.md), overview of how a lamp behaves around other lamps,
  greetings and crowd-dim, and how they fit together. **Start here** for the
  social system, then the docs below.
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

### Operations + environment
- [`environment/`](environment/README.md), toolchain setup, npm task catalog,
  flashing a lamp, and the emulator+bridge path for development without a phone.
- [`debug-instruments.md`](debug-instruments.md), the `LAMP_DEBUG` serial
  bracket-tag catalog (lamp + wisp): which tag to grep for what.

### Conventions
- [`code-smells.md`](code-smells.md), a catalog of code smells as
  *heuristics* (with the "when it's actually fine" cases), to reason about
  refactors. Pairs with the comment policy and conventions in
  [`CLAUDE.md`](../../CLAUDE.md).
- [`embedded-heap.md`](embedded-heap.md), heap discipline for the tight,
  fragmented lamp/wisp heap. Read before adding any roster / per-peer / hot-path
  feature.

### Decisions
- [`../adrs/`](../adrs/README.md), the **Architecture Decision Records** — the
  significant, hard-to-reverse choices that shape the firmware (ESP-NOW mesh,
  OTA over mesh, the dual-core concurrency model, …) and the alternatives they
  rejected. Read these for the *why* behind the lock-ins.

---

Design specs and audit reports aren't kept here, once a feature ships, the code
is the source of truth. For history, use `git log`.

# Lamp OS

ESP32-based smart-lamp fleet. Three components:

- **`software/lamp-os/`**, lamp firmware (ESP32-WROOM). BLE GATT control
  service for the Flutter app + ESP-NOW mesh for lamp-to-lamp. Build envs
  `upesy_wroom_standard` / `upesy_wroom_snafu` (one per variant).
- **`software/wisp/`**, wisp infrastructure node (Seeed Xiao ESP32-C6,
  external antenna). Aurora palette subscriber + mesh paint distributor +
  status beacon. Build env `seeed_xiao_esp32_c6`.
- **`software/lamp-app-flutter/`**, iOS/Android control app (Flutter).
  Talks BLE GATT to lamps; no direct mesh participation.

## Authoritative references

Read these before changing networking, protocol, or BLE behavior:

- **[`docs/dev/mesh-api.md`](docs/dev/mesh-api.md)**, wire-format spec for every
  message type. The code wins ties; update this doc when it doesn't.

The full developer handbook (architecture, expressions, personality, security)
lives in [`docs/dev/`](docs/dev/).

## Lock-ins (don't change without a protocol version bump)

`PROTOCOL_VERSION = 0x04` is the production wire format for the deployed
fleet, MSG_EVENT gossip-relays, DedupRing capacity is 64, HELLO interval
is 5 s. Mixed-fleet across protocol versions does not interoperate (loud,
diagnosable failure: peers don't show up).

The **BLE GATT attribute layout is a frozen contract**, same status as the
mesh wire format. Handles are positional, so adding / removing / reordering a
characteristic (or adding a NOTIFY → CCCD) shifts handles and can stale-out
already-connected Android installs ("silent wrong-handle reads"). We can't fix
that the spec-correct way, Service Changed needs link-layer bonding, which
conflicts with our app-layer AES-GCM auth, and `clearGattCache()` /
`refresh()` is hidden-API-blocked on Android 14+. So we freeze the layout
instead:

- `kGattLayout` / `kGattSchemaVersion` in
  `components/network/gatt_layout.hpp` are the single source of truth.
  `CHAR_SCHEMA_VERSION` exposes the version; the app reads it
  (`BleClient.readGattSchemaVersion`, legacy lamps → 0).
- **Grow append-only** (new characteristic at the tail) or evolve a
  characteristic's *payload* (settings_blob / sections / page-data), neither
  moves an existing handle, so deployed installs keep working.
- Insert / remove / reorder / add-CCCD is a **breaking** change: bump
  `kGattSchemaVersion` and re-pin the hash in `test_ble_gatt_layout` (the
  native test fails until you do), and expect a one-time force-stop on
  already-paired phones. The boot-time assert in `ble_control.cpp` catches
  table-vs-registration drift.

## Build + test

```sh
# Native unit tests (lamp side, runs in CI)
cd software/lamp-os && pio test -e native

# Build lamp firmware (one env per variant)
cd software/lamp-os && pio run -e upesy_wroom_standard   # or upesy_wroom_snafu

# Build wisp firmware
cd software/wisp && pio run -e seeed_xiao_esp32_c6
```

The native suite covers protocol parsers, dedup ring, color math, fade
math, cascade dedup. Keep it green, currently 347/347.

## Tail two lamps simultaneously

A small Python helper at `/tmp/dual_tap.py` (local-only, not in repo)
opens both lamp serial ports and prefixes each line with the lamp name.
Output goes to `/tmp/dual_tap.log`. Useful for diff-checking cascade
sender vs receiver behavior across two physical lamps during firmware
iteration.

## Code conventions

Phase I cleanup hardened these. Don't reintroduce.

### Comments

Default to NO comment. Only write one when the WHY is non-obvious, a
hidden constraint, a subtle invariant, a workaround for a specific bug.

Forbidden patterns:

- Internal vocabulary: `Phase X.Y`, `Audit fix #N`, `audit fix [HIGH]`,
  `Per YYYY-MM-DD code review bug #N`, `PHASE C`, ticket numbers, commit
  SHAs in source comments.
- Calendar-date archaeology: `Pre-2026-06-12 behavior was X`, rationale
  must stand on its own; previous state is irrelevant.
- Tombstones for moved code: `// X was moved to Y.cpp`. Anyone grepping
  finds the symbol where it lives.
- Restatements of what well-named code already says.
- Meta-framing: "this handler still serves a purpose", "(none expected, 
  they share the snapshot)".
- Forward-leaning text: `will add later`, `reserved for`, `TODO when`,
  `future phases can`, `deferred`, `stub`, `placeholder`.

When a wire-format enum or API is removed, sweep ALL comments mentioning
it (lamp + wisp mirror copies of `lamp_protocol.hpp` included).

### No speculative API surface

Don't expose API without a current-day consumer:

- No enum values, capability bits, or struct fields "reserved" for
  hypothetical features.
- No virtual hooks "for future override".
- No template helpers introduced for hypothetical reuse.

When a feature actually ships, the API surface lands in the same commit.

### Variant-include hygiene

Framework code (`core/`, `components/`, `behaviors/`, `config/`,
`expressions/`, `util/`) must NOT include variant headers
(`lamps/standard/*.hpp`, `lamps/snafu/*.hpp`). Variant-specific
constants (pin numbers, max brightness, pixel counts) live in the
variant header; the framework receives them via injected `HwConfig`
POD + `Config::Defaults`.

# Lamp OS

ESP32-based smart-lamp fleet. Three components:

- **`software/lamp-os/`**, lamp firmware (ESP32-WROOM). BLE GATT control
  service for the Flutter app + ESP-NOW mesh for lamp-to-lamp. Build envs
  are per-variant: `upesy_wroom_standard` (default) and `upesy_wroom_snafu`.
  Each signed binary is type-gated via the `{type}-{channel}` slot in the
  LSIG footer, so a cross-variant OTA is silently dropped.
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

The mesh wire format carries a **receive range**, not a single version:
`PROTOCOL_VERSION_EMIT = 0x05` is what we broadcast; `RX_MIN = 0x04` ..
`RX_MAX = 0x05` is what we parse. Splitting emit from receive lets the fleet
*receive* a newer version before it *emits* one — the safe path for a
multi-version OTA wave. MSG_EVENT gossip-relays, DedupRing capacity is 64,
HELLO interval is 5 s. v0x04 widened the FW channel slot to 16 bytes for
per-variant OTA gating (`{type}-{channel}`); v0x05 added a TLV trailer to
HELLO + WISP_HELLO (current TLV: `HELLO_TLV_OTA_STATE`). Unknown TLVs are
silently skipped (forward-compat). Cross-version mismatch fails loud (peers
don't show up).

**TLV-first rule — don't bump the protocol for additive fields.** Anything
expressible as a new TLV type ID on an existing message rides as a TLV:
receivers that don't know it skip it, senders that do can emit it. Reserve a
version bump for genuine parser-contract changes (redefining a field's
semantics, changing fixed offsets, removing a field older receivers still
consume). The fleet runs mixed versions during OTA waves, so an unnecessary
bump splits the mesh and forces a manual re-flash. Every bump's commit
message must state what parser contract changed and why a TLV wasn't enough.

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
  `CHAR_SCHEMA_VERSION` exposes the version on the wire (read-only, single
  byte; absent on legacy lamps that predate it). No app consumer reads it
  yet — that lands with the first feature that gates on the version.
- **Grow append-only** (new characteristic at the tail) or evolve a
  characteristic's *payload* (settings_blob / sections / page-data), neither
  moves an existing handle, so deployed installs keep working.
- Insert / remove / reorder / add-CCCD is a **breaking** change: bump
  `kGattSchemaVersion` and re-pin the hash in `test_ble_gatt_layout` (the
  native test fails until you do), and expect a one-time force-stop on
  already-paired phones. The boot-time assert in `ble_control.cpp` catches
  table-vs-registration drift.

## Build + test

Drive build / flash / test through the root `npm run …` tasks
(`package.json`), not the raw `pio` / `adb` underneath. The scripts are the
contract CI and bench-verify run against; bypassing them silently desyncs
(env names, env vars, port handling). Fix the script, don't shell around it.

```sh
npm run lamp:test          # native unit tests (runs in CI)
npm run lamp:build         # lamp firmware, standard variant (default)
npm run lamp:build:snafu   # snafu variant
npm run wisp:build         # wisp firmware
```

The native suite covers protocol parsers, dedup ring, color math, fade
math, cascade dedup, OTA receiver/indicator, and the GATT layout pin. Keep
it green, currently 374/374.

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

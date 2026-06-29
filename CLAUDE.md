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

**Keep docs current.** Before calling work done, check whether anything under
`docs/dev/` describes the behavior you changed; if so, update it in the same
change. A doc that contradicts shipped code is a bug.

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

## Plugins this workflow assumes

Contributors using Claude Code should have two plugins installed; several
conventions here reference them:

- **ponytail** — laziness / over-engineering review mode plus the `/ponytail-*`
  commands. The `// ponytail:` comment convention and the `ponytail-debt`
  ledger depend on it.
- **superpowers** — the skills library (brainstorming, writing-plans,
  subagent-driven-development, TDD, code review) the dev flow leans on.

## Tail multiple lamps simultaneously

`scripts/bench_tap.py` tails several USB serial ports at once, prefixing
each line with a short label so cross-lamp behavior (cascade OTA, mesh
paint, greet handshakes) reads as one stream. Useful for diff-checking
sender vs receiver across two physical lamps during firmware iteration.

```sh
python3 scripts/bench_tap.py /dev/cu.usbserial-0001:flora \
                             /dev/cu.usbserial-6:gramp -o /tmp/bench.log
```

See the script's `--help` for label/log options. (Supersedes the old
local-only `/tmp/dual_tap.py`.)

## Code conventions

Phase I cleanup hardened these. Don't reintroduce.

### Comments

Default to NO comment. Write one only for a non-obvious WHY: a hidden
constraint, a subtle invariant, a bug workaround. Code that says what it does
in its name gets nothing.

Register: terse, like a senior dev, not a narrator.

- No attention labels: `Note:`, `NB:`, `Why:`, `Defensive:`, `Rationale:`,
  `THREADING:`. State the fact; the label is noise.
- No "we"; state the fact or use the imperative.
- Prefer a period. A colon only before an actual list, never to splice two
  clauses; no em-dashes; a parenthetical only for a genuine aside, never to
  smuggle in an implementation detail.
- No heading or section-divider comments.
- Don't narrate control flow. A comment restating what an `if` / `else` /
  `switch` / loop does is noise; the branch condition already says it.
- No `X not Y` antithesis, no `(is/isn't) load-bearing` framing. State what
  the code does, not what it's being contrasted against.

Length and placement:

- ~2 lines inline. State an invariant ONCE, on the primary `.h` declaration;
  don't restate it in the member comment and again in the `.cpp` body.
- A header comments the *contract* — what the declaration is for, what a
  caller must honor. Never how it works; implementation notes live in the
  `.cpp`, or in `docs/dev/` if longer than a line.
- An algorithm/flow walkthrough, a byte-budget tally, or anything past a few
  lines goes in a `docs/dev/` README with a one-line pointer-comment, not
  inlined. A `.cpp` stays comment-light; no file-top contents block.
- Prefer a self-documenting name over a comment: if a comment exists only to
  say what a literal or flag *means*, make it a named constant or enum. A
  genuine magic-number constant gets at most a one-line "what breaks if it's
  wrong".
- No fixed per-file comment count.

`// TODO:` / `// FIXME:` (concrete, tagged) and `// ponytail:` markers are
allowed: honest tracked debt, not vague forward-leaning prose.

Forbidden patterns:

- Internal vocabulary: `Phase X.Y`, `audit fix #N`, ticket numbers, commit SHAs.
- History narration: how the code used to behave or how a value was tuned —
  `previously`, `no longer`, `originally N, bumped to M`, `grew from X to Y
  after bench`, `since/as of <date>`, comparisons to a superseded design.
  Git holds the past.
- Hedge-as-content: a comment whose substance is `defensive` / `shouldn't
  happen` / `just in case` / `for now`. State the guard's real reason or cut it.
- Tombstones for moved code; restatements of what well-named code already says.
- Commentary on debug scaffolding (`LAMP_DEBUG` guards, log lines). The log
  string is the message; don't annotate it.
- Forward-leaning prose with no actionable tag: `will add later`,
  `reserved for`, `future phases can`, `deferred`, `stub`, `placeholder`.
- Provenance breadcrumbs: `Ported from legacy X`, `Mirrors the wisp's X`.
  State the shared constraint, not the lineage.

References: point only to authoritative in-repo docs (`docs/dev/…`), and the
target must resolve. No planning docs, review rounds, labeled work-units
(`Commit E`, `plan §5`, `cq-H`), porting briefs, or machine-local paths
(`/Users/…`, `~/…`).

When a wire-format enum or API is removed, sweep ALL comments mentioning it
(lamp + wisp mirror copies of `lamp_protocol.hpp` included).

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

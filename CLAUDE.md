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
  Form spacing + grouping convention: [`software/lamp-app-flutter/docs/FORM_STYLING.md`](software/lamp-app-flutter/docs/FORM_STYLING.md).

## Authoritative references

Read these before changing networking, protocol, or BLE behavior:

- **[`docs/dev/networking.md`](docs/dev/networking.md)**, wire-format spec for every
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
multi-version OTA wave. MSG_EVENT is nearby-scoped (no relay), DedupRing capacity is 64,
HELLO interval is 5 s. v0x04 widened the FW channel slot to 16 bytes for
per-variant OTA gating (`{type}-{channel}`); v0x05 added a TLV trailer to
HELLO + WISP_HELLO (TLVs: `HELLO_TLV_OTA_STATE`, `HELLO_TLV_FW_CHANNEL`,
`HELLO_TLV_FS_STATE`). Unknown TLVs are
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
  `components/network/ble/gatt_layout.hpp` are the single source of truth.
  `CHAR_SCHEMA_VERSION` exposes the version on the wire (read-only, single
  byte; absent on legacy lamps that predate it). `kGattSchemaVersion` is
  currently **4**; `CHAR_WISP_CLAIMS` (`5f64f4eb-…`) is the v4 tail. No app
  consumer gates on the version yet — that lands with the first feature that
  requires it.
- **Grow append-only** (new characteristic at the tail) or evolve a
  characteristic's *payload* (settings_blob / sections / page-data), neither
  moves an existing handle, so deployed installs keep working.
- Insert / remove / reorder / add-CCCD is a **breaking** change: bump
  `kGattSchemaVersion` and re-pin the hash in `test_ble_gatt_layout` (the
  native test fails until you do), and expect a one-time force-stop on
  already-paired phones. The boot-time assert in `ble_control.cpp` catches
  table-vs-registration drift.

## Worktrees and branch safety

This repo uses multiple git worktrees under `.claude/worktrees/`, each on its
own branch (e.g. the `net-hardening` worktree is on `snafu-cleanup`), separate
from the main checkout at the repo root (usually on `dev`). **Before any build,
flash, install, or commit, confirm both the worktree AND the branch** —
`git rev-parse --show-toplevel` and `git rev-parse --abbrev-ref HEAD`. A stray
`cd` to the repo root (or an agent that starts there) silently builds, edits,
or commits against the WRONG branch: changes land where no one is looking,
codegen dirties the wrong tree, and reviews read stale code. This has bitten us
for real. **Any agent dispatched to work in a worktree MUST be given the
absolute worktree path and told NOT to `cd` to the repo root.**

## Build + test

Drive build / flash / test through the root `npm run …` tasks
(`package.json`), not the raw `pio` / `adb` underneath. The scripts are the
contract CI and bench-verify run against; bypassing them silently desyncs
(env names, env vars, port handling). Fix the script, don't shell around it.

```sh
npm run lamp:test          # native unit tests (runs in CI)
npm run lamp:build         # lamp firmware, unsigned (no key needed)
VARIANT=snafu npm run lamp:build   # any other variant, still unsigned
npm run wisp:build         # wisp firmware
```

Local builds are unsigned by default (`LAMP_FIRMWARE_SKIP_SIGN=1`). Unsigned
binaries boot and mesh normally; they cannot be distributed over ESP-NOW OTA
(no LSIG footer). Use `:signed` variants when you have the signing key and need
an OTA-distributable binary.

`lamp:flash` and `lamp:build` take three optional env params: `VARIANT`
(default `standard`), `CHANNEL` (flash defaults to `beta`), and `PORT` (flash
only, passed to `--upload-port` for when more than one board is attached).
`VARIANT=snafu CHANNEL=stable PORT=/dev/cu.usbserial-6 npm run lamp:flash`.

`lamp:build:signed` / `lamp:flash:signed` build and sign locally. Requires the
signing key at `~/.lamp-os-firmware-key.bin`. Accept the same `VARIANT`,
`CHANNEL`, and `PORT` env params.

`lamp:flash:release` downloads a prebuilt signed binary from the GitHub `beta`
release (via `gh`) and flashes it over USB with `esptool`. No build, no key
needed. `VARIANT` (default `standard`), `RELEASE_TAG` (default `beta`), and
`PORT` env params apply. Beta releases are published manually
(`release-beta.yml` is `workflow_dispatch`), so the downloaded build is only as
current as the last manual run.

The native suite covers protocol parsers, dedup ring, color math, fade
math, cascade dedup, OTA receiver/indicator, and the GATT layout pin. Keep
it green.

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

This is the *speculative-generality* smell, and like the others it's a
heuristic, not a law — an interface whose second implementation is a test fake,
or one that inverts a dependency on a volatile boundary (NVS, network, clock),
earns its keep today and isn't speculative. See
[`docs/dev/code-smells.md`](docs/dev/code-smells.md) for the catalog and the
"when it's actually fine" cases; judge against it, don't wield it.

### Naming and C++ style

- **No Hungarian notation.** Type things for the compiler; name them for
  humans. No type-encoding prefixes (`szName`, `pBuf`, `dwCount`, `nLen`).
  Scope markers are not Hungarian and stay: trailing `_` on members
  (`store_`, `entries_`), `s_` / `g_` for file-statics / globals.
- **Reuse before you build.** Reach for an existing type or helper before a
  new one (`Color` for an RGBW value, the existing DedupRing, etc.), and
  check the stdlib / framework before hand-rolling. Reinventing what already
  lives here is the most common slop — see
  [`docs/dev/code-smells.md`](docs/dev/code-smells.md).
- **Definitions go in the `.cpp`.** Declarations in the header, definitions in
  the `.cpp` by default. Header-only needs a reason: templates, `constexpr`, a
  one-line accessor, or a native-test seam (a test that `#include`s the
  `.cpp`). A header full of function bodies is the smell.
- **Prefer modern C++ over C idioms.** `{}`-init / `std::array` / `std::fill`
  over `memset`; a class or `std::` container over a hand-rolled struct + loose
  functions. Exception: a genuine raw byte buffer or a tight embedded hot path
  where the C idiom is measurably clearer or faster — then say so in a comment.

These are defaults to judge against, not absolutes (e.g. wire-format
`static_assert`s on fixed offsets are a feature, not over-assertion; keep
them). When a default doesn't fit, the reason goes in the diff or a comment.

### Variant-include hygiene

Framework code (`core/`, `components/`, `behaviors/`, `config/`,
`expressions/`, `util/`) must NOT include variant headers
(`lamps/standard/*.hpp`, `lamps/snafu/*.hpp`). Variant-specific
constants (pin numbers, max brightness, pixel counts) live in the
variant header; the framework receives them via injected `HwConfig`
POD + `Config::Defaults`.

# Network Folder Reorg — Implementation Plan

> **For agentic workers:** mechanical reorganization of `software/lamp-os/src/components/network/`. Pure moves/renames/splits — ZERO behavior, wire-format, or GATT-handle change. Each task is gated by both-variant builds + the native test suite. Steps use `- [ ]` checkboxes.

**Goal:** Untangle the flat 17-file network folder into four concern-subfolders (`protocol/`, `transport/`, `mesh/`, `ble/`), splitting the two monoliths and the `nearby_lamps` tangle — without changing a single wire byte or GATT handle.

**Architecture:** Splits first (each file divided, re-aggregated under an unchanged umbrella so no `#include` consumer changes), then the subfolder moves as one mechanical sweep. The wire format (`lamp_protocol.hpp`, mirrored in `software/wisp/`) and the GATT layout (`gatt_layout.hpp` + `ble_control.cpp` registration order) are **frozen lock-ins**.

## Global Constraints (every task)

- **Live 22-lamp fleet. Moves/renames/splits ONLY.** Never reorder a struct field, byte offset, `static_assert`, enum value, message size, or GATT characteristic. No logic edits.
- **`lamp_protocol.hpp` split:** the umbrella header's aggregate must be **byte-identical** to the pre-split header (every constant/struct/function present, same order of *definition* where it matters for the `static_assert` locks). The **wisp mirror at `software/wisp/src/lamp_protocol.hpp` is split in lockstep** to the identical structure.
- **`ble_control.cpp`:** the `createCharacteristic()` registration block stays **contiguous and unreordered** (the boot assert + `test_ble_gatt_layout` hash guard it). This plan does NOT split `ble_control.cpp` (deferred).
- **Per-task gate (all must pass before commit):** `npm run lamp:build` (standard) → SUCCESS, `npm run lamp:build:snafu` (snafu) → SUCCESS, `npm run lamp:test` (native). **Known-pre-existing baseline:** `test_fs_signature::test_digest_golden_and_order_independent` fails (unrelated FS-OTA P2a work) — only a NEW failure beyond that blocks. Run from the worktree root.
- Commit per task. Sweep comments to match new names/paths (repo comment-hygiene rule).

> Paths relative to `software/lamp-os/src/components/network/` unless noted. Done already (committed prereqs): the iOS page-fix, comment/doc-drift cleanup, and the `show_receiver`→`mesh_link` rename.

---

### Task 1: Split `wisp_cache` out of `nearby_lamps`

**Files:** Create `wisp_cache.cpp` / `wisp_cache.hpp`; Modify `nearby_lamps.cpp` / `.hpp` + every includer that uses the wisp-cache API.

`nearby_lamps` owns three things; extract the wisp one. Move `WispCache` (the hello/status/palette cache + its mac-mismatch invalidation) and `getWispStatusReadJson()` (base64 + the `CHAR_WISP_STATUS` JSON builder) into `wisp_cache.*`. The lamp **roster** (BLE-adv + ESP-NOW merge, prune, RSSI sort) stays in `nearby_lamps`.

- [ ] **Step 1:** Read `nearby_lamps.cpp/.hpp`; identify the exact `WispCache` class + `getWispStatusReadJson` + any helpers used only by them. `grep -rn 'WispCache\|getWispStatusReadJson\|wispCache' software/lamp-os/src` to find all consumers (likely `ble_control.cpp` for the JSON, `mesh_link.cpp` for cache writes).
- [ ] **Step 2:** Create `wisp_cache.hpp` (the class decl + `getWispStatusReadJson` signature) and `wisp_cache.cpp` (definitions). Move verbatim — no logic change. Keep the same namespace.
- [ ] **Step 3:** In `nearby_lamps.*`, remove the moved code; add `#include "wisp_cache.hpp"` only if `nearby_lamps` itself still references it (it may not). Update consumers' includes to `wisp_cache.hpp`.
- [ ] **Step 4: Gate** — `npm run lamp:build && npm run lamp:build:snafu && npm run lamp:test` (no new failures vs baseline).
- [ ] **Step 5: Commit** — `refactor(net): split wisp_cache out of nearby_lamps`.

---

### Task 2: Extract `pending_slots.hpp` from `mesh_link.hpp`

**Files:** Create `pending_slots.hpp`; Modify `mesh_link.hpp` / `.cpp` + includers.

`mesh_link.hpp` defines the app-level `Pending*` DTO structs (the Core0→Core1 hand-off payloads) inline, which forces every includer of the mesh transport to also pull `config`/`expressions`/`firmware_receiver` headers. Move the `Pending*` struct definitions into `pending_slots.hpp`.

- [ ] **Step 1:** Identify the `Pending*` structs in `mesh_link.hpp` (e.g. `PendingEvent`, `PendingControlOp`, override/palette pendings) and the `postPending*` forwarder decls. `grep -rn 'Pending' software/lamp-os/src/components/network software/lamp-os/src/core` for consumers.
- [ ] **Step 2:** Create `pending_slots.hpp` with the struct defs (carry whatever minimal includes the structs themselves need — `Color`, etc.). `mesh_link.hpp` includes `pending_slots.hpp`.
- [ ] **Step 3:** Point consumers that only need the DTOs at `pending_slots.hpp` instead of the whole `mesh_link.hpp` where it reduces coupling (don't force it where they need the transport too).
- [ ] **Step 4: Gate** (both builds + native tests, no new failures).
- [ ] **Step 5: Commit** — `refactor(net): extract pending_slots DTOs from mesh_link header`.

---

### Task 3: Split `lamp_protocol.hpp` into per-family headers under an umbrella  ⚠️ FROZEN — highest care

**Files:** Create `protocol/header.hpp`, `presence.hpp`, `control_op.hpp`, `wisp.hpp`, `override.hpp`, `event.hpp`, `fw.hpp`, `fs.hpp`, `dedup_ring.hpp`; turn `lamp_protocol.hpp` into an umbrella that `#include`s them in order. **Mirror the identical split in `software/wisp/src/`.**

> This is the one task that touches a frozen lock-in. Do it as its own commit, verify byte-identity, and split the wisp mirror in the SAME commit so the two never diverge.

- [ ] **Step 1:** Map the section boundaries in `lamp_protocol.hpp` — magic/version/`MsgType`/`HEADER_SIZE` → `header.hpp`; each family's constants+offsets+`static_assert`s+`Parsed*`+build/parse/inspect → its family header; the `DedupRing` runtime class → `dedup_ring.hpp`. Record the exact line ranges.
- [ ] **Step 2:** Move each section **verbatim** into its new header (no edits to any constant/offset/struct/assert). Preserve definition order where a `static_assert` depends on a prior constant — keep dependent pieces together or ordered in the umbrella's include sequence so the aggregate compiles identically.
- [ ] **Step 3:** Make `lamp_protocol.hpp` an umbrella: just `#include "protocol/header.hpp"` … `#include "protocol/dedup_ring.hpp"` in dependency order. Consumers keep `#include ".../lamp_protocol.hpp"` unchanged.
- [ ] **Step 4:** Apply the **identical** split to `software/wisp/src/lamp_protocol.hpp` (it omits the FW/FS families by design — split only what it contains, same header names for the shared families).
- [ ] **Step 5: Byte-identity check** — confirm the preprocessed aggregate is unchanged: `cd software/lamp-os && g++ -E -I src src/components/network/lamp_protocol.hpp` (or the native test's include path) before vs after produces the same token stream for the wire structs; at minimum, `test_protocol_fw` / `test_protocol_fs` / `test_dedup_ring` / `test_msg_event` must pass unchanged.
- [ ] **Step 6: Gate** — both builds + native tests; **specifically confirm** `test_ble_gatt_layout`, `test_protocol_fw`, `test_protocol_fs`, `test_dedup_ring`, `test_cascade_dedup` green.
- [ ] **Step 7: Commit** — `refactor(protocol): split lamp_protocol into per-family headers under umbrella (firmware + wisp mirror, byte-identical)`.

---

### Task 4: Tier-2 — move files into `protocol/ transport/ mesh/ ble/` subfolders

**Files:** `mkdir` the four subdirs; `git mv` each file in; update every `#include` path across `software/lamp-os/src` + `test/`.

Pure relocation. Target placement:
- `protocol/` — the umbrella + per-family headers (from Task 3) already live here.
- `transport/` — `espnow_link.*`, `wifi.*`. (`ble_gap` carve-out from `bluetooth.*` is **deferred/optional** — leave `bluetooth.*` where it is for now, or move it whole to `transport/`.)
- `mesh/` — `mesh_link.*`, `pending_slots.hpp`, `nearby_lamps.*`, `wisp_cache.*`.
- `ble/` — `ble_control.*`, `write_router.hpp`, `gatt_layout.hpp`, `crypto.*`, `bluetooth.*`.

- [ ] **Step 1:** `mkdir` the four subfolders; `git mv` files per the placement above (do it in include-dependency-friendly order, but the build is the check).
- [ ] **Step 2:** Update every `#include "...network/<file>"` path repo-wide (`grep -rln 'components/network/' software/lamp-os/src software/lamp-os/test` → fix paths). Watch `platformio.ini` — confirm it globs `src/**` (no explicit file list to update).
- [ ] **Step 3: Gate** — both builds + native tests (this is where a missed include path surfaces).
- [ ] **Step 4: Commit** — `refactor(net): organize network/ into protocol|transport|mesh|ble subfolders`.

---

## Deferred (NOT in this plan — separate call, most blast radius)
- Split `ble_control.cpp` (1407) into callbacks + JSON builders — touches the GATT-adjacent grab-bag; defer.
- Carve `ble_gap.*` out of `bluetooth.cpp` to break the `bluetooth↔ble_control` mutual include.

## Self-Review
- Coverage: the assessment's Tier-1 (wisp_cache, pending_slots, lamp_protocol split) → Tasks 1-3; Tier-2 (subfolders) → Task 4; the frozen-risk items each carry explicit byte-identity + lockstep-mirror + registration-order constraints. The `fs_signature` pre-existing failure is pinned as the baseline so it can't be mistaken for a regression.

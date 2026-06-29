# Lamp Identity — Design

**Status:** approved design, pre-plan
**Date:** 2026-06-28

## Goal

Give the app one lamp identity that works on iOS and Android and covers mesh
lamps and pre-existing (main-branch) non-mesh lamps, then point the wisp
painted-lamps list at it. Replace the Android-only `bdAddr − 2`-from-device-id
derivation currently on `wisp-revive`.

## Context

The wisp claimed-lamp filter (on `wisp-revive`) matches inventory lamps against
the wisp's claimed set by deriving a mesh MAC from the phone's BLE device id
(`meshMacFromBleId`, BLE − 2). That works only on Android (where the device id
IS the BLE MAC) and breaks on iOS (opaque per-install UUID). It also derives
identity from the wrong source — the phone — when the lamp already reports it.

The app already has a working universal lamp identity it can reuse:

- **`bdAddr`** (colon-hex BLE address) is the social/disposition key on both
  sides: app `dispositionsProvider.get/set(String bdAddr)`; firmware
  `config_->getDisposition(p.bdAddr)`, `greetingFor(peerBdAddr)`.
- The connected lamp **reports** every peer's `bdAddr` in its `nearby`
  page-protocol section (`LampNearbyPeer.bdAddr`, `CHAR_NEARBY_LAMPS`). Because
  it is lamp-reported (not phone-derived), it already works on iOS and already
  covers lamps the user has not added.
- ESP32 lamps use a static public BLE address, so `bdAddr` is stable and
  unique.

## Two lamp populations (resolved)

- **Inventory lamps** — explicitly added via onboarding (`AddLampNotifier` →
  `inventory_notifier`). The user's own lamps; persisted with name + password.
- **Seen / nearby peers** — `LampNearbyPeer`, read from the connected lamp's
  `nearby` JSON; the lamp's vantage on its mesh/BLE neighbours. Not added, not
  minted, but **first-class** for social dispositions and the wisp list.

Both populations key on `bdAddr`. The wisp page is about the second: lamps the
wisp can see on the mesh, regardless of inventory membership.

## Identity model

- **The identity is `bdAddr`.** No newly-minted internal id. It is already the
  social key, it is lamp-reported (cross-platform, covers seen lamps), and it
  is control-method-agnostic — a non-mesh lamp seen over BLE still has a
  `bdAddr`, so the model extends to ArtNet-controlled lamps later with no
  change to the identity itself.
- **Legacy fallback (inventory only):** a lamp too old to emit `bdAddr` (empty
  `bdAddr`) cannot be identified in the mesh/social/wisp world — already true
  and accepted (social shows it but cannot disposition it). For inventory
  *persistence* of such a lamp across an iOS UUID rotation, the existing
  `reconcileIdByIdentity` (name + last base/shade colours) stays as the
  fallback. No new mechanism.

## Iteration 1 — scope (this build)

All claimed mesh lamps show on the wisp page, identified by `bdAddr`.

1. **`CHAR_WISP_CLAIMS` emits `bdAddr`, not mesh MAC.** The lamp serving the
   characteristic converts each claimed mesh MAC to its `bdAddr` (`mesh + 2`,
   the ESP32 relationship) before emitting. The app then matches
   `bdAddr`-to-`bdAddr` with no conversion. (Blob stays `[count][addr*6]`,
   same size budget.)
2. **Wisp page membership IS the claimed set; names are resolved separately.**
   The list is every claimed `bdAddr` (so all claimed lamps show). Each one's
   `name` is looked up by `bdAddr` in the connected lamp's `nearby` peers, then
   in inventory; a claim resolvable in neither shows with a short `bdAddr`
   label rather than being dropped. Inventory is cross-referenced by `bdAddr`
   only to label "your" lamps, and is no longer the source of the list.
3. **Inventory learns its own `bdAddr`.** On connect, record the lamp's
   `bdAddr` into its inventory entry. On Android it already equals the device
   id (no-op); on iOS the lamp self-reports it. (Plan must verify whether a
   lamp already exposes its *own* `bdAddr` — it reports peers' today — or
   whether a self-identity read is added.)
4. **Delete the `−2`-from-device-id hack** (`meshMacFromBleId` usage in the
   wisp screen) — superseded by lamp-reported `bdAddr`.

## Components

Firmware (`software/lamp-os/src/`):
- `nearby_lamps.cpp` `buildWispClaimsBlob` — emit `bdAddr` (`mesh + 2`) per
  claimed lamp instead of the mesh MAC.
- Self-`bdAddr` exposure — verify it exists; add a read if not (the lamp knows
  its own `bdAddr`).

App (`software/lamp-app-flutter/lib/`):
- `features/wisp/` — point `_PaintedLampsList` at the nearby-peer report
  intersected with claims (by `bdAddr`); drop `meshMacFromBleId`.
- `features/inventory/` — store `bdAddr` on the inventory entry, populated on
  connect.
- `domain/wisp_claims.dart` / `tuple_sampler.dart` — the claimed set is now
  `bdAddr`s; remove the mesh-MAC derivation.

## Data flow (iteration 1)

1. Wisp broadcasts `MSG_WISP_CLAIM` (mesh MACs of claimed lamps).
2. Connected lamp caches it; serves `CHAR_WISP_CLAIMS` as `bdAddr`s.
3. Connected lamp also serves `CHAR_NEARBY_LAMPS` (`name` + `bdAddr` per peer).
4. App reads both; for each claimed `bdAddr`, finds its name in the nearby
   peers (or inventory), and renders the list. The colour preview keys on the
   same `bdAddr` (mesh MAC = `bdAddr − 2`) so it stays byte-accurate.

## Error handling / edge cases

- **Claimed `bdAddr` not in the nearby report:** the connected lamp can't name
  it (different vantage than the wisp). Show it with a short `bdAddr` label
  rather than dropping it, so "all claimed lamps show."
- **Legacy lamp, empty `bdAddr`:** cannot appear keyed by identity; out of the
  mesh/social/wisp world by nature (accepted).
- **iOS:** all identity is lamp-reported, so the wisp page and social work; the
  only residual gap is inventory *persistence* of a directly-added legacy lamp
  on iOS, handled by the name+colours reconcile.
- **Best-effort claims read** (timeout, missing characteristic on a legacy
  lamp) keeps its current graceful fallback: unknown claims → show all, never
  block.

## Testing

- Unit: `CHAR_WISP_CLAIMS` blob builder emits `mesh + 2` `bdAddr`s; native test.
- Unit (app): claimed-set parse + the nearby-peers ∩ claims intersection by
  `bdAddr`; the inventory `bdAddr` capture.
- Hardware: flash a lamp (standard + beta), wisp claiming ≥1 lamp, confirm the
  wisp page lists the claimed lamps by name on both an Android device and (if
  available) iOS.

## Future direction (captured, NOT built)

The wisp already runs ArtNet (`ArtnetEmitter`) and main-branch non-mesh lamps
respond to ArtNet, so the wisp can in principle see and control non-mesh lamps
too. Because `bdAddr` is control-method-agnostic, the identity model already
covers them. The roadmap, each its own iteration:

1. Show a **separate list of nearby legacy (non-mesh, BLE-visible) lamps** the
   wisp could control via ArtNet.
2. **Address** them (map `bdAddr` → ArtNet universe/channel).
3. **Control** them over ArtNet.
4. **Show their assigned colours** in the app.

To keep this an addition rather than a rewrite, the wisp page's lamp model
should carry `bdAddr` + `name` + a reachability/control-method field
(mesh now; artnet later), even though only the mesh source is populated in
iteration 1.

## Out of scope (this iteration)

- ArtNet discovery, addressing, or control of non-mesh lamps.
- Any new minted internal id.
- Changes to the social/dispositions feature (already `bdAddr`-keyed).

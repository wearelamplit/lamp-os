# Lamp Identity (iteration 1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The wisp page shows all the lamps the wisp claims, by name, identified via `bdAddr` so it works on iOS and Android — replacing the Android-only `bdAddr − 2`-from-phone-device-id derivation.

**Architecture:** `CHAR_WISP_CLAIMS` emits `bdAddr`s (the lamp converts `mesh + 2`). The wisp page's list membership is the claimed `bdAddr` set; each name is resolved from the connected lamp's already-reported `nearby` peers (`bdAddr` + `name`), which are lamp-reported and therefore cross-platform. The per-lamp colour preview keys on the mesh MAC = `bdAddr − 2`. No inventory involvement.

**Tech Stack:** C++17 / PlatformIO (lamp firmware), Flutter / Dart / Riverpod (app), Unity (native test), `flutter test`.

## Global Constraints

- Source of truth design: `docs/superpowers/specs/2026-06-28-lamp-identity-design.md`.
- Identity is `bdAddr` (colon-hex BLE address); no new minted id.
- `bdAddr` of a claimed lamp = its mesh MAC `+ 2` (ESP32: BLE = STA base + 2), full 48-bit with carry.
- Run builds/tests via `npm run …` (`lamp:build`, `lamp:test`, `app:test`), never raw pio/flutter where a script exists.
- GATT layout is frozen: this plan does NOT add/reorder a characteristic. `CHAR_WISP_CLAIMS` already exists; only its *payload contents* change (mesh MAC → bdAddr), which is payload evolution, not a layout change. No `kGattSchemaVersion` bump.
- `CHAR_WISP_CLAIMS` blob format is unchanged: `[count:1][addr:6]*count`; only the 6-byte values change meaning from mesh MAC to bdAddr.
- The claims read stays best-effort (timeout, null-on-failure → show all) — do not regress that.
- `lamp:test` has ONE known pre-existing failure, `test_fs_signature` (dev's fs-ota merge, unrelated). Treat the suite as green if that is the only failure.
- Branch: work continues on `wisp-revive` (this supersedes the `−2` hack added there).

---

### Task 1: `CHAR_WISP_CLAIMS` emits `bdAddr`, not mesh MAC

**Files:**
- Modify: `software/lamp-os/src/components/network/nearby_lamps.cpp` (`buildWispClaimsBlob`)
- Modify: `software/lamp-os/src/components/network/nearby_lamps.hpp` (declare the helper if exposed for test)
- Test: `software/lamp-os/test/test_wisp_claims_blob/wisp_claims_blob.cpp` (new)

**Interfaces:**
- Produces: a free function `void bdAddrFromMeshMac(const uint8_t mesh[6], uint8_t outBdAddr[6])` — adds 2 to the 6-byte big-endian value with carry. Used by `buildWispClaimsBlob`; native-testable.
- `buildWispClaimsBlob` signature is unchanged (`size_t (uint8_t* out, size_t outCap, uint32_t nowMs)`); only the bytes it writes change.

- [ ] **Step 1: Write the failing test**

Create `software/lamp-os/test/test_wisp_claims_blob/wisp_claims_blob.cpp`:

```cpp
#include <unity.h>
#include <cstdint>
#include "components/network/wisp_claims_addr.hpp"

void setUp() {}
void tearDown() {}

void test_adds_two_simple() {
  const uint8_t mesh[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA4};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xFC, 0xB4, 0x67, 0xF1, 0xDD, 0xA6};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

void test_carries_across_octets() {
  const uint8_t mesh[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  uint8_t out[6];
  ble_control::bdAddrFromMeshMac(mesh, out);
  const uint8_t want[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEF, 0x01};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, out, 6);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_adds_two_simple);
  RUN_TEST(test_carries_across_octets);
  return UNITY_END();
}
```

- [ ] **Step 2: Run to verify it fails (header missing)**

Run: `npm run lamp:test`
Expected: FAIL/ERROR — `wisp_claims_addr.hpp` not found.

- [ ] **Step 3: Create the header-only helper**

Create `software/lamp-os/src/components/network/wisp_claims_addr.hpp`:

```cpp
#pragma once
#include <cstdint>

namespace ble_control {

// A claimed lamp's BLE address is its mesh (WiFi-STA) MAC + 2 on ESP32.
// 6-byte big-endian add-2 with carry.
inline void bdAddrFromMeshMac(const uint8_t mesh[6], uint8_t outBdAddr[6]) {
  uint16_t carry = 2;
  for (int i = 5; i >= 0; --i) {
    const uint16_t v = static_cast<uint16_t>(mesh[i]) + carry;
    outBdAddr[i] = static_cast<uint8_t>(v & 0xFF);
    carry = v >> 8;
  }
}

}  // namespace ble_control
```

- [ ] **Step 4: Run to verify the test passes**

Run: `npm run lamp:test`
Expected: PASS for `test_wisp_claims_blob` (the only other failure permitted is the pre-existing `test_fs_signature`).

- [ ] **Step 5: Use the helper in `buildWispClaimsBlob`**

In `software/lamp-os/src/components/network/nearby_lamps.cpp`, add `#include "components/network/wisp_claims_addr.hpp"` near the top, then replace the bulk `memcpy` in `buildWispClaimsBlob` with a per-entry conversion:

```cpp
  out[0] = count;
  for (uint8_t i = 0; i < count; ++i) {
    bdAddrFromMeshMac(snap.claimedLampMacs[i], out + 1 + static_cast<size_t>(i) * 6);
  }
  return needed;
```

(Delete the old `if (count > 0) { std::memcpy(out + 1, snap.claimedLampMacs, count*6); }`.)

- [ ] **Step 6: Build the firmware**

Run: `npm run lamp:build`
Expected: SUCCESS.

- [ ] **Step 7: Commit**

```bash
git add software/lamp-os/src/components/network/wisp_claims_addr.hpp software/lamp-os/src/components/network/nearby_lamps.cpp software/lamp-os/test/test_wisp_claims_blob
git commit -m "feat(lamp): CHAR_WISP_CLAIMS emits bdAddr (mesh + 2), not mesh MAC

The app keys lamp identity on bdAddr (the social/disposition key); emit the
claimed lamps as bdAddrs so the app matches bdAddr-to-bdAddr with no phone-side
MAC derivation. Same blob layout; only the 6-byte values change meaning.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Wisp page shows claimed lamps by name via `bdAddr`

**Files:**
- Modify: `software/lamp-app-flutter/lib/features/wisp/presentation/wisp_config_screen.dart` (`_PaintedLampsList`, `_PaintedLampRow`)
- Modify: `software/lamp-app-flutter/lib/features/wisp/domain/wisp_claims.dart` (doc only — the parser already yields colon-hex strings, now bdAddrs)
- Test: `software/lamp-app-flutter/test/features/wisp/painted_lamps_match_test.dart` (new — the pure matching/resolve helper)

**Interfaces:**
- Consumes: `lampNearbyPeersNotifierProvider(lampId)` → `AsyncValue<List<LampNearbyPeer>>`, each peer has `String bdAddr`, `String name`. `WispNotifier.claimedMacs` → `Set<String>?` (now a set of bdAddr strings; null = unknown → show all). `parseClaimedMacs(List<int>)` (unchanged — parses `[count][addr*6]` to colon-hex strings). `meshMacFromBleId(String)` from `tuple_sampler.dart` (reused: `bdAddr − 2` = mesh MAC for the colour preview). `predictTuple({mac, palette})`.
- Produces: a pure top-level helper `List<PaintedLampEntry> resolvePaintedLamps({required Set<String>? claimed, required List<LampNearbyPeer> peers})` and a small value type `PaintedLampEntry({required String bdAddr, required String name})`, both in `wisp_config_screen.dart` (or a sibling `painted_lamps.dart` if the screen file is already large — check first).

- [ ] **Step 1: Write the failing test**

Create `software/lamp-app-flutter/test/features/wisp/painted_lamps_match_test.dart`:

```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/social/domain/lamp_nearby_peer.dart';
import 'package:lamp_app/features/wisp/presentation/wisp_config_screen.dart'
    show resolvePaintedLamps, PaintedLampEntry;

void main() {
  LampNearbyPeer peer(String bd, String name) =>
      LampNearbyPeer(name: name, bdAddr: bd);

  test('null claimed -> empty (show-all handled by caller, not here)', () {
    expect(resolvePaintedLamps(claimed: null, peers: const []), isEmpty);
  });

  test('every claimed bdAddr appears; name from peers when present', () {
    final peers = [peer('FC:B4:67:F1:DD:A6', 'grady')];
    final out = resolvePaintedLamps(
      claimed: {'FC:B4:67:F1:DD:A6', 'AA:BB:CC:DD:EE:02'},
      peers: peers,
    );
    expect(out.length, 2);
    expect(out.firstWhere((e) => e.bdAddr == 'FC:B4:67:F1:DD:A6').name, 'grady');
    // unresolved claim still shows, labeled by a short bdAddr tail
    expect(out.firstWhere((e) => e.bdAddr == 'AA:BB:CC:DD:EE:02').name,
        contains('EE:02'));
  });
}
```

> Note: `resolvePaintedLamps` returns entries for ALL claimed bdAddrs (membership = claims). The `null` (unknown) case returns empty here; the WIDGET turns null into "show all peers" — that branch is widget-level, not in this pure helper.

- [ ] **Step 2: Run to verify it fails**

Run: `cd software/lamp-app-flutter && flutter test test/features/wisp/painted_lamps_match_test.dart`
Expected: FAIL — `resolvePaintedLamps` / `PaintedLampEntry` undefined.

- [ ] **Step 3: Add the pure helper + value type**

In `software/lamp-app-flutter/lib/features/wisp/presentation/wisp_config_screen.dart`, add the import for `LampNearbyPeer` (`import '../../social/domain/lamp_nearby_peer.dart';`) and these top-level declarations:

```dart
class PaintedLampEntry {
  const PaintedLampEntry({required this.bdAddr, required this.name});
  final String bdAddr;
  final String name;
}

// Membership is the claimed bdAddr set (all claimed lamps show). Name is
// resolved from the connected lamp's nearby peers; an unresolved claim shows
// with its last two bdAddr octets so it is never dropped.
List<PaintedLampEntry> resolvePaintedLamps({
  required Set<String>? claimed,
  required List<LampNearbyPeer> peers,
}) {
  if (claimed == null) return const [];
  final byBd = {for (final p in peers) p.bdAddr.toUpperCase(): p.name};
  return [
    for (final bd in claimed)
      PaintedLampEntry(
        bdAddr: bd,
        name: byBd[bd.toUpperCase()] ??
            'Lamp ${bd.length >= 5 ? bd.substring(bd.length - 5) : bd}',
      ),
  ];
}
```

- [ ] **Step 4: Run to verify the helper test passes**

Run: `cd software/lamp-app-flutter && flutter test test/features/wisp/painted_lamps_match_test.dart`
Expected: PASS.

- [ ] **Step 5: Rewire `_PaintedLampsList` to use it**

In `_PaintedLampsList.build`, replace the inventory-based body. Watch the nearby peers and the claimed set, and render from `resolvePaintedLamps`. Replace the existing `inventoryAsync.when(...)` block with:

```dart
    final claimedMacs = notifier.claimedMacs; // Set<String>? of bdAddrs
    final peersAsync = ref.watch(lampNearbyPeersNotifierProvider(lampId));
    final peers = peersAsync.value ?? const <LampNearbyPeer>[];

    // claimedMacs == null: claims unavailable (legacy lamp / timeout) -> show
    // every nearby peer rather than blocking or hiding.
    final entries = claimedMacs == null
        ? [for (final p in peers) PaintedLampEntry(bdAddr: p.bdAddr, name: p.name)]
        : resolvePaintedLamps(claimed: claimedMacs, peers: peers);

    if (entries.isEmpty) {
      return const Padding(
        padding: EdgeInsets.symmetric(vertical: 12),
        child: Text('No lamps claimed by this wisp right now.',
            style: TextStyle(color: BrandColors.fogGrey, fontSize: 12)),
      );
    }
    return Column(
      crossAxisAlignment: CrossAxisAlignment.stretch,
      children: [
        // (keep the existing header subtitle Padding here)
        for (final e in entries)
          _PaintedLampRow(bdAddr: e.bdAddr, name: e.name, palette: palette),
      ],
    );
```

Add the import `import '../../social/application/lamp_nearby_peers_notifier.dart';`. Remove the now-unused `inventoryNotifierProvider` watch in this widget and the `meshMacFromBleId(l.id)` inventory match.

- [ ] **Step 6: Update `_PaintedLampRow` to take `bdAddr` + `name`**

Change `_PaintedLampRow` to accept `{required String bdAddr, required String name, required List<LampColor> palette}` instead of an `InventoryLamp`. Its colour preview now derives the mesh MAC from the bdAddr:

```dart
    final mac = meshMacFromBleId(bdAddr); // bdAddr - 2 = mesh MAC
    final prediction = (mac == null || palette.isEmpty)
        ? null
        : predictTuple(mac: mac, palette: palette);
```

Render `name` as the row label (drop the inventory-name lookup). Keep the rest of the row UI.

- [ ] **Step 7: Run analyze + the wisp tests**

Run: `cd software/lamp-app-flutter && flutter analyze lib/features/wisp/ && flutter test test/features/wisp/`
Expected: analyze clean; all wisp tests pass.

- [ ] **Step 8: Hardware verify**

Flash a lamp (`PLATFORMIO_UPLOAD_PORT=<port> npm run lamp:flash:beta`) and reinstall the app (`npm run app:install`). With the wisp claiming ≥1 lamp, open the wisp pane and confirm the painted-lamps list shows the claimed lamp(s) by name. (On Android the names should resolve from the nearby report.)

- [ ] **Step 9: Commit**

```bash
git add software/lamp-app-flutter/lib/features/wisp software/lamp-app-flutter/test/features/wisp/painted_lamps_match_test.dart
git commit -m "feat(app): wisp page lists claimed lamps by bdAddr via nearby report

Membership is the claimed bdAddr set (all claimed lamps show); names resolve
from the connected lamp's nearby peers (lamp-reported, so cross-platform).
Colour preview keys on mesh MAC = bdAddr - 2. Drops the Android-only
device-id derivation as the matching source.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Remove the dead `−2`-from-inventory path + stale tests

**Files:**
- Modify: `software/lamp-app-flutter/lib/features/wisp/domain/tuple_sampler.dart` (rename `meshMacFromBleId` → `meshMacFromBdAddr` for honesty, since its input is now a bdAddr, not an inventory id)
- Modify: callers in `wisp_config_screen.dart`
- Modify: `software/lamp-app-flutter/test/features/wisp/tuple_sampler_test.dart` (rename the group)

**Interfaces:**
- Produces: `meshMacFromBdAddr(String bdAddr)` (same body as `meshMacFromBleId`; pure rename so the name reflects that it operates on a lamp-reported bdAddr, never the phone device-id).

- [ ] **Step 1: Rename the function + its test group**

In `tuple_sampler.dart` rename `meshMacFromBleId` → `meshMacFromBdAddr` (body unchanged — still `bdAddr − 2`, 48-bit borrow). Update the doc comment to say it takes a lamp-reported bdAddr. In `tuple_sampler_test.dart` rename the `group('meshMacFromBleId'...)`/`group('meshMacFromBdAddr'...)` and its references; the `parseMacFromBleId` group stays (still used to parse colon-hex). Update the one caller in `_PaintedLampRow` (Task 2 Step 6) to the new name.

- [ ] **Step 2: Confirm no remaining inventory-id derivation**

Run: `cd software/lamp-app-flutter && grep -rn "meshMacFromBleId\|inventoryNotifierProvider" lib/features/wisp/`
Expected: no hits (the wisp feature no longer derives a mesh MAC from a device-id, nor reads inventory for the painted list).

- [ ] **Step 3: Analyze + full app test**

Run: `cd software/lamp-app-flutter && flutter analyze lib/ && flutter test`
Expected: analyze clean; tests green.

- [ ] **Step 4: Commit**

```bash
git add software/lamp-app-flutter/lib/features/wisp software/lamp-app-flutter/test/features/wisp/tuple_sampler_test.dart
git commit -m "refactor(app): rename meshMacFromBleId -> meshMacFromBdAddr

The mesh-MAC derivation now operates on a lamp-reported bdAddr (the right
source), never the phone device-id. Pure rename + doc.

Co-Authored-By: Claude Opus 4.8 (1M context) <noreply@anthropic.com>"
```

---

## Deferred to a follow-on iteration (NOT in this plan)

Per the spec, these are separable and the wisp page does not need them:

- **Inventory learns its own `bdAddr`** (read on connect; on iOS via a lamp self-identity read — the lamp has `NimBLEDevice::getAddress()` but does not expose its OWN bdAddr to the app yet, only peers'). This is only needed to label "your lamps" in lists, not for the wisp page to function. Decide new-characteristic vs fold-into-existing-read at that time (GATT-contract touch).
- **ArtNet / non-mesh lamp** discovery, addressing, control, colour display (the captured roadmap).

## Verification (whole-plan)

- `npm run lamp:test` green except the pre-existing `test_fs_signature`; `npm run lamp:build` clean.
- `cd software/lamp-app-flutter && flutter analyze lib/ && flutter test` clean/green.
- Hardware: wisp claiming ≥1 lamp → the wisp page lists the claimed lamp(s) by name on Android (and iOS if available), with no "no lamps" when the wisp is actively claiming.

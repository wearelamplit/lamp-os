# Control Flow Save-Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the color gradient editors an explicit Save/Cancel with a "Discard changes?" guard on gesture-dismiss (no more silent revert), clearly label reboot-bearing actions, and retire the dead "global Save Changes" comments.

**Architecture:** The base/shade editor sheets already snapshot on open (`_originalColors`/`_originalAc`) and have Save + Cancel buttons; today any non-Save dismiss silently reverts via `PopScope(canPop:true)`. We replace that with a `canPop`-gated discard guard backed by a shared `confirmDiscard` dialog, routing all three explicit exits (Save, Cancel, Discard) through one `_close()` method. Single-value auto-commit is unchanged.

**Tech Stack:** Flutter (Material 3), Riverpod, `flutter test` / `flutter analyze` via `npm run app:test` / `npm run app:analyze`.

## Global Constraints

- **Save model:** single-value tweaks (brightness, name, SSID, toggles) stay auto-commit — do NOT change them. The gradient editor uses explicit Save/Cancel; Cancel discards (reverts to open-snapshot). Reboot-bearing actions (password, advanced-LED) auto-apply but are labeled before firing.
- **Flow depth unchanged:** color card → gradient pane → picker. Do NOT add a single-swatch shortcut.
- **Do NOT touch the connection/reconnect state machine** (reconnect-loss is a separate deferred pass).
- **Keep `lampSaveStatus`** — it drives the reboot "Saving changes…" message. Only fix comments that frame it as a global pill.
- Run from the worktree root: `npm run app:test` (was green at 358) and `npm run app:analyze`. Commit after each task. Hex literals only in `lib/core/theme/brand.dart`; use theme tokens.

> Paths below are relative to `software/lamp-app-flutter/`.

---

### Task 1: Shared `confirmDiscard` dialog + shade editor discard guard

**Files:**
- Create: `lib/core/widgets/confirm_discard.dart`
- Modify: `lib/features/control/presentation/widgets/shade_editor_sheet.dart`
- Test: `test/core/widgets/confirm_discard_test.dart`, `test/features/control/presentation/widgets/shade_editor_sheet_test.dart`

**Interfaces:**
- Produces: `Future<bool> confirmDiscard(BuildContext context)` — shows an AlertDialog titled "Discard changes?"; returns `true` if the user taps **Discard**, `false` (or on barrier dismiss → treat as false) if **Keep editing**.

- [ ] **Step 1: Write the failing test for `confirmDiscard`**
```dart
// test/core/widgets/confirm_discard_test.dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/widgets/confirm_discard.dart';

void main() {
  testWidgets('confirmDiscard returns true on Discard, false on Keep editing',
      (tester) async {
    late BuildContext ctx;
    await tester.pumpWidget(MaterialApp(
      theme: appTheme,
      home: Builder(builder: (c) { ctx = c; return const SizedBox(); }),
    ));

    final discardFuture = confirmDiscard(ctx);
    await tester.pumpAndSettle();
    expect(find.text('Discard changes?'), findsOneWidget);
    await tester.tap(find.text('Discard'));
    await tester.pumpAndSettle();
    expect(await discardFuture, isTrue);

    final keepFuture = confirmDiscard(ctx);
    await tester.pumpAndSettle();
    await tester.tap(find.text('Keep editing'));
    await tester.pumpAndSettle();
    expect(await keepFuture, isFalse);
  });
}
```

- [ ] **Step 2: Run it — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/core/widgets/confirm_discard_test.dart`
Expected: FAIL (`confirm_discard.dart` not found).

- [ ] **Step 3: Implement `confirmDiscard`**
```dart
// lib/core/widgets/confirm_discard.dart
import 'package:flutter/material.dart';

/// Confirm-before-losing-edits dialog. Returns true to discard, false to
/// keep editing (barrier-dismiss counts as keep editing).
Future<bool> confirmDiscard(BuildContext context) async {
  final result = await showDialog<bool>(
    context: context,
    builder: (ctx) => AlertDialog(
      title: const Text('Discard changes?'),
      content: const Text('Your unsaved changes will be lost.'),
      actions: [
        TextButton(
          onPressed: () => Navigator.of(ctx).pop(false),
          child: const Text('Keep editing'),
        ),
        FilledButton(
          onPressed: () => Navigator.of(ctx).pop(true),
          style: FilledButton.styleFrom(
            backgroundColor: Theme.of(ctx).colorScheme.error,
            foregroundColor: Theme.of(ctx).colorScheme.onError,
          ),
          child: const Text('Discard'),
        ),
      ],
    ),
  );
  return result ?? false;
}
```

- [ ] **Step 4: Run it — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/core/widgets/confirm_discard_test.dart`
Expected: PASS.

- [ ] **Step 5: Write the failing shade-editor tests**
Add to `test/features/control/presentation/widgets/shade_editor_sheet_test.dart` (create if absent). Use the project's existing control-test harness pattern (an `InMemoryBleClient` seeded via `test/_support/seed.dart`, `controlNotifierProvider` overridden, pumped under `MaterialApp(theme: appTheme)`); open the sheet via `showShadeEditorSheet`. Three behaviors:
```dart
// 1. Cancel reverts the live shade colors to the open-snapshot and closes.
//    - open sheet, drive a stop edit so shade.colors changes from baseline,
//      tap 'Cancel', pumpAndSettle:
//      expect(find.byType(ShadeEditorSheet), findsNothing);
//      expect(notifier shade.colors == original baseline);
// 2. Save commits (BleUuids.commit written) and closes without reverting.
//      tap 'Save', pumpAndSettle:
//      expect(ble.writesTo(devId, BleUuids.commit), isNotEmpty);
//      expect(find.byType(ShadeEditorSheet), findsNothing);
//      expect(shade.colors == edited value);
// 3. System-back with unsaved changes shows the discard dialog; Discard reverts+closes.
//      after an edit: await tester.binding.handlePopRoute(); await tester.pumpAndSettle();
//      expect(find.text('Discard changes?'), findsOneWidget);
//      await tester.tap(find.text('Discard')); await tester.pumpAndSettle();
//      expect(find.byType(ShadeEditorSheet), findsNothing);
//      expect(shade.colors == original baseline);
```
Write these three `testWidgets` with the harness; they FAIL against the current silent-revert implementation (no 'Discard changes?' dialog appears in test 3 — the back currently pops + silently reverts).

- [ ] **Step 6: Run them — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/control/presentation/widgets/shade_editor_sheet_test.dart`
Expected: test 3 FAILS (no discard dialog).

- [ ] **Step 7: Rework the shade editor to the discard-guard pattern**
In `_ShadeEditorSheetState`, replace the `_committed` field + `cancel()` + the `PopScope(canPop:true, onPopInvoked → silent revert)` with:
```dart
import 'package:flutter/foundation.dart'; // for listEquals
import '../../../../core/widgets/confirm_discard.dart';
...
  List<LampColor>? _originalColors;
  bool _allowPop = false;

  bool _hasUnsavedChanges(List<LampColor> colors) =>
      _originalColors != null && !listEquals(colors, _originalColors);

  void _close({required bool revert}) {
    if (revert && _originalColors != null) {
      ref.read(controlNotifierProvider(widget.lampId).notifier)
          .setShadeColors(_originalColors!);
    }
    setState(() => _allowPop = true);
    // Pop after the frame so PopScope picks up _allowPop=true (setState +
    // synchronous Navigator.pop would still see the old canPop=false).
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (mounted) Navigator.of(context).pop();
    });
  }

  Future<void> _save() async {
    final notifier = ref.read(controlNotifierProvider(widget.lampId).notifier);
    try {
      await notifier.commit();
    } catch (_) {
      if (!mounted) return;
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text("Couldn't save — disconnected")),
      );
      return;
    }
    if (!mounted) return;
    _close(revert: false);
  }
```
PopScope:
```dart
return PopScope(
  canPop: _allowPop || !_hasUnsavedChanges(colors),
  onPopInvokedWithResult: (didPop, _) async {
    if (didPop) return; // no unsaved changes — already popped
    final discard = await confirmDiscard(context);
    if (discard) _close(revert: true);
  },
  child: SafeArea( ... ),
);
```
Wire the buttons: Cancel `onPressed: () => _close(revert: true)`; Save `onPressed: _save`. Delete the old `cancel()` and `_committed`.

- [ ] **Step 8: Run shade tests + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/control/presentation/widgets/shade_editor_sheet_test.dart && npm run --prefix ../.. app:test`
Expected: shade tests PASS; full suite green.

- [ ] **Step 9: Commit**
```bash
git add software/lamp-app-flutter/lib/core/widgets/confirm_discard.dart software/lamp-app-flutter/lib/features/control/presentation/widgets/shade_editor_sheet.dart software/lamp-app-flutter/test/core/widgets/confirm_discard_test.dart software/lamp-app-flutter/test/features/control/presentation/widgets/shade_editor_sheet_test.dart
git commit -m "feat(control): shade editor explicit Save/Cancel + discard guard"
```

---

### Task 2: Base editor discard guard (incl. `ac`)

**Files:**
- Modify: `lib/features/control/presentation/widgets/base_editor_sheet.dart`
- Test: `test/features/control/presentation/widgets/base_editor_sheet_test.dart`

**Interfaces:**
- Consumes: `confirmDiscard` (Task 1); `controlNotifier.setBaseColors`, `.commit`, and the existing base Save persistence (`writeSettingsBlob` for the `ac`/structure — preserve whatever the current Save handler does).

- [ ] **Step 1: Write the failing base-editor tests**
Mirror Task 1's three shade tests for the base editor (`showBaseEditorSheet` / `BaseEditorSheet`), PLUS a fourth: changing only the **active color index (`ac`)** (with colors unchanged) also counts as an unsaved change — system-back then shows the discard dialog. Use `notifier`/state's `base.ac` for the assertion. Write so test 3 + 4 FAIL against the current implementation.

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/control/presentation/widgets/base_editor_sheet_test.dart`
Expected: discard-dialog tests FAIL.

- [ ] **Step 3: Rework the base editor**
Apply the same pattern as Task 1 Step 7, but for base: `_originalColors` + `_originalAc`, `setBaseColors`, and:
```dart
bool _hasUnsavedChanges(List<LampColor> colors, int ac) =>
    (_originalColors != null && !listEquals(colors, _originalColors)) ||
    (_originalAc != null && ac != _originalAc);

void _close({required bool revert}) {
  if (revert) {
    final n = ref.read(controlNotifierProvider(widget.lampId).notifier);
    if (_originalColors != null) n.setBaseColors(_originalColors!);
    // ac is restored by setBaseColors-side state if the editor tracks it;
    // if ac is set separately, also restore _originalAc via its setter.
  }
  setState(() => _allowPop = true);
  WidgetsBinding.instance.addPostFrameCallback((_) {
    if (mounted) Navigator.of(context).pop();
  });
}
```
**Preserve the existing `_save()` persistence** (the base Save already calls `commit()` + `writeSettingsBlob(...)` for the gradient structure/ac — keep that exact persistence; only route the close through `_close(revert:false)` after it succeeds). PopScope identical to Task 1 but `canPop: _allowPop || !_hasUnsavedChanges(colors, activeIndex)`. Cancel → `_close(revert:true)`; Save → existing `_save`. Delete `_committed` + old `cancel()`.
> If `ac` is restored through a dedicated notifier setter rather than via `setBaseColors`, read the current Save/revert code and restore `_originalAc` through the same setter the editor already uses — do not invent a new persistence path.

- [ ] **Step 4: Run base tests + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/control/presentation/widgets/base_editor_sheet_test.dart && npm run --prefix ../.. app:test`
Expected: PASS; full suite green.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/features/control/presentation/widgets/base_editor_sheet.dart software/lamp-app-flutter/test/features/control/presentation/widgets/base_editor_sheet_test.dart
git commit -m "feat(control): base editor explicit Save/Cancel + discard guard"
```

---

### Task 3: Label reboot-bearing actions

**Files:**
- Modify: `lib/features/lamp_shell/presentation/advanced_leds_screen.dart` (the "Update"/save action), and the password-change confirm (`lib/features/lamp_shell/presentation/setup_screen.dart` or wherever `setLampPassword` is triggered — grep `setLampPassword`).
- Test: `test/features/lamp_shell/advanced_leds_screen_test.dart` (extend)

**Interfaces:** none new — copy-only change to existing confirm/action UI.

- [ ] **Step 1: Locate the two reboot triggers**
Run: `cd software/lamp-app-flutter && grep -rn 'setLampPassword\|writeSettingsBlob.*reboot: true\|Advanced LED\|Update' lib/features/lamp_shell lib/features/control/application/control_notifier.dart | head`
Identify (a) the advanced-LED save/"Update" button and (b) the password-change confirm path.

- [ ] **Step 2: Write a failing test for the advanced-LED label**
In `advanced_leds_screen_test.dart`, pump the screen with advanced unlocked and assert the save action's surrounding copy contains the reboot warning, e.g. `expect(find.textContaining('restarts the lamp'), findsOneWidget);`. It FAILS today (no such copy).

- [ ] **Step 3: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/lamp_shell/advanced_leds_screen_test.dart`
Expected: FAIL.

- [ ] **Step 4: Add the reboot-cost copy**
At the advanced-LED save action, add visible helper text near the button: `Text('Saving restarts the lamp (~10s).', style: Theme.of(context).textTheme.bodySmall)`. At the password-change confirm dialog/action, add the same sentence to its body/subtitle. Copy must read exactly `Saving restarts the lamp (~10s).` (so the `textContaining('restarts the lamp')` assertion holds and the copy is consistent).

- [ ] **Step 5: Run test + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/lamp_shell/advanced_leds_screen_test.dart && npm run --prefix ../.. app:test`
Expected: PASS; full suite green.

- [ ] **Step 6: Commit**
```bash
git add -A software/lamp-app-flutter/lib software/lamp-app-flutter/test
git commit -m "feat(control): label reboot-bearing save actions"
```

---

### Task 4: Retire dead "global Save Changes" comments

**Files (comment-only edits):**
- `lib/features/control/presentation/widgets/shade_editor_sheet.dart`, `base_editor_sheet.dart` (docstrings)
- `lib/features/control/application/control_notifier.dart` (lines ~945, 1017, 1442)
- `lib/features/lamp_shell/application/wifi_notifier.dart:58`
- `lib/features/social/presentation/social_screen.dart:26`
- `lib/features/lamp_shell/presentation/home_mode_screen.dart:86,94,96`
- `lib/features/lamp_shell/presentation/advanced_leds_screen.dart:32`

**Interfaces:** none — comments only, zero behavior change.

- [ ] **Step 1: Find every stale reference**
Run: `cd software/lamp-app-flutter && grep -rni 'global save\|save changes pill\|AppBar.*Save\|rides the.*Save Changes\|Save Changes (settings_blob)\|Save Changes path' lib`
This is the authoritative list to fix.

- [ ] **Step 2: Rewrite each comment to describe actual behavior**
Replace "rides the global Save Changes / settings_blob path" wording with the truth: settings persist per-change via `writeSettingsBlob` immediately (name/SSID/devMode `reboot:false`, password/LED `reboot:true`); the gradient editors persist on explicit **Save** (`commit()` / `writeSettingsBlob`) and discard on **Cancel**. For `lampSaveStatus`, keep it but describe it as "tracks the reboot-and-reconnect window for `reboot:true` saves" rather than a global pill. Do NOT change any code — only comments/docstrings.

- [ ] **Step 3: Verify no stale references remain + suite green**
Run: `cd software/lamp-app-flutter && grep -rni 'global save\|save changes pill\|rides the.*Save Changes' lib || echo CLEAN` then `npm run --prefix ../.. app:test && npm run --prefix ../.. app:analyze`
Expected: `CLEAN`; full suite green; analyze no new findings.

- [ ] **Step 4: Commit**
```bash
git add -A software/lamp-app-flutter/lib
git commit -m "docs(control): retire dead global-Save-Changes comments"
```

---

## Self-Review

- **Spec coverage:** §1 gradient editor Save/Cancel/discard → Tasks 1+2; §2 single-value unchanged (no task — correct, nothing to change) + reboot labeling → Task 3; §3 dead-comment sweep → Task 4; §4 testing → tests in each task. Covered.
- **Type consistency:** `confirmDiscard(BuildContext) → Future<bool>` defined in Task 1, consumed in Task 2; `_close({required bool revert})`, `_hasUnsavedChanges`, `_allowPop` consistent across Tasks 1–2. `listEquals` from `package:flutter/foundation.dart`.
- **No placeholders:** the one deliberate "read-the-current-code" note is the base editor's `ac` restore path (Task 2 Step 3) — flagged because the existing Save persistence must be preserved verbatim, not reinvented; the engineer reads the current handler and reuses its setter.

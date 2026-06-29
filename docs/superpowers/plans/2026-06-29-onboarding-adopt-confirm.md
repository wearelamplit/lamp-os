# Onboarding Adopt-Confirm + Pulse Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the post-naming "meet" step with an adopt-confirmation right after a lamp is picked — connect to that lamp and pulse it (washed-out-bright of its color) so the user can physically identify the stray they're adopting — recopy onboarding in the whimsical stray-adoption voice, and clean up the verify view.

**Architecture:** A new `AddLampStep.adoptConfirm` sits between picking a lamp and naming it. A small `AdoptPulseController` opens a BLE connection to the picked lamp on entry, re-fires a `pulse` expression (washed-out-bright of the lamp's advertised base color) via the `expressionTest` characteristic on a repeat timer, and cleans up (stop-write + disconnect) on every exit. The claim still connects fresh at submit — no link is held through the form steps.

**Tech Stack:** Flutter, Riverpod, `flutter_blue_plus` (via `BleClient`), `flutter test` / `flutter analyze` via `npm run app:test` / `npm run app:analyze`.

## Global Constraints

- **Connection rule:** never hold a BLE link through the name/password form steps (trips `LINK_SUPERVISION_TIMEOUT`). The adopt-confirm connection is opened on entry to that step and closed on every exit; the claim connects fresh at `submit()` exactly as today.
- **No firmware work:** the `pulse` expression and the `expressionTest` trigger already exist. Onboarding writes go through `bleClientProvider` directly (NOT `controlNotifier`, which is post-adoption).
- **Copy (binding tone):** cute, whimsical, "adopting a stray" — NO romantic phrasing. Exact strings: adopt-confirm title `Found your stray?`; body `The one blinking at you is the stray you tapped. Take it in?`; buttons `Adopt` / `Cancel`; password submit `Take {name} home`; verifying `Settling in…` (unchanged); done CTA `Say hi to {name}` (unchanged); done section header `Getting to know {name}` (was `First moves with {name}`).
- An unclaimed factory-default lamp accepts control-service writes unauthenticated (same path the claim uses) — `baseColors` and `expressionTest` writes during adopt-confirm rely on this.
- Hex literals only in `lib/core/theme/brand.dart`. Run `npm run app:test` (was green at 369) + `npm run app:analyze`. Commit after each task.

> Paths below are relative to `software/lamp-app-flutter/`.

---

### Task 1: Washed-out-bright color helper

**Files:**
- Create: `lib/features/onboarding/application/identify_color.dart`
- Test: `test/features/onboarding/identify_color_test.dart`

**Interfaces:**
- Consumes: `LampColor` (`lib/features/control/domain/lamp_color.dart`).
- Produces: `LampColor washedOutBright(LampColor base)` — blends the RGB toward white ~50% and maxes brightness (`w` channel left 0), for a brightened/identify version of the lamp's color.

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/domain/lamp_color.dart';
import 'package:lamp_app/features/onboarding/application/identify_color.dart';

void main() {
  test('washedOutBright lightens each channel toward white, never darker', () {
    const base = LampColor(r: 0x20, g: 0x00, b: 0x40, w: 0);
    final out = washedOutBright(base);
    expect(out.r, greaterThan(base.r));
    expect(out.g, greaterThan(base.g));
    expect(out.b, greaterThan(base.b));
    expect(out.r, lessThanOrEqualTo(255));
  });
  test('pure white stays white', () {
    const white = LampColor(r: 255, g: 255, b: 255, w: 0);
    final out = washedOutBright(white);
    expect(out.r, 255); expect(out.g, 255); expect(out.b, 255);
  });
}
```

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/identify_color_test.dart`
Expected: FAIL (not found).

- [ ] **Step 3: Implement**
```dart
import '../../control/domain/lamp_color.dart';

/// A brightened, washed-toward-white version of [base] for the adopt-confirm
/// identify pulse — recognisably the lamp's own colour, but obviously lit up.
LampColor washedOutBright(LampColor base) {
  int wash(int c) => c + ((255 - c) * 0.5).round(); // 50% toward white
  return LampColor(r: wash(base.r), g: wash(base.g), b: wash(base.b), w: 0);
}
```

- [ ] **Step 4: Run — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/identify_color_test.dart`

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/features/onboarding/application/identify_color.dart software/lamp-app-flutter/test/features/onboarding/identify_color_test.dart
git commit -m "feat(onboarding): washed-out-bright identify color helper"
```

---

### Task 2: AdoptPulseController (connect → repeat-pulse → cleanup)

**Files:**
- Create: `lib/features/onboarding/application/adopt_pulse_controller.dart`
- Test: `test/features/onboarding/adopt_pulse_controller_test.dart`

**Interfaces:**
- Consumes: `BleClient` (`lib/core/ble/ble_client.dart` — has `connect`, `disconnect`, `write`), `BleUuids` (`controlService`, `expressionTest`, `baseColors`), `washedOutBright` (Task 1), `LampColor`.
- Produces: `class AdoptPulseController` with `Future<void> start(String deviceId, LampColor baseColor)` and `Future<void> stop()`. `start` connects, sets the washed-out color, and begins re-firing the pulse on a timer; `stop` cancels the timer, sends the expression-complete write, and disconnects. Idempotent `stop` (safe to call twice).

**Mechanism notes (verify on-device during this task):**
- The pulse trigger payload mirrors `controlNotifier.testExpression` but is sent via `BleClient` directly: JSON `{'a':'test_expression','type':'pulse','target':3,'colors':[<washed-out color>]}` to `(controlService, expressionTest)` with `withoutResponse:true`. The firmware `test_expression` handler reads `target` (default 3) and a `colors` key (`software/lamp-os/src/core/lamp.cpp:391,400`); `pulse` uses the first color (`pulse_expression.cpp:39 firstColorOr`).
- **Match the `colors` serialization to how the app already encodes expression colors** — read `ExpressionConfig.toJson` / the `expressionOp` upsert path in `control_notifier.dart` and `lib/features/lamp_shell/domain/` to copy the exact color encoding (hex string vs RGBW object). Do NOT invent a format.
- **Fallback (use only if passing `colors` in the test payload doesn't visibly pulse the washed color on a real lamp):** before triggering, write the washed-out color to `(controlService, baseColors)` (same encoding `controlNotifier.setBaseColors` uses), trigger `pulse` with no colors, and on `stop` restore `baseColors` to the original `baseColor`. Record which path you used in the task report.
- Stop payload: `{'a':'test_expression_complete'}` to `(controlService, expressionTest)`.
- Re-fire interval: 1500ms. All BLE writes are best-effort (wrap in try/catch; a dropped write must not crash the controller).

- [ ] **Step 1: Write the failing test** — using `InMemoryBleClient` (`test/_support/in_memory_ble_client.dart`) and `fake_async`:
```dart
// start(deviceId, color): connects; writes to expressionTest at least twice
//   across two 1500ms ticks (use FakeAsync elapse). assert
//   ble.writesTo(deviceId, BleUuids.expressionTest) length grows over time and
//   the payload decodes to type 'pulse'.
// stop(): sends a final expressionTest write whose payload decodes to
//   'test_expression_complete', AND ble.isConnected(deviceId) == false.
// stop() called twice does not throw.
```
Write these three assertions concretely against the `AdoptPulseController` API above; they FAIL (class absent).

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/adopt_pulse_controller_test.dart`

- [ ] **Step 3: Implement `AdoptPulseController`** per the mechanism notes (connect → fire-once immediately → `Timer.periodic(1500ms)` re-fire → `stop` cancels timer, sends complete, disconnects; guard against double-stop with a `bool _stopped`). Verify the colors-in-payload path on a real lamp via `npm run app:install`; if it doesn't pulse the washed color, switch to the baseColors fallback and note it.

- [ ] **Step 4: Run targeted test + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/adopt_pulse_controller_test.dart && npm run --prefix ../.. app:test`

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/features/onboarding/application/adopt_pulse_controller.dart software/lamp-app-flutter/test/features/onboarding/adopt_pulse_controller_test.dart
git commit -m "feat(onboarding): adopt-confirm pulse controller"
```

---

### Task 3: Step machine — replace `meet` with `adoptConfirm` after pick

**Files:**
- Modify: `lib/features/onboarding/domain/add_lamp_state.dart` (the `AddLampStep` enum)
- Modify: `lib/features/onboarding/application/add_lamp_notifier.dart` (`select`, `next`, `previous`)
- Modify: `lib/features/onboarding/presentation/add_lamp_shell.dart` (the `switch (step)` body + progress dots index)
- Test: `test/features/onboarding/add_lamp_notifier_test.dart` (extend)

**Interfaces:**
- Produces: `AddLampStep.adoptConfirm` (replaces `AddLampStep.meet`); `select(deviceId)` now sets `step: AddLampStep.adoptConfirm` (was `name`).

- [ ] **Step 1: Write the failing test** — assert `select('lamp-x')` lands on `AddLampStep.adoptConfirm` (not `name`); `next()` from `adoptConfirm` → `name`; `previous()` from `adoptConfirm` → `scan`; and the old `meet` is gone (`AddLampStep.values` contains `adoptConfirm`, not `meet`). FAILS today.

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/add_lamp_notifier_test.dart`

- [ ] **Step 3: Implement**
- In `add_lamp_state.dart`: rename enum value `meet` → `adoptConfirm` (keep its position so other ordinals don't shift unexpectedly; re-run codegen if the freezed/json files reference it: `npm run app:codegen`).
- In `add_lamp_notifier.dart`: `select()` sets `step: AddLampStep.adoptConfirm`. Update `next`: `scan→adoptConfirm`? No — `select` jumps straight to `adoptConfirm`, so `next` maps `adoptConfirm → name`, `name → password`, `password → verifying`, `verifying → done`. Update `previous`: `adoptConfirm → scan`, `name → adoptConfirm`? No — naming comes AFTER adopt-confirm and the connection is already closed; `name → adoptConfirm` would re-open a pulse. Set `previous`: `name → scan` (back out to the list), `adoptConfirm → scan`, `password → name`, `verifying → password`, `done → password`. (Remove the `connecting`/`meet` arms.)
- In `add_lamp_shell.dart`: replace `AddLampStep.meet => const AddLampMeetStep()` with `AddLampStep.adoptConfirm => const AdoptConfirmStep()` (Task 4), and fix the `_ProgressDots` index math for the new step count.

- [ ] **Step 4: Run targeted + full suite — PASS** (Task 4's widget must exist or be stubbed; if doing strict TDD order, stub `AdoptConfirmStep` as an empty `SizedBox` here and flesh it in Task 4.)
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/add_lamp_notifier_test.dart && npm run --prefix ../.. app:test`

- [ ] **Step 5: Commit**
```bash
git add -A software/lamp-app-flutter/lib/features/onboarding software/lamp-app-flutter/test/features/onboarding/add_lamp_notifier_test.dart
git commit -m "feat(onboarding): adoptConfirm step replaces meet, after pick"
```

---

### Task 4: AdoptConfirmStep widget

**Files:**
- Create: `lib/features/onboarding/presentation/widgets/adopt_confirm_step.dart`
- Delete: `lib/features/onboarding/presentation/widgets/add_lamp_meet_step.dart` (+ drop its import in the shell)
- Test: `test/features/onboarding/adopt_confirm_step_test.dart`

**Interfaces:**
- Consumes: `AdoptPulseController` (Task 2), `addLampNotifierProvider` (`next` → name, `previous` → scan), the picked lamp's advertised base color (from `nearbyLampsNotifierProvider` keyed by `state.deviceId` — match how `add_lamp_scan_step.dart` reads the lamp), the critter asset.

- [ ] **Step 1: Write the failing widget test** — pump under `MaterialApp(theme: appTheme)` with an `InMemoryBleClient` override and a seeded nearby lamp; assert: the title `Found your stray?` and the body + `Adopt`/`Cancel` render; on first frame an `expressionTest` write fires (pulse started); tapping **Cancel** sends the complete-write + disconnects (`!ble.isConnected`) and returns to scan (`addLampNotifier.step == AddLampStep.scan`); tapping **Adopt** sends complete-write + disconnects and advances to `name`. FAILS (widget absent).

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/adopt_confirm_step_test.dart`

- [ ] **Step 3: Implement** a `ConsumerStatefulWidget` that:
  - In `initState`, reads the picked lamp's base color and `controller.start(deviceId, washedOutBright? no — pass base; the controller applies the wash)`. (Pass the raw advertised base color; the controller calls `washedOutBright` internally.)
  - In `dispose`, calls `controller.stop()` (cleanup on every teardown).
  - **Adopt** button → `await controller.stop(); ref.read(addLampNotifierProvider.notifier).next();`
  - **Cancel** button → `await controller.stop(); ref.read(addLampNotifierProvider.notifier).previous();`
  - Wrap in a `PopScope`/`WillPop` so a system-back also routes through `stop()` then `previous()`.
  - Renders the critter + the lamp's colors + copy: title `Found your stray?`, body `The one blinking at you is the stray you tapped. Take it in?`, buttons `Adopt` / `Cancel`. Theme tokens only.
  - On connect/start failure (the controller surfaces it), show an inline `FriendlyError.inline` "Couldn't reach it — move closer" + a Retry that re-runs `start`; Cancel still works.
  Delete `add_lamp_meet_step.dart` and its shell import.

- [ ] **Step 4: Run targeted + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/adopt_confirm_step_test.dart && npm run --prefix ../.. app:test`

- [ ] **Step 5: Commit**
```bash
git add -A software/lamp-app-flutter/lib/features/onboarding software/lamp-app-flutter/test/features/onboarding/adopt_confirm_step_test.dart
git commit -m "feat(onboarding): adopt-confirm step with identify pulse"
```

---

### Task 5: Verify-view cleanup (hide password fields during the wait)

**Files:**
- Modify: `lib/features/onboarding/presentation/widgets/add_lamp_password_step.dart`
- Test: `test/features/onboarding/add_lamp_password_step_test.dart` (extend / create)

**Interfaces:** none new.

- [ ] **Step 1: Write the failing test** — pump the password step with `state.step == AddLampStep.verifying`; assert the password `TextField`s are absent (`find.byType(TextField)` findsNothing) AND `find.text('Settling in…')` findsOneWidget (the `_VerifyingTips`/spinner render). FAILS today (fields still present during verifying).

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/add_lamp_password_step_test.dart`

- [ ] **Step 3: Implement** — in the password step's `build`, when `isVerifying` is true, render ONLY the dedicated verify state (critter + `_VerifyingTips` + the "Settling in…" indicator) and do NOT build the password/confirm `TextField`s or the Skip/submit row. Keep the non-verifying branch unchanged. (Gate the form subtree on `if (!isVerifying) ...[ fields, buttons ]` and show the verify subtree when `isVerifying`.)

- [ ] **Step 4: Run targeted + full suite — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/add_lamp_password_step_test.dart && npm run --prefix ../.. app:test`

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/features/onboarding/presentation/widgets/add_lamp_password_step.dart software/lamp-app-flutter/test/features/onboarding/add_lamp_password_step_test.dart
git commit -m "feat(onboarding): clean verify view, hide password fields during wait"
```

---

### Task 6: Copy pass (stray-adoption voice)

**Files:**
- Modify: `lib/features/onboarding/presentation/widgets/add_lamp_password_step.dart` (submit label), `add_lamp_done_step.dart` (CTA + section header), and any other onboarding string.
- Test: extend the relevant step tests to assert the new strings.

**Interfaces:** none — copy only.

- [ ] **Step 1: Full string read** — `grep -rniE "Text\\(|label:|title:" lib/features/onboarding` and read every user-facing string. Build the revised set in the binding tone (cute, whimsical, stray-adoption; no romantic phrasing).

- [ ] **Step 2: Write failing tests** for the headline strings: password submit shows `Take {name} home`; done step shows `Getting to know {name}` (not `First moves with`) and the CTA `Say hi to {name}`. FAIL today.

- [ ] **Step 3: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/`

- [ ] **Step 4: Apply the copy** — submit `Welcome them home` → `Take {name} home`; done header `First moves with {name}` → `Getting to know {name}`; keep `Settling in…` and `Say hi to {name}`. Apply any other rewrites your string read surfaced toward the stray/whimsical tone (no romantic phrasing). Theme tokens only; behavior unchanged.

- [ ] **Step 5: Run tests + full suite + analyze — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/onboarding/ && npm run --prefix ../.. app:test && npm run --prefix ../.. app:analyze`

- [ ] **Step 6: Commit**
```bash
git add -A software/lamp-app-flutter/lib/features/onboarding software/lamp-app-flutter/test/features/onboarding
git commit -m "feat(onboarding): stray-adoption copy pass"
```

---

## Self-Review

- **Spec coverage:** §1 flow + pulse → Tasks 1–4; §2 copy → Task 6 (+ confirm-step copy in Task 4); §3 verify view → Task 5; §4 testing → tests in each task. Connection-only-during-adopt-confirm is enforced by Task 2 (`stop` disconnects) + Task 4 (`stop` on Adopt/Cancel/dispose/back). Covered.
- **Type consistency:** `AdoptPulseController.start(String, LampColor)` / `stop()` (Task 2) consumed in Task 4; `washedOutBright(LampColor)` (Task 1) used in Task 2; `AddLampStep.adoptConfirm` (Task 3) used in Tasks 3–4.
- **Known latitude:** Task 2's `colors`-in-test-payload format + the colors-vs-baseColors-fallback is the one spot requiring on-device verification — called out explicitly with a concrete fallback, not left vague. Task 3 notes the freezed/codegen re-run for the enum rename.

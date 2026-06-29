# Setup Hub UX Consistency Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Setup hub consistent and plain-language — fix the mis-grouped Setup-hotspot row and the invisible Home-Mode drill affordance, turn the password subtitle into real status, de-jargon two labels, and unify icon weight.

**Architecture:** Two tasks. Task 1 adds an opt-in `drillChevron` to the shared `SettingsRow` so a toggle-row can also show a drill chevron (default behavior unchanged for every other row). Task 2 applies all the Setup-screen edits (regroup, Home-Mode chevron, password status, de-jargon, icons). All data is already on `ControlState` (`state.lamp.hasPassword` is a `bool?` on the lamp section).

**Tech Stack:** Flutter (Material 3), Riverpod, `flutter test` / `flutter analyze` via `npm run app:test` / `npm run app:analyze`.

## Global Constraints

- **Hub-only.** Do NOT touch the sub-panes (Home Mode pane, LED setup screen) except the LED-setup *summary text* on the hub. Do NOT touch the advanced/dev gated rows (`effectiveAdvancedProvider` / `devMode`: Nearby debug, Cached firmwares, Factory reset) or their gating.
- **No behavior change to toggles/navigation** — Home Mode still enable-drills-in on first-on; the only change is making the drill discoverable. `setLampWebappEnabled` / `setHomeEnabled` unchanged.
- **Exact strings:** password subtitle `Protected` / `Open · no password` / (none when `hasPassword == null`); hotspot title `Setup hotspot`, subtitle `Broadcasts a setup network for 2 min after each power-on` (on) / `Off` (off); LED setup subtitle `Base {base.px} · Shade {shade.px} LEDs` (no byte-order).
- **Icons:** LED setup `Icons.lightbulb_outline`; Setup hotspot `Icons.router_outlined`. Theme tokens only, no hardcoded hex.
- Run `npm run app:test` (was green at 391) + `npm run app:analyze`. Commit after each task.

> Paths below are relative to `software/lamp-app-flutter/`.

---

### Task 1: `SettingsRow.drillChevron`

**Files:**
- Modify: `lib/core/widgets/settings_row.dart`
- Test: `test/core/widgets/settings_row_test.dart`

**Interfaces:**
- Produces: `SettingsRow(..., bool drillChevron = false)` — when `true` and `onTap != null`, the row renders its `trailing` widget AND a trailing `chevron_right`. Default `false` preserves the current rule (chevron only when `onTap != null && trailing == null`).

- [ ] **Step 1: Write the failing tests**
```dart
// test/core/widgets/settings_row_test.dart — add:
testWidgets('drillChevron shows chevron alongside a trailing widget', (tester) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(body: SettingsRow(
      icon: Icons.home_outlined, title: 'Home Mode',
      trailing: Switch(value: true, onChanged: (_) {}),
      onTap: () {}, drillChevron: true,
    )),
  ));
  expect(find.byType(Switch), findsOneWidget);
  expect(find.byIcon(Icons.chevron_right), findsOneWidget);
});
testWidgets('without drillChevron a trailing widget suppresses the chevron', (tester) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(body: SettingsRow(
      icon: Icons.wifi_tethering, title: 'Toggle only',
      trailing: Switch(value: false, onChanged: (_) {}),
      onTap: () {},
    )),
  ));
  expect(find.byType(Switch), findsOneWidget);
  expect(find.byIcon(Icons.chevron_right), findsNothing);
});
testWidgets('a plain drill row still shows the chevron', (tester) async {
  await tester.pumpWidget(MaterialApp(
    home: Scaffold(body: SettingsRow(
      icon: Icons.memory, title: 'Drill', onTap: () {},
    )),
  ));
  expect(find.byIcon(Icons.chevron_right), findsOneWidget);
});
```
(If `settings_row_test.dart` has no imports yet, add `package:flutter/material.dart`, `package:flutter_test/flutter_test.dart`, and the `SettingsRow` import `package:lamp_app/core/widgets/settings_row.dart`.)

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/core/widgets/settings_row_test.dart`
Expected: the first test FAILS (`drillChevron` is not a parameter).

- [ ] **Step 3: Implement**
In `settings_row.dart`, add the field and adjust the trailing/chevron block:
```dart
  const SettingsRow({
    super.key,
    required this.icon,
    required this.title,
    this.subtitle,
    this.trailing,
    this.onTap,
    this.drillChevron = false,
  });
  ...
  final VoidCallback? onTap;
  final bool drillChevron;
```
Replace the trailing/chevron section (currently lines 66–72) with:
```dart
              if (trailing != null) ...[
                const SizedBox(width: 8),
                trailing!,
              ],
              if (onTap != null && (trailing == null || drillChevron)) ...[
                if (trailing != null) const SizedBox(width: 4),
                Icon(Icons.chevron_right,
                    color: colorScheme.onSurfaceVariant, size: 22),
              ],
```

- [ ] **Step 4: Run — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/core/widgets/settings_row_test.dart`

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/widgets/settings_row.dart software/lamp-app-flutter/test/core/widgets/settings_row_test.dart
git commit -m "feat(widgets): SettingsRow opt-in drillChevron alongside trailing"
```

---

### Task 2: Setup-hub edits (regroup, chevron, status, de-jargon, icons)

**Files:**
- Modify: `lib/features/lamp_shell/presentation/setup_screen.dart` (the `_SetupBody` `ListView`, lines ~92–193)
- Test: `test/features/lamp_shell/setup_screen_test.dart`

**Interfaces:**
- Consumes: `SettingsRow.drillChevron` (Task 1); `state.lamp.hasPassword` (`bool?` on the lamp section); `state.lamp.webappEnabled`, `state.base.px`, `state.shade.px`.

- [ ] **Step 1: Write the failing tests**
Extend `setup_screen_test.dart` using the existing control-test harness (seed a `ControlState` via the harness already used in that file; pump `SetupScreen`/`_SetupBody` under `MaterialApp(theme: appTheme)`). Add:
```dart
// Password status (drive hasPassword through the seeded lamp section):
//  hasPassword:true  → expect(find.text('Protected'), findsOneWidget);
//  hasPassword:false → expect(find.text('Open · no password'), findsOneWidget);
//  hasPassword:null  → expect(find.text('Protected'), findsNothing);
//                      expect(find.text('Open · no password'), findsNothing);
// De-jargon:
//  expect(find.text('Setup hotspot'), findsOneWidget);
//  expect(find.textContaining('Boot-time setup AP'), findsNothing);
//  expect(find.textContaining('GRB'), findsNothing);   // LED summary cleaned
//  with base.px=40, shade.px=60: expect(find.text('Base 40 · Shade 60 LEDs'), findsOneWidget);
// Home Mode drill chevron: the Home Mode row shows BOTH a Switch and a chevron.
//  Find the SettingsRow whose title is 'Home Mode' and assert a descendant Switch AND
//  a descendant Icon(Icons.chevron_right) (use find.descendant / find.widgetWithText).
// Grouping order: 'Setup hotspot' sits under CONNECTIVITY, not LEDS — assert its vertical
//  position is below the 'CONNECTIVITY' heading and above the 'LEDS' heading using
//  tester.getTopLeft(find.text(...)).dy comparisons (headings render uppercased:
//  'CONNECTIVITY' / 'LEDS' / 'LAMP').
```
Write these as concrete `testWidgets` against the harness; they FAIL today (old labels, no chevron, wrong group, instruction subtitle).

- [ ] **Step 2: Run — FAIL**
Run: `cd software/lamp-app-flutter && flutter test test/features/lamp_shell/setup_screen_test.dart`

- [ ] **Step 3: Implement the edits in `_SetupBody`'s `ListView`**
- **Password row** subtitle → status:
```dart
SettingsRow(
  icon: Icons.lock_outline,
  title: 'Password',
  subtitle: switch (state.lamp.hasPassword) {
    true => 'Protected',
    false => 'Open · no password',
    null => null,
  },
  onTap: () async { /* unchanged showPasswordPromptDialog body */ },
),
```
- **Connectivity group:** keep the Home Mode row but add `drillChevron: true`, and **move the Setup-hotspot row to immediately after Home Mode** (out of the LEDs group):
```dart
const SettingsGroupHeading('Connectivity'),
SettingsRow(
  icon: Icons.home_outlined,
  title: 'Home Mode',
  subtitle: homeSubtitle,
  drillChevron: true,
  trailing: Switch( /* unchanged */ ),
  onTap: () => context.push(AppRoutes.homeMode(lampId)),
),
SettingsRow(
  icon: Icons.router_outlined,
  title: 'Setup hotspot',
  subtitle: state.lamp.webappEnabled
      ? 'Broadcasts a setup network for 2 min after each power-on'
      : 'Off',
  trailing: Switch(
    value: state.lamp.webappEnabled,
    onChanged: (v) => n.setLampWebappEnabled(v),
  ),
),
```
- **LEDs group:** now only LED setup, with the de-jargoned subtitle + outline icon:
```dart
const SettingsGroupHeading('LEDs'),
SettingsRow(
  icon: Icons.lightbulb_outline,
  title: 'LED setup',
  subtitle: 'Base ${state.base.px} · Shade ${state.shade.px} LEDs',
  onTap: () => context.push(AppRoutes.advancedLeds(lampId)),
),
```
Delete the old "Boot-time setup AP" SettingsRow from the LEDs group (it's now the Setup-hotspot row under Connectivity). Leave the gated advanced/dev rows (`if (ref.watch(effectiveAdvancedProvider(lampId)))` / `if (state.lamp.devMode)`) unchanged.

- [ ] **Step 4: Run targeted + full suite + analyze — PASS**
Run: `cd software/lamp-app-flutter && flutter test test/features/lamp_shell/setup_screen_test.dart && npm run --prefix ../.. app:test && npm run --prefix ../.. app:analyze`
Expected: setup tests PASS; full suite green; analyze no new findings.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/features/lamp_shell/presentation/setup_screen.dart software/lamp-app-flutter/test/features/lamp_shell/setup_screen_test.dart
git commit -m "feat(setup): regroup hotspot, drill chevron, password status, de-jargon"
```

---

## Self-Review

- **Spec coverage:** §1 regroup → Task 2 (hotspot under Connectivity); §2 drill affordance → Task 1 (`drillChevron`) + Task 2 (Home Mode uses it); §3 password status → Task 2 (switch on `state.lamp.hasPassword`, incl. null); §4 de-jargon → Task 2 (Setup hotspot label/subtitle, LED summary); §5 icons → Task 2 (`lightbulb_outline`, `router_outlined`); testing → tests in both tasks. Covered.
- **Type consistency:** `drillChevron` (bool, default false) defined Task 1, consumed Task 2. `state.lamp.hasPassword` is `bool?` (confirmed in `sections.dart:62`), matching the 3-arm switch.
- **No placeholders:** the password `onTap` and Home Mode `Switch`/`trailing` bodies are marked "unchanged" with a pointer to the existing code (the engineer keeps the current body verbatim — it is not new code to write).

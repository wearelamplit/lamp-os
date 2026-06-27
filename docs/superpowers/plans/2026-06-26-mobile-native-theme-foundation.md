# Mobile-Native Theme Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the app's unused theme + Vue-port bespoke UI with a real Material 3 design system built on the current lamplit.ca brand, a native component kit, three standardized controls, and a global re-skin.

**Architecture:** A single source-of-truth theme (`lib/core/theme/`) — raw brand tokens → hand-mapped M3 `ColorScheme.dark` → typography (Josefin+Inter) → spacing tokens → assembled `ThemeData` with component themes. Screens consume the theme via `Theme.of(context)` / `context.textTheme` instead of inline styles. A shared widget kit (`lib/core/widgets/`) provides the native form-grouping system. Three controls are rebuilt on Material primitives. A final mechanical sweep routes existing screens through the new system.

**Tech Stack:** Flutter (Material 3), Riverpod, go_router, `flutter test` / `flutter analyze` (via `npm run app:test` / `npm run app:analyze`).

## Global Constraints

- **Brand source of truth:** lamplit.ca/brand. Hex literals live ONLY in `lib/core/theme/brand.dart`.
- **Dark-only.** No light theme.
- **Material by default; bespoke only where earned** (hardware RGBW picker, critter system, `StatusDot`).
- **Brand role map:** primary = Deep Pink `#C869C8` (light pair Soft Pink `#EFA8F0`); secondary = Golden Yellow `#F8CC48` (Cream `#FFFDD1`); tertiary = Deep Blue `#6366F1` (Lavender `#9EA1FF`); success (extension) = Deep Green `#86EFAC` (Light `#BBFFAD`); error = Coral `#F87171`. Neutrals: Midnight `#0D0D0D` (surface), Carbon `#1A1A1A` (surfaceContainer/cards), Lamp White `#FDFDFD` (onSurface), `#CCCCCC` (onSurfaceVariant). Warm-white reference `#FABB3E`.
- **Fonts:** Josefin Sans (display/headline/title; max weight 700) + Inter (body/label/data), bundled in `assets/fonts/`.
- **Spacing tokens:** `xs=4, sm=8, md=12, lg=16, xl=24, xxl=32`; radius `card=12`.
- **Tests stay green.** Existing suite is 334. Run `npm run app:test` and `npm run app:analyze` (working dir: `software/lamp-app-flutter`). Commit after each task.
- **Out of scope:** per-page UX redesign, light theme, exposing hidden/advanced tiers, app-size work.

> All paths below are relative to `software/lamp-app-flutter/`.

---

## Phase 1 — Theme Foundation

### Task 1: Brand tokens

**Files:**
- Create: `lib/core/theme/brand.dart`
- Test: `test/core/theme/brand_test.dart`
- (Old `lib/core/theme/brand_colors.dart` is retired in Task 18's sweep; leave it for now so nothing breaks.)

**Interfaces:**
- Produces: `abstract class Brand` with `static const Color` tokens: `softPink, deepPink, creamYellow, goldenYellow, lavenderBlue, deepBlue, lightGreen, deepGreen, midnightBlack, carbonGrey, lampWhite, fogGrey (#CCCCCC), coral (#F87171), warmWhite (#FABB3E)`.

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('brand tokens match lamplit.ca/brand hex values', () {
    expect(Brand.deepPink, const Color(0xFFC869C8));
    expect(Brand.goldenYellow, const Color(0xFFF8CC48));
    expect(Brand.deepBlue, const Color(0xFF6366F1));
    expect(Brand.deepGreen, const Color(0xFF86EFAC));
    expect(Brand.midnightBlack, const Color(0xFF0D0D0D));
    expect(Brand.carbonGrey, const Color(0xFF1A1A1A));
    expect(Brand.lampWhite, const Color(0xFFFDFDFD));
    expect(Brand.coral, const Color(0xFFF87171));
    expect(Brand.warmWhite, const Color(0xFFFABB3E));
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/brand_test.dart`
Expected: FAIL — `brand.dart` / `Brand` not found.

- [ ] **Step 3: Write minimal implementation**
```dart
import 'package:flutter/material.dart';

/// Raw brand tokens — the ONLY place color hex literals live.
/// Source of truth: lamplit.ca/brand. Recolor the app here.
abstract class Brand {
  // Primary (Pink)
  static const softPink = Color(0xFFEFA8F0);
  static const deepPink = Color(0xFFC869C8);
  // Secondary (Yellow)
  static const creamYellow = Color(0xFFFFFDD1);
  static const goldenYellow = Color(0xFFF8CC48);
  // Tertiary (Blue)
  static const lavenderBlue = Color(0xFF9EA1FF);
  static const deepBlue = Color(0xFF6366F1);
  // Quaternary (Green) → success role
  static const lightGreen = Color(0xFFBBFFAD);
  static const deepGreen = Color(0xFF86EFAC);
  // Neutrals
  static const midnightBlack = Color(0xFF0D0D0D);
  static const carbonGrey = Color(0xFF1A1A1A);
  static const lampWhite = Color(0xFFFDFDFD);
  static const fogGrey = Color(0xFFCCCCCC);
  // Functional
  static const coral = Color(0xFFF87171);
  static const warmWhite = Color(0xFFFABB3E);
}
```

- [ ] **Step 4: Run test to verify it passes**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/brand_test.dart`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/brand.dart software/lamp-app-flutter/test/core/theme/brand_test.dart
git commit -m "feat(theme): brand token source of truth"
```

---

### Task 2: M3 ColorScheme

**Files:**
- Create: `lib/core/theme/app_color_scheme.dart`
- Test: `test/core/theme/app_color_scheme_test.dart`

**Interfaces:**
- Consumes: `Brand` (Task 1).
- Produces: `const ColorScheme appColorScheme` (a `ColorScheme` with `brightness: Brightness.dark`).

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_color_scheme.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('color scheme maps brand roles correctly', () {
    expect(appColorScheme.brightness, Brightness.dark);
    expect(appColorScheme.primary, Brand.deepPink);
    expect(appColorScheme.secondary, Brand.goldenYellow);
    expect(appColorScheme.tertiary, Brand.deepBlue);
    expect(appColorScheme.error, Brand.coral);
    expect(appColorScheme.surface, Brand.midnightBlack);
    expect(appColorScheme.surfaceContainer, Brand.carbonGrey);
    expect(appColorScheme.onSurface, Brand.lampWhite);
    expect(appColorScheme.onSurfaceVariant, Brand.fogGrey);
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_color_scheme_test.dart`
Expected: FAIL — `appColorScheme` not found.

- [ ] **Step 3: Write minimal implementation**
```dart
import 'package:flutter/material.dart';
import 'brand.dart';

/// Hand-mapped M3 dark ColorScheme. Each brand color's light/dark variant
/// forms the role's tonal pair (color + container/on-color). Surfaces are
/// solid neutral tones (no opacity overlays).
const ColorScheme appColorScheme = ColorScheme(
  brightness: Brightness.dark,
  primary: Brand.deepPink,
  onPrimary: Brand.midnightBlack,
  primaryContainer: Brand.deepPink,
  onPrimaryContainer: Brand.softPink,
  secondary: Brand.goldenYellow,
  onSecondary: Brand.midnightBlack,
  secondaryContainer: Brand.goldenYellow,
  onSecondaryContainer: Brand.creamYellow,
  tertiary: Brand.deepBlue,
  onTertiary: Brand.lampWhite,
  tertiaryContainer: Brand.deepBlue,
  onTertiaryContainer: Brand.lavenderBlue,
  error: Brand.coral,
  onError: Brand.midnightBlack,
  surface: Brand.midnightBlack,
  onSurface: Brand.lampWhite,
  surfaceContainerLowest: Brand.midnightBlack,
  surfaceContainerLow: Brand.carbonGrey,
  surfaceContainer: Brand.carbonGrey,
  surfaceContainerHigh: Color(0xFF222222),
  surfaceContainerHighest: Color(0xFF2A2A2A),
  onSurfaceVariant: Brand.fogGrey,
  outline: Color(0xFF3A3A3A),
  outlineVariant: Color(0xFF2A2A2A),
);
```

- [ ] **Step 4: Run test to verify it passes**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_color_scheme_test.dart`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/app_color_scheme.dart software/lamp-app-flutter/test/core/theme/app_color_scheme_test.dart
git commit -m "feat(theme): hand-mapped M3 dark ColorScheme from brand"
```

---

### Task 3: BrandExtras ThemeExtension (success / gradient / warm-white)

**Files:**
- Create: `lib/core/theme/brand_extras.dart`
- Test: `test/core/theme/brand_extras_test.dart`

**Interfaces:**
- Consumes: `Brand`.
- Produces: `class BrandExtras extends ThemeExtension<BrandExtras>` with fields `Color success`, `Color onSuccess`, `Gradient chromeGradient`, `Color warmWhite`; a `static const BrandExtras dark`; standard `copyWith`/`lerp`. Plus extension getter `BuildContext.brandExtras`.

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/brand_extras.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('BrandExtras exposes success/gradient/warmWhite and lerps', () {
    const e = BrandExtras.dark;
    expect(e.success, Brand.deepGreen);
    expect(e.warmWhite, Brand.warmWhite);
    final mid = e.lerp(e, 0.5) as BrandExtras;
    expect(mid.success, Brand.deepGreen);
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/brand_extras_test.dart`
Expected: FAIL — `BrandExtras` not found.

- [ ] **Step 3: Write minimal implementation**
```dart
import 'package:flutter/material.dart';
import 'brand.dart';

/// Roles M3's ColorScheme has no slot for: success (green), the aurora→pink
/// chrome gradient, and the warm-white channel reference.
@immutable
class BrandExtras extends ThemeExtension<BrandExtras> {
  const BrandExtras({
    required this.success,
    required this.onSuccess,
    required this.chromeGradient,
    required this.warmWhite,
  });

  final Color success;
  final Color onSuccess;
  final Gradient chromeGradient;
  final Color warmWhite;

  static const BrandExtras dark = BrandExtras(
    success: Brand.deepGreen,
    onSuccess: Brand.midnightBlack,
    chromeGradient: LinearGradient(colors: [Brand.deepBlue, Brand.deepPink]),
    warmWhite: Brand.warmWhite,
  );

  @override
  BrandExtras copyWith({
    Color? success,
    Color? onSuccess,
    Gradient? chromeGradient,
    Color? warmWhite,
  }) =>
      BrandExtras(
        success: success ?? this.success,
        onSuccess: onSuccess ?? this.onSuccess,
        chromeGradient: chromeGradient ?? this.chromeGradient,
        warmWhite: warmWhite ?? this.warmWhite,
      );

  @override
  BrandExtras lerp(ThemeExtension<BrandExtras>? other, double t) {
    if (other is! BrandExtras) return this;
    return BrandExtras(
      success: Color.lerp(success, other.success, t)!,
      onSuccess: Color.lerp(onSuccess, other.onSuccess, t)!,
      chromeGradient: Gradient.lerp(chromeGradient, other.chromeGradient, t)!,
      warmWhite: Color.lerp(warmWhite, other.warmWhite, t)!,
    );
  }
}

extension BrandExtrasContext on BuildContext {
  BrandExtras get brandExtras =>
      Theme.of(this).extension<BrandExtras>() ?? BrandExtras.dark;
}
```

- [ ] **Step 4: Run test to verify it passes**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/brand_extras_test.dart`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/brand_extras.dart software/lamp-app-flutter/test/core/theme/brand_extras_test.dart
git commit -m "feat(theme): BrandExtras extension (success/gradient/warm-white)"
```

---

### Task 4: Spacing tokens

**Files:**
- Create: `lib/core/theme/app_spacing.dart`
- Test: `test/core/theme/app_spacing_test.dart`

**Interfaces:**
- Produces: `abstract class AppSpace` with `static const double xs=4, sm=8, md=12, lg=16, xl=24, xxl=32;` and `abstract class AppRadius` with `static const double card=12;`.

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_spacing.dart';

void main() {
  test('spacing + radius tokens', () {
    expect(AppSpace.md, 12);
    expect(AppSpace.lg, 16);
    expect(AppRadius.card, 12);
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_spacing_test.dart`
Expected: FAIL.

- [ ] **Step 3: Write minimal implementation**
```dart
/// 4/8pt spacing scale + radius tokens. Replaces magic-number EdgeInsets.
abstract class AppSpace {
  static const double xs = 4;
  static const double sm = 8;
  static const double md = 12;
  static const double lg = 16;
  static const double xl = 24;
  static const double xxl = 32;
}

abstract class AppRadius {
  static const double card = 12;
}
```

- [ ] **Step 4: Run test to verify it passes**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_spacing_test.dart`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/app_spacing.dart software/lamp-app-flutter/test/core/theme/app_spacing_test.dart
git commit -m "feat(theme): spacing + radius tokens"
```

---

### Task 5: Bundle fonts (Josefin Sans + Inter)

**Files:**
- Create: `assets/fonts/JosefinSans-Regular.ttf`, `JosefinSans-SemiBold.ttf`, `JosefinSans-Bold.ttf`, `Inter-Regular.ttf`, `Inter-Medium.ttf`, `Inter-SemiBold.ttf` (static weights downloaded from Google Fonts; OFL-licensed)
- Modify: `pubspec.yaml` (add the `fonts:` declarations under `flutter:`)

**Interfaces:**
- Produces: font families `JosefinSans` (weights 400/600/700) and `Inter` (400/500/600) available to `TextStyle(fontFamily: ...)`.

- [ ] **Step 1: Download the static TTFs into `assets/fonts/`**
```bash
cd software/lamp-app-flutter && mkdir -p assets/fonts
# Fetch from Google Fonts (github.com/google/fonts, OFL). Place the 6 files above.
```

- [ ] **Step 2: Declare fonts in `pubspec.yaml`** (under the existing `flutter:` key, alongside `uses-material-design: true`)
```yaml
  fonts:
    - family: JosefinSans
      fonts:
        - asset: assets/fonts/JosefinSans-Regular.ttf
        - asset: assets/fonts/JosefinSans-SemiBold.ttf
          weight: 600
        - asset: assets/fonts/JosefinSans-Bold.ttf
          weight: 700
    - family: Inter
      fonts:
        - asset: assets/fonts/Inter-Regular.ttf
        - asset: assets/fonts/Inter-Medium.ttf
          weight: 500
        - asset: assets/fonts/Inter-SemiBold.ttf
          weight: 600
```

- [ ] **Step 3: Verify it resolves**
Run: `cd software/lamp-app-flutter && flutter pub get && flutter analyze lib`
Expected: no asset/font errors.

- [ ] **Step 4: Commit**
```bash
git add software/lamp-app-flutter/assets/fonts software/lamp-app-flutter/pubspec.yaml
git commit -m "feat(theme): bundle Josefin Sans + Inter fonts"
```

---

### Task 6: Typography scale

**Files:**
- Create: `lib/core/theme/app_typography.dart`
- Test: `test/core/theme/app_typography_test.dart`

**Interfaces:**
- Produces: `const TextTheme appTextTheme` — Josefin for display/headline/title roles, Inter for body/label. All colors default to `Brand.lampWhite` (overridable per use).

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_typography.dart';

void main() {
  test('type scale pairs Josefin (display/title) + Inter (body)', () {
    expect(appTextTheme.displaySmall!.fontFamily, 'JosefinSans');
    expect(appTextTheme.titleMedium!.fontFamily, 'JosefinSans');
    expect(appTextTheme.bodyMedium!.fontFamily, 'Inter');
    expect(appTextTheme.labelLarge!.fontFamily, 'Inter');
    // Josefin tops out at 700
    expect(appTextTheme.displaySmall!.fontWeight!.index, lessThanOrEqualTo(FontWeight.w700.index));
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_typography_test.dart`
Expected: FAIL.

- [ ] **Step 3: Write minimal implementation**
```dart
import 'package:flutter/material.dart';
import 'brand.dart';

const _josefin = 'JosefinSans';
const _inter = 'Inter';
const _ink = Brand.lampWhite;

/// Type scale. Josefin Sans for brand/headings/titles, Inter for dense body
/// + data. Screens use context.textTheme.* instead of inline TextStyle.
const TextTheme appTextTheme = TextTheme(
  displaySmall: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w700, fontSize: 28, color: _ink),
  headlineSmall: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 22, color: _ink),
  titleLarge: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 18, color: _ink),
  titleMedium: TextStyle(fontFamily: _josefin, fontWeight: FontWeight.w600, fontSize: 15, color: _ink),
  bodyLarge: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 15, color: _ink),
  bodyMedium: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 13, color: Brand.fogGrey),
  bodySmall: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w400, fontSize: 12, color: Brand.fogGrey),
  labelLarge: TextStyle(fontFamily: _inter, fontWeight: FontWeight.w600, fontSize: 11, letterSpacing: 1.2, color: Brand.fogGrey),
);
```

- [ ] **Step 4: Run test to verify it passes**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_typography_test.dart`
Expected: PASS.

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/app_typography.dart software/lamp-app-flutter/test/core/theme/app_typography_test.dart
git commit -m "feat(theme): Josefin+Inter type scale"
```

---

### Task 7: Assemble ThemeData + component themes, wire into app

**Files:**
- Rewrite: `lib/core/theme/app_theme.dart`
- Modify: `lib/app.dart` (point `MaterialApp.theme` at the new `appTheme`; ensure `themeMode`/`darkTheme` reflect dark-only)
- Test: `test/core/theme/app_theme_test.dart`

**Interfaces:**
- Consumes: `appColorScheme`, `appTextTheme`, `BrandExtras.dark`, `AppRadius`.
- Produces: `ThemeData appTheme`. Replaces the old `AppTheme` symbol if `app.dart` referenced it (update the reference).

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/core/theme/app_theme.dart';
import 'package:lamp_app/core/theme/brand_extras.dart';
import 'package:lamp_app/core/theme/brand.dart';

void main() {
  test('appTheme is M3, dark, carries BrandExtras, themes bottom sheets', () {
    expect(appTheme.useMaterial3, true);
    expect(appTheme.brightness, Brightness.dark);
    expect(appTheme.colorScheme.primary, Brand.deepPink);
    expect(appTheme.extension<BrandExtras>(), isNotNull);
    expect(appTheme.bottomSheetTheme.showDragHandle, true);
    expect(appTheme.cardTheme.color, Brand.carbonGrey);
  });
}
```

- [ ] **Step 2: Run test to verify it fails**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_theme_test.dart`
Expected: FAIL.

- [ ] **Step 3: Write minimal implementation**
```dart
import 'package:flutter/material.dart';
import 'app_color_scheme.dart';
import 'app_spacing.dart';
import 'app_typography.dart';
import 'brand.dart';
import 'brand_extras.dart';

final ThemeData appTheme = ThemeData(
  useMaterial3: true,
  brightness: Brightness.dark,
  colorScheme: appColorScheme,
  scaffoldBackgroundColor: Brand.midnightBlack,
  textTheme: appTextTheme,
  extensions: const [BrandExtras.dark],
  cardTheme: CardThemeData(
    color: Brand.carbonGrey,
    elevation: 0,
    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppRadius.card)),
    margin: EdgeInsets.zero,
  ),
  bottomSheetTheme: const BottomSheetThemeData(
    showDragHandle: true,
    backgroundColor: Brand.carbonGrey,
  ),
  dialogTheme: const DialogThemeData(backgroundColor: Brand.carbonGrey),
  snackBarTheme: const SnackBarThemeData(behavior: SnackBarBehavior.floating),
  listTileTheme: const ListTileThemeData(iconColor: Brand.fogGrey),
  // sliderTheme / inputDecorationTheme / button themes inherit colorScheme;
  // override here only where the M3 default needs brand nudging.
);
```
Then in `lib/app.dart`, set `theme: appTheme` (and `darkTheme: appTheme`, `themeMode: ThemeMode.dark`), replacing any reference to the old theme symbol.

- [ ] **Step 4: Run test + full suite + analyze**
Run: `cd software/lamp-app-flutter && flutter test test/core/theme/app_theme_test.dart && npm run --prefix ../.. app:test && npm run --prefix ../.. app:analyze`
Expected: theme test PASS; full suite still green; analyze clean. (The app now renders in the new brand colors wherever screens read the theme.)

- [ ] **Step 5: Commit**
```bash
git add software/lamp-app-flutter/lib/core/theme/app_theme.dart software/lamp-app-flutter/lib/app.dart software/lamp-app-flutter/test/core/theme/app_theme_test.dart
git commit -m "feat(theme): assemble ThemeData + component themes; wire into app"
```

---

## Phase 2 — Component Kit

> Pattern for Tasks 8–12: write a widget test that pumps the widget inside `MaterialApp(theme: appTheme, home: ...)`, assert structure/behavior, implement, green, commit. Each replaces or refactors a bespoke widget in `lib/core/widgets/`.

### Task 8: SectionHeader (shared, promoted)

**Files:**
- Create: `lib/core/widgets/section_header.dart`
- Test: `test/core/widgets/section_header_test.dart`

**Interfaces:**
- Produces: `class SectionHeader extends StatelessWidget { const SectionHeader(this.label, {super.key}); final String label; }` — renders `label.toUpperCase()` in `context.textTheme.labelLarge`.

- [ ] **Step 1: Failing test** — pump `SectionHeader('Personality')`; `expect(find.text('PERSONALITY'), findsOneWidget)`.
- [ ] **Step 2:** Run `flutter test test/core/widgets/section_header_test.dart` → FAIL.
- [ ] **Step 3:** Implement: a `Padding(AppSpace.sm vertical)` wrapping `Text(label.toUpperCase(), style: Theme.of(context).textTheme.labelLarge)`.
- [ ] **Step 4:** Run test → PASS.
- [ ] **Step 5:** `git commit -m "feat(widgets): shared SectionHeader"`

---

### Task 9: LampCard

**Files:**
- Create: `lib/core/widgets/lamp_card.dart`
- Test: `test/core/widgets/lamp_card_test.dart`

**Interfaces:**
- Produces: `class LampCard extends StatelessWidget { const LampCard({required this.child, this.padding, super.key}); final Widget child; final EdgeInsets? padding; }` — a `Card` (inherits `CardTheme`) with default `EdgeInsets.all(AppSpace.lg)` padding.

- [ ] **Step 1: Failing test**
```dart
testWidgets('LampCard renders child on carbon surface', (t) async {
  await t.pumpWidget(MaterialApp(theme: appTheme, home: const Scaffold(body: LampCard(child: Text('hi')))));
  expect(find.text('hi'), findsOneWidget);
  expect(find.byType(Card), findsOneWidget);
});
```
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement `Card(child: Padding(padding ?? EdgeInsets.all(AppSpace.lg), child: child))`.
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** `git commit -m "feat(widgets): LampCard surface"`

---

### Task 10: NavRow

**Files:**
- Create: `lib/core/widgets/nav_row.dart`
- Test: `test/core/widgets/nav_row_test.dart`

**Interfaces:**
- Produces: `class NavRow extends StatelessWidget` with `{required IconData icon, required String title, String? subtitle, VoidCallback? onTap}` — a `ListTile` with a leading tinted icon chip (`CircleAvatar`-style, `colorScheme.primaryContainer` bg), `title` (`titleMedium`), `subtitle` (`bodySmall`), trailing `Icons.chevron_right`, `onTap`.

- [ ] **Step 1: Failing test** — pump `NavRow(icon: Icons.wifi, title: 'WiFi', subtitle: 'Off', onTap: () => tapped = true)`; assert `find.text('WiFi')`, `find.byIcon(Icons.chevron_right)`, tap fires callback.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement the `ListTile` per the interface above (leading chip = `Container`/`CircleAvatar` 36px with `Icon(icon, color: colorScheme.onPrimaryContainer)`).
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** `git commit -m "feat(widgets): canonical NavRow"`

---

### Task 11: FormSection

**Files:**
- Create: `lib/core/widgets/form_section.dart`
- Test: `test/core/widgets/form_section_test.dart`

**Interfaces:**
- Consumes: `SectionHeader`, `LampCard`.
- Produces: `class FormSection extends StatelessWidget { const FormSection({required this.title, required this.children, super.key}); final String title; final List<Widget> children; }` — renders a `SectionHeader(title)` above a `LampCard` containing `children` separated by `Divider`s (hairline, `colorScheme.outlineVariant`).

- [ ] **Step 1: Failing test** — pump `FormSection(title: 'LEDs', children: [Text('a'), Text('b')])`; assert header `'LEDS'` + both rows + at least one `Divider`.
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement: `Column(crossAxisAlignment: stretch, children: [SectionHeader(title), LampCard(padding: EdgeInsets.zero, child: Column(children: _withDividers(children)))])` where `_withDividers` interleaves `Divider(height: 1, color: outlineVariant)`.
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** `git commit -m "feat(widgets): FormSection grouped-form pattern"`

---

### Task 12: Refactor SettingRow + toggle row to consume theme

**Files:**
- Modify: `lib/core/widgets/settings_row.dart`
- Test: `test/core/widgets/settings_row_test.dart` (add/extend)

**Interfaces:**
- Produces: `SettingsRow` unchanged public API, but internals read `context.textTheme` / `colorScheme` (no hardcoded `BrandColors`/`fontSize`); a toggle variant uses `SwitchListTile` semantics with `Switch(activeColor: colorScheme.primary)`.

- [ ] **Step 1: Failing test** — pump a `SettingsRow` with a title; assert its title `Text` resolves `DefaultTextStyle`/style fontFamily `JosefinSans` (i.e., it now comes from `titleMedium`), not a hardcoded size. (Assert via `tester.widget<Text>(...).style?.fontFamily`.)
- [ ] **Step 2:** Run → FAIL (currently hardcoded).
- [ ] **Step 3:** Replace inline `TextStyle(fontSize: 15...)`/`BrandColors.*` with `Theme.of(context).textTheme.titleMedium` / `colorScheme` references; route the icon chip color through `colorScheme.primaryContainer`.
- [ ] **Step 4:** Run targeted test + `npm run app:test` → green.
- [ ] **Step 5:** `git commit -m "refactor(widgets): SettingsRow consumes theme"`

---

### Task 13: Native sheets — retire the blurred sheet

**Files:**
- Modify: `lib/core/widgets/inactive_backdrop_scrim.dart` (or delete the blur helper and replace its exports)
- Modify call sites: `lib/features/control/presentation/widgets/color_picker_sheet.dart`, `base_editor_sheet.dart`, `shade_editor_sheet.dart`, `password_prompt_dialog.dart` (swap `showBlurredModalBottomSheet`/`showBlurredDialog` → `showModalBottomSheet(...)` / `showDialog(...)`)
- Test: `test/core/widgets/native_sheet_test.dart`

**Interfaces:**
- Produces: a thin `Future<T?> showAppSheet<T>(BuildContext context, {required WidgetBuilder builder})` wrapping `showModalBottomSheet<T>(context: context, isScrollControlled: true, builder: builder)` (drag handle comes from `bottomSheetTheme`). Remove `BackdropFilter` usage.

- [ ] **Step 1: Failing test** — pump a button that calls `showAppSheet`; tap; assert the sheet content appears AND a drag handle is present (`find.byType(BottomSheet)` / the M3 handle). Then assert swipe-down dismisses it (drag the sheet down, `pumpAndSettle`, content gone).
- [ ] **Step 2:** Run → FAIL (`showAppSheet` undefined).
- [ ] **Step 3:** Implement `showAppSheet`; replace the three editor call sites + the password dialog to use it / `showDialog`; delete the `BackdropFilter` blur path.
- [ ] **Step 4:** Run targeted test + `npm run app:test` (the wisp/control sheet tests must still pass — they tap keyed rows, not the blur) → green. Fix any test that asserted the removed blur widget.
- [ ] **Step 5:** `git commit -m "refactor(widgets): native bottom sheets with drag handle, drop blur"`

---

### Task 14: Consolidate password dialogs

**Files:**
- Modify: `lib/features/lamp_shell/presentation/setup_screen.dart` (`_PasswordDialog`), `lib/features/.../connect_password_prompt.dart` (`_PasswordDialog`) → route through `showPasswordPromptDialog` (`lib/core/widgets/password_prompt_dialog.dart`)
- Test: existing password-flow tests must stay green.

**Interfaces:**
- Consumes: `showPasswordPromptDialog` (already the canonical impl).

- [ ] **Step 1:** Identify the two duplicate dialogs; confirm `showPasswordPromptDialog` covers their needs (obscure toggle, submit-on-enter, error/busy). If `connect_password_prompt` has busy/error states the shared one lacks, add those params to the shared dialog first (with a test).
- [ ] **Step 2:** Replace both call sites with `showPasswordPromptDialog(...)`; delete the two `_PasswordDialog` classes.
- [ ] **Step 3:** Run `npm run app:test` (auth/onboarding/setup password tests) → green.
- [ ] **Step 4:** `git commit -m "refactor(widgets): single password dialog"`

---

## Phase 3 — Core Controls

### Task 15: Warm-white blend math (pure util)

**Files:**
- Create: `lib/features/control/application/warm_white_blend.dart`
- Test: `test/features/control/warm_white_blend_test.dart`

**Interfaces:**
- Produces: `Color blendWarmWhite(Color rgb, int w, {Color warmWhite = Brand.warmWhite})` — screen-blends `warmWhite` scaled by `w/255` over `rgb`. Screen blend per channel: `out = 255 - (255-base)*(255-top)/255`, where `top = warmWhite * (w/255)`.

- [ ] **Step 1: Write the failing test**
```dart
import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lamp_app/features/control/application/warm_white_blend.dart';

void main() {
  test('w=0 returns the base rgb unchanged', () {
    expect(blendWarmWhite(const Color(0xFF200040), 0), const Color(0xFF200040));
  });
  test('w>0 brightens toward warm-white (screen blend)', () {
    final base = const Color(0xFF000000);
    final out = blendWarmWhite(base, 255); // full warm white over black
    expect(out.value, const Color(0xFFFABB3E).value | 0xFF000000);
  });
  test('screen blend never darkens any channel', () {
    final base = const Color(0xFF402030);
    final out = blendWarmWhite(base, 128);
    expect(out.red, greaterThanOrEqualTo(base.red));
    expect(out.green, greaterThanOrEqualTo(base.green));
    expect(out.blue, greaterThanOrEqualTo(base.blue));
  });
}
```
- [ ] **Step 2:** Run `flutter test test/features/control/warm_white_blend_test.dart` → FAIL.
- [ ] **Step 3: Implement**
```dart
import 'package:flutter/material.dart';
import '../../../core/theme/brand.dart';

/// Screen-blend the warm-white channel (intensity `w` 0..255) over [rgb],
/// so the preview shows the physical warm-white pixel's contribution.
Color blendWarmWhite(Color rgb, int w, {Color warmWhite = Brand.warmWhite}) {
  if (w <= 0) return rgb;
  final f = (w.clamp(0, 255)) / 255.0;
  int ch(int base, int top) {
    final t = (top * f).round();
    return 255 - ((255 - base) * (255 - t) ~/ 255);
  }
  return Color.fromARGB(255, ch(rgb.red, warmWhite.red),
      ch(rgb.green, warmWhite.green), ch(rgb.blue, warmWhite.blue));
}
```
- [ ] **Step 4:** Run → PASS.
- [ ] **Step 5:** `git commit -m "feat(control): warm-white screen-blend util"`

---

### Task 16: Brightness control (standard slider)

**Files:**
- Rewrite: `lib/features/control/presentation/widgets/brightness_card.dart`
- Test: `test/features/control/presentation/widgets/brightness_card_test.dart` (extend existing)

**Interfaces:**
- Consumes: `LampCard`, the existing brightness notifier (`controlNotifier`) — keep its commit-on-release behavior.
- Produces: a `LampCard` with `Icon(Icons.brightness_low)` · `Slider` · `Icon(Icons.brightness_high)`, live preview onChanged + commit onChangeEnd (preserve current callbacks).

- [ ] **Step 1: Failing test** — pump the card; drag the `Slider`; assert onChangeEnd commit fires once with the released value (reuse the existing test's notifier fake). Assert it's a Material `Slider` (not the bespoke widget).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Rebuild on `Slider` inside `LampCard` with flanking icons; wire `onChanged` = live preview, `onChangeEnd` = commit.
- [ ] **Step 4:** Run targeted + `npm run app:test` → green.
- [ ] **Step 5:** `git commit -m "feat(control): standard Material brightness slider"`

---

### Task 17: Interval dual-grab RangeSlider

**Files:**
- Create: `lib/core/widgets/interval_range_slider.dart`
- Modify: the expression interval editor that currently uses a bespoke control (locate via `grep -rl interval lib/features/lamp_shell/presentation/expression_editor_screen.dart`)
- Test: `test/core/widgets/interval_range_slider_test.dart`

**Interfaces:**
- Produces: `class IntervalRangeSlider extends StatelessWidget` with `{required RangeValues values, required double min, required double max, required ValueChanged<RangeValues> onChanged, String Function(double)? labelFor}` — a themed `RangeSlider` with `labels` on thumbs.

- [ ] **Step 1: Failing test** — pump with `values: RangeValues(2, 8), min: 0, max: 30`; assert a `RangeSlider` renders; simulate a thumb drag; assert `onChanged` fires with updated `RangeValues` (start ≤ end).
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Implement the `RangeSlider` wrapper; map the expression editor's two interval fields (min/max) to `values.start`/`values.end` and back in onChanged.
- [ ] **Step 4:** Run targeted + the expression editor test (`test/features/lamp_shell/expression_editor_screen_test.dart`) → green; update that test if it asserted the old control.
- [ ] **Step 5:** `git commit -m "feat(control): dual-grab interval RangeSlider"`

---

### Task 18: bpp-aware RGBW color picker

**Files:**
- Modify: `lib/features/control/presentation/widgets/color_picker_sheet.dart`
- Test: `test/features/control/presentation/widgets/color_picker_sheet_test.dart` (create/extend)

**Interfaces:**
- Consumes: `blendWarmWhite` (Task 15); the strip's `bpp` (4 = has white channel, 3 = none) from the picker's existing config/props.
- Produces: picker shows the **W slider only when `bpp == 4`**; the preview swatch shows `blendWarmWhite(rgb, w)` when `bpp == 4`, and plain `rgb` when `bpp == 3`.

- [ ] **Step 1: Write the failing tests**
```dart
testWidgets('4bpp shows W slider; preview reflects warm-white', (t) async {
  await t.pumpWidget(_harness(bpp: 4, initial: /* rgb */, ...));
  expect(find.byKey(const Key('ww-slider')), findsOneWidget);
  // set W high, assert preview swatch color == blendWarmWhite(rgb, w)
});
testWidgets('3bpp hides W slider and preview ignores warm-white', (t) async {
  await t.pumpWidget(_harness(bpp: 3, ...));
  expect(find.byKey(const Key('ww-slider')), findsNothing);
});
```
- [ ] **Step 2:** Run → FAIL.
- [ ] **Step 3:** Gate the W slider on `bpp == 4` (wrap in `if (bpp == 4) Slider(key: Key('ww-slider'), ...)`); compute the preview swatch via `bpp == 4 ? blendWarmWhite(rgb, w) : rgb`; ensure hex round-trip drops the W component when `bpp == 3`. Keep `onLive` streaming + snapshot/rollback intact.
- [ ] **Step 4:** Run targeted + `npm run app:test` → green.
- [ ] **Step 5:** `git commit -m "feat(control): bpp-aware RGBW picker + warm-white preview"`

---

## Phase 4 — Global Re-skin Sweep

> Mechanical, not a redesign. Apply the recipe below per feature folder. Each task = one folder, ends green. The deeper per-page UX rework is a later superpowers cycle.

**Sweep recipe (per file):**
1. `Colors.white.withValues(alpha: ...)` card/panel → wrap content in `LampCard` (or use `colorScheme.surfaceContainer`).
2. `BrandColors.auroraBlue` / other `BrandColors.*` / raw `Color(0x..)` → `Theme.of(context).colorScheme.*` or `Brand.*` token (prefer `colorScheme`).
3. Inline `TextStyle(fontSize: ...)` → `context.textTheme.<role>` (titleMedium for row/card titles, bodySmall/bodyMedium for secondary, labelLarge for section headers).
4. Magic-number `EdgeInsets`/`SizedBox` → `AppSpace.*`.
5. `Colors.redAccent` → `colorScheme.error`; success/online color → `context.brandExtras.success`.
6. Bare section labels → `SectionHeader`; tappable nav rows → `NavRow` where 1:1.

**Gate for every sweep task:** `npm run app:test` stays green + `npm run app:analyze` clean. (These are cosmetic swaps; behavior/keys unchanged, so tests should pass untouched. Where a test asserted a retired bespoke widget or a literal color, update it.)

### Task 19: Sweep `lib/features/control/`
- [ ] Apply the recipe across `control_screen.dart` + `presentation/widgets/*` (base_card, brightness_card already done, shade_card, etc.).
- [ ] Run `npm run app:test && npm run app:analyze` → green.
- [ ] `git commit -m "refactor(control): route through theme + LampCard"`

### Task 20: Sweep `lib/features/lamp_shell/`
- [ ] Apply recipe across `setup_screen.dart`, `info_screen.dart`, `expressions_screen.dart`, `expression_editor_screen.dart`, `advanced_leds_screen.dart`, `home_mode_screen.dart`, `bt_only_lamp_screen.dart`, `add_expression_picker_screen.dart`. Use `FormSection` where a screen has bare grouped rows (mechanical 1:1 only — no IA changes).
- [ ] Run `npm run app:test && npm run app:analyze` → green.
- [ ] `git commit -m "refactor(lamp_shell): route through theme + kit"`

### Task 21: Sweep `lib/features/inventory/` + `social/` + `nearby/` + `wisp/` + `firmware/` + `onboarding/`
- [ ] Apply recipe across these feature folders (`my_lamps_screen`, `social_screen`, `nearby_lamps_screen`, `wisp_config_screen`, `firmware_cache_screen`, onboarding steps). Route Social's bare spinner/empty into `EmptyStatePane`; unify destructive red on `colorScheme.error`.
- [ ] Run `npm run app:test && npm run app:analyze` → green.
- [ ] `git commit -m "refactor(features): route remaining screens through theme + kit"`

### Task 22: Retire dead tokens + final verification
- [ ] Delete `lib/core/theme/brand_colors.dart` once `grep -rn BrandColors lib` returns zero. Remove now-unused brand tokens (`headerLime`, `cloudGrey`, `softGrey`, `auroraBlueHover`, `nameplateGrey`) if unreferenced.
- [ ] Full gate: `npm run app:test` (≥334 green) + `npm run app:analyze` (clean) + `npm run app:install` to eyeball the re-skin on-device.
- [ ] `git commit -m "chore(theme): retire aurora-blue brand_colors + dead tokens"`

---

## Notes for the implementer

- Run everything from `software/lamp-app-flutter` or via the root `npm run app:*` tasks; don't shell raw `flutter` if an npm task exists.
- The three OTA/firmware C++ files the user is editing in the main repo are unrelated — this plan is Flutter-only.
- If a widget test fails after a sweep because it asserted a retired bespoke widget or a literal color, that's expected: update the test to the new widget/token, don't revert the sweep.
- Hidden/advanced affordances (wisp 5-tap, advanced LED config, dev-mode cascades) are intentional — do not surface or alter their gating during the sweep.

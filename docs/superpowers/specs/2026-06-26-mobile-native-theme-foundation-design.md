# Design: Mobile-Native Theme Foundation + Component Kit + Core Controls

**Date:** 2026-06-26
**Surface:** `software/lamp-app-flutter` (the Flutter control app)
**Branch:** `ux-cleanup`

## Problem

The app is "web trying to be mobile." A `ThemeData` (textTheme, cardTheme) is
defined but **never consumed** — there are 0 uses of `Theme.of(context).textTheme`,
188 inline `TextStyle()` constructors, 12 ad-hoc font sizes, and the same card
decoration hand-rolled in 7 places. Surfaces use web-style translucent panels
(`Colors.white.withValues(alpha:0.04)`) instead of solid Material elevation. The
app is built on a retired brand color (aurora-blue `#446C9C`) rather than the
current brand. Several bespoke widgets are Vue-port baggage that reinvent
framework widgets and lose built-in behavior (e.g. custom blurred bottom sheets
that break swipe-to-dismiss).

## Goal

A genuinely native, cohesive, beautiful Material 3 foundation built on the
**current** brand (lamplit.ca/brand), with a single source of truth for color,
type, and spacing, so the whole app re-skins from one place and is trivial to
recolor later. Standardize the form/control patterns the app got wrong.

## Guiding Principle

**Standard Material by default; bespoke only where it genuinely earns it.** The
existing bespoke UX was largely Vue-port baggage and gets retired. The bespoke
that stays: the hardware-coupled RGBW color picker, the critter identity system,
and `StatusDot` — these do something the framework can't.

## Scope

**In this spec:**
1. Theme/token foundation (ColorScheme, typography, spacing, surfaces, component themes).
2. Shared component kit (card, grouped-form system, section header, nav row, refactored rows, native sheets/dialogs).
3. Three core controls (brightness, dual-grab interval range slider, bpp-aware RGBW color picker with warm-white visualization).
4. A global, **mechanical** re-skin sweep that routes existing screens through the new theme/tokens/components (re-skins app-wide; does NOT restructure screens).

**Out of scope (explicitly):**
- Per-page UX/information-architecture/form-grouping redesign — handled later, **each page/section as its own superpowers cycle**.
- Light theme — the brand is **dark-only**.
- Exposing hidden/advanced affordances. The wisp config (5-tap), advanced
  hardware/LED config, and dev-mode expression cascades are **intentional
  audience-gating** for internal/builder users and stay hidden. (Wisp gets its
  own password later — noted, not built here.)
- Reducing app size. Standardizing widgets does **not** cut APK size (Material is
  already linked); the size levers are load-bearing deps. Separate concern.

## Section 1 — Theme Foundation (source of truth)

Restructure `lib/core/theme/`:

- **`brand.dart`** — raw brand tokens, named per lamplit.ca/brand. The ONLY file
  with color hex literals. Source of truth; recolor here.
  - Primary (Pink): Soft Pink `#EFA8F0` (light) / Deep Pink `#C869C8` (dark)
  - Secondary (Yellow): Cream `#FFFDD1` / Golden `#F8CC48`
  - Tertiary (Blue): Lavender `#9EA1FF` / Deep Blue `#6366F1`
  - Quaternary (Green): Light `#BBFFAD` / Deep `#86EFAC`
  - Neutrals: Midnight Black `#0D0D0D` (bg), Carbon Grey `#1A1A1A` (cards), Lamp White `#FDFDFD` (text)
  - Error: Coral `#F87171`
  - Warm-white reference: `#FABB3E` (for the WW channel visualization)

- **`app_color_scheme.dart`** — hand-mapped M3 `ColorScheme.dark`, using each
  brand color's light/dark variants as the M3 tonal pair:

  | M3 role | value | pair |
  |---|---|---|
  | primary | Deep Pink `#C869C8` | container/on tones from Soft Pink `#EFA8F0` |
  | secondary | Golden Yellow `#F8CC48` | Cream `#FFFDD1` |
  | tertiary | Deep Blue `#6366F1` | Lavender `#9EA1FF` |
  | error | Coral `#F87171` | — |
  | surface | Midnight `#0D0D0D` | `surfaceContainer` = Carbon `#1A1A1A`, stepping up for elevation |
  | onSurface | Lamp White `#FDFDFD` | `onSurfaceVariant` ≈ `#CCCCCC` (secondary text) |

  Surfaces use **solid M3 surface-container tones, not opacity overlays** — this
  is what removes the "web-like opacity thing."

- **`brand_extras.dart`** — a `ThemeExtension<BrandExtras>` for roles M3 has no
  slot for: `success` (green `#86EFAC`/`#BBFFAD`, used for online/connected +
  success feedback), the **aurora→pink gradient** for active nav/chrome, and the
  `warmWhite` reference. Accessed via `Theme.of(context).extension<BrandExtras>()`.

- **`app_typography.dart`** — a real type scale mapped to M3 `TextTheme` roles,
  so screens use `context.textTheme.titleMedium` instead of inline styles.
  **Two-font pairing:** **Josefin Sans** for display/headline/title roles
  (brand/headings); **Inter** for body/label/data (dense, readable). Both
  bundled as static weights in `assets/fonts/` + declared in `pubspec.yaml`
  (no runtime fetch). Roles: `displaySmall` (~28, Josefin Sans Bold/700 — Josefin
  tops out at 700, so the current w800 nameplate maps to 700), `titleLarge`/
  `titleMedium` (card+row titles, Josefin SemiBold/600), `bodyMedium`/`bodySmall`
  (12–13 secondary, Inter), `labelLarge` (uppercase section headers). Exact
  per-role sizes/weights finalized during implementation.

- **`app_spacing.dart`** — 4/8pt spacing tokens (`xs=4, sm=8, md=12, lg=16,
  xl=24, xxl=32`) + radius tokens (e.g. `card=12`), replacing magic-number
  `EdgeInsets`.

- **`app_theme.dart`** — `ThemeData(useMaterial3: true)` wiring scheme +
  typography + extension + **component themes**: `cardTheme`, `sliderTheme`,
  `inputDecorationTheme`, `filledButtonTheme`/`textButtonTheme`, `listTileTheme`,
  **`bottomSheetTheme` (with drag handle)**, `snackBarTheme`, `dialogTheme`,
  `chipTheme`. Widgets inherit the look without per-call styling.

## Section 2 — Component Kit

`lib/core/widgets/`:

- **`LampCard`** — the single surface card (Carbon `#1A1A1A`, radius 12, token
  padding) via `CardTheme`. Replaces the 7 hand-rolled opacity panels.

- **Native form-grouping system** (the "mobile not web" fix):
  - **`FormSection`** — a titled group: uppercase `SectionHeader` + a
    `LampCard`-wrapped stack of rows with hairline dividers. The standard mobile
    grouped-settings pattern.
  - **`NavRow`** — one canonical tappable-row-that-navigates (leading icon chip ·
    title · subtitle · chevron), unifying the 3 divergent implementations.
  - **`SettingRow` / toggle row** — refactored to consume the theme (no hardcoded
    colors/sizes); toggle rows use `SwitchListTile` semantics with 48dp targets.
  - **`SectionHeader`** — promote the existing Setup-only uppercase header to
    shared; used everywhere a group starts (Social, Expressions, etc.).

- **Standard inputs/buttons/dialogs** — lean on the component themes (themed
  `FilledButton`/`TextButton`/`TextField`); consolidate the 3 near-duplicate
  password dialogs onto the single shared one.

- **Sheets** — adopt `showModalBottomSheet(showDragHandle: true,
  isScrollControlled: true)` as the standard. **Drop the custom `BackdropFilter`
  blur** (it was the reason the bespoke sheets lost swipe-to-dismiss). Native
  gesture > blur.

- **Keep & lean on (bespoke that earns it):** `StatusDot`, the critter system
  (`critter_icon`), `AppSnackbar`, `EmptyStatePane` — refactored to consume the
  theme, none reinvented.

## Section 3 — Core Controls

- **Brightness** — a standard themed Material `Slider` in a `LampCard`, flanked
  by brightness-low/high icons, value shown, **live preview + auto-commit on
  release** (preserves current behavior). Optional carry-over: the thumb
  color-morph as a themed slider detail.

- **Interval (dual-grab)** — Material **`RangeSlider`** (the real two-thumb
  pattern), mapped to the interval's min/max with value labels on the thumbs,
  themed in brand pink. Reused anywhere a min/max range input is needed.

- **bpp-aware RGBW color picker** — keep the four-slider concept (R/G/B/W on
  themed sliders + hex field + live streaming `onLive` + snapshot/rollback
  session), fixing two things:
  - **Warm-white made honest:** the preview swatch composites the **W channel as
    a screen-blend of the warm-white reference (`#FABB3E`) at the W intensity over
    the RGB base**, so warm-white visibly warms/brightens the preview (matches the
    physical pixel; restores the Vue `ColorPreview` behavior the Flutter swatch
    lost).
  - **bpp-gated:** the **W slider and its preview contribution appear only when
    the strip has a white channel (4bpp); both vanish for 3bpp strips** — no dead
    control, no phantom warmth.

## Section 4 — Global Re-skin Sweep + Structure & Testing

- **Global re-skin sweep (mechanical, not a redesign):** replace hardcoded
  literals app-wide so the brand goes live everywhere:
  - `white@0.04` panels → `LampCard`
  - aurora-blue / `Colors.*` literals → `colorScheme` / brand tokens
  - inline `TextStyle()` → `context.textTheme.*`
  - magic `EdgeInsets` → spacing tokens
  - `Colors.redAccent` → `colorScheme.error`

  This re-skins to pink-primary + solid surfaces and kills the opacity look. The
  thoughtful per-page rework (IA, deliberate `FormSection` grouping, flow fixes)
  is the later superpowers passes — NOT this spec.

- **Structure:** `lib/core/theme/` (brand · app_color_scheme · brand_extras ·
  app_typography · app_spacing · app_theme) + `lib/core/widgets/` (LampCard ·
  FormSection · SectionHeader · NavRow · refactored rows) + the three controls in
  their feature folders.

- **Testing:**
  - The existing **334 widget tests stay green** — the re-skin changes look, not
    behavior/keys, so most survive untouched. Where a test asserts a retired
    bespoke widget or specific text, update it.
  - New unit/widget tests for control *logic*: bpp-gating of the W slider, the
    warm-white blend math, and the RangeSlider min/max mapping.
  - No golden/pixel tests (no infra; would be churn).
  - Sequenced **foundation-first** so every step keeps the app building + tests
    green.

## Sequencing

1. Theme foundation (brand → scheme → typography → spacing → theme + fonts).
2. Component kit (card → section header → form section → nav row → refactored rows → native sheets/dialogs).
3. Core controls (brightness → interval range slider → bpp-aware color picker).
4. Global re-skin sweep (route screens through theme/tokens/components).

Each step keeps the app green; the brand re-skin becomes visible as soon as the
foundation + sweep land.

## Follow-ons (later, separate cycles)

- Per-page/section UX redesign — each its own superpowers spec → plan → build.
- Wisp config password.
- Build-flag hygiene check (`--tree-shake-icons`, `--split-debug-info`) if size
  is ever revisited.

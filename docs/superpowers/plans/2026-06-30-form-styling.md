# Global Form-Styling Standardization — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Make form spacing consistent app-wide (hardcoded px → `AppSpace` tokens, primitives included), convert `advanced_leds` to `FormSection` cards, and document the convention.

**Architecture:** Mechanical spacing sweep against the existing `AppSpace`/`AppRadius` scale (add no tokens), staged primitives-first so the 391-test suite is the regression net; then the one clean grouping conversion; then a convention doc. No behavior changes.

**Tech Stack:** Flutter (Material 3), `flutter test`/`flutter analyze` via `npm run app:test` / `npm run app:analyze` from the worktree root.

## Global Constraints

- **Spacing scale (do NOT add tokens):** `AppSpace` xs=4, sm=8, md=12, lg=16, xl=24, xxl=32; `AppRadius.card=12`. (`lib/core/theme/app_spacing.dart`.)
- **Off-grid → token rule:** 4→xs, 8→sm, 12→md, 16→lg, 24→xl, 32→xxl; **6→sm(8)**, **10→md(12)**, **14→md(12)** *(except `SettingsRow` vertical padding → lg(16), per decision)*, **20→xl(24)** for section gaps / **lg(16)** for in-group field gaps (per-site).
- **Leave as literals WITH a one-line comment** (deliberate, not spacing): hairlines/border widths `1`/`2`, signal-bar widths, `BorderRadius.circular(999)` pills, and component *dimensions* (avatar `36`, icon sizes `18`/`20`/`22`, fixed control `48`).
- **No behavior change** — spacing/padding/radius literals only (and Task 4's structural regroup). Theme tokens only; no hardcoded hex.
- Run `npm run app:test` (baseline 391) + `npm run app:analyze` (no new findings) per task. Commit after each.

> Paths relative to `software/lamp-app-flutter/`.

---

### Task 1: Primitive purity — `SettingsRow` / `SettingsGroupHeading`

**Files:** Modify `lib/core/widgets/settings_row.dart`; Test `test/core/widgets/settings_row_test.dart`.

This widget renders on every settings screen, so land it first as the regression net.

- [ ] **Step 1: Failing test** — add to `settings_row_test.dart`: pump a `SettingsRow`, find its inner content `Container`, assert its `padding == const EdgeInsets.symmetric(horizontal: AppSpace.lg, vertical: AppSpace.lg)`. Fails today (vertical is `14`).
- [ ] **Step 2: Run → FAIL** — `cd software/lamp-app-flutter && flutter test test/core/widgets/settings_row_test.dart`.
- [ ] **Step 3: Implement** — add `import '../theme/app_spacing.dart';`. `SettingsRow` content padding `EdgeInsets.symmetric(horizontal: 16, vertical: 14)` → `horizontal: AppSpace.lg, vertical: AppSpace.lg`. icon→text `SizedBox(width: 14)` → `AppSpace.md`; inner `SizedBox(width: 8|4)` → `AppSpace.sm|xs`. Comment the avatar `width/height: 36` and `Icon size: 18/22` and `border width: 1` as `// deliberate dimension, not spacing`. `SettingsGroupHeading` `EdgeInsets.fromLTRB(16, 24, 16, 8)` → `fromLTRB(AppSpace.lg, AppSpace.xl, AppSpace.lg, AppSpace.sm)`.
- [ ] **Step 4: Run → PASS** + full suite — `flutter test test/core/widgets/settings_row_test.dart && npm run --prefix ../.. app:test`.
- [ ] **Step 5: Commit** — `git commit -m "refactor(widgets): tokenize SettingsRow spacing (rows roomier)"`.

---

### Task 2: Mechanical spacing sweep (clean-grid files)

**Files (Modify):** `firmware/presentation/firmware_update_panel.dart`, `social/presentation/social_screen.dart`, `control/presentation/widgets/{color_picker_sheet,shade_card,base_card}.dart`, `control/presentation/knockout_screen.dart`, `inventory/presentation/my_lamps_screen.dart`, `onboarding/presentation/widgets/{add_lamp_done_step,add_lamp_name_step,add_lamp_scan_step}.dart`, `onboarding/presentation/add_lamp_shell.dart`, `lamp_shell/presentation/{setup_screen,expression_editor_screen,expressions_screen,info_screen,lamp_shell,bt_only_lamp_screen,add_expression_picker_screen}.dart`, `lamp_shell/presentation/widgets/expression_params_panel.dart`, `nearby/presentation/nearby_lamps_screen.dart`.

Pure literal→token swaps per the Global Constraints rule. Apply the off-grid rule where 6/10/14/20 appear (e.g. `expression_editor_screen` `SizedBox(height: 20)` section gaps → `AppSpace.xl`; `add_lamp_*` `6`/`10` → `sm`/`md`). Leave commented exceptions. No structural or behavior change.

- [ ] **Step 1:** Per file, grep its spacing literals (`SizedBox(height:|width:`, `EdgeInsets.`, `BorderRadius.circular(`) and map each via the rule; leave + comment the exceptions.
- [ ] **Step 2: Implement** the swaps, batching ~3 files per commit. Add `app_spacing.dart` import where missing.
- [ ] **Step 3: Verify** after each batch — `flutter analyze` clean; the touched files have no remaining bare spacing literals except commented exceptions (`grep -nE 'SizedBox\((height|width): [0-9]|EdgeInsets\.[a-zA-Z]+\([^)]*[0-9]' <files>`).
- [ ] **Step 4: Full suite** — `npm run --prefix ../.. app:test` green.
- [ ] **Step 5: Commit** each batch — `git commit -m "refactor(<area>): tokenize spacing"`.

---

### Task 3: Needs-care sweep — `wisp_config_screen`, `wifi_network_picker`

**Files (Modify):** `lib/features/wisp/presentation/wisp_config_screen.dart`, `lib/features/lamp_shell/presentation/widgets/wifi_network_picker.dart`. One file per commit.

- [ ] **Step 1:** `wisp_config_screen` — apply the `20` rule per site (section gaps → `xl`, in-group field gaps → `lg`); `10→md`, `6→sm`; **leave** `2` and `circular(999)`. Read each site to classify section-vs-field.
- [ ] **Step 2:** `wifi_network_picker` — `6→sm`, clean-grid → tokens; **leave** the `1`/`2` signal-bar `width`s and `BorderRadius.circular(1|2)` (deliberate bars) WITH a comment.
- [ ] **Step 3: Verify** — `flutter analyze` clean; `npm run --prefix ../.. app:test` green.
- [ ] **Step 4: Commit** each — `git commit -m "refactor(wisp|wifi): tokenize spacing, keep deliberate bars"`.

---

### Task 4: `advanced_leds_screen` → FormSection cards

**Files:** Modify `lib/features/lamp_shell/presentation/advanced_leds_screen.dart`; Test `test/features/lamp_shell/advanced_leds_screen_test.dart`.

**Interfaces:** `FormSection(title, children)` from `lib/core/widgets/form_section.dart` — renders `SectionHeader` + `LampCard(padding: EdgeInsets.zero)` with auto-dividers. **Children get NO inset** — wrap each in `Padding(EdgeInsets.all(AppSpace.lg))` (or use self-padding row primitives).

- [ ] **Step 1: Failing test** — assert two `FormSection`s render (titles "Shade strip" / "Base strip") and the gated byte-order controls still appear when advanced-unlocked / hide otherwise. Fails today.
- [ ] **Step 2: Run → FAIL**.
- [ ] **Step 3: Implement** — group the existing Shade-strip controls (LED-count field, byte-order segmented button, knockout `NavRow`) into a `FormSection('Shade strip', children: [...])` and the Base-strip controls into `FormSection('Base strip', ...)`, each child wrapped to restore inset. Keep the advanced gating + the reboot-cost label exactly as-is.
- [ ] **Step 4: Run → PASS** + full suite + analyze.
- [ ] **Step 5: Commit** — `git commit -m "feat(leds): group advanced LED setup into FormSection cards"`.

---

### Task 5: Convention doc

**Files:** Create `software/lamp-app-flutter/docs/FORM_STYLING.md`; Modify `lib/core/widgets/form_section.dart` (doc-comment); Modify root `CLAUDE.md` if it exists (one-line link).

- [ ] **Step 1:** Write `FORM_STYLING.md` (~30 lines): (1) spacing rule (all via `AppSpace`/`AppRadius`; snap to nearest; add no tokens; commented literal exceptions for hairlines/pills/component-sizes); (2) idiom rule — `FormSection` card = cluster of mixed controls for one config object · `SettingsRow` under `SettingsGroupHeading` = list of homogeneous tap rows · loose = single control / non-form / full-bleed pill selector; (3) build from primitives, not ad-hoc Card/Container.
- [ ] **Step 2:** Add a short doc-comment of the idiom rule above `class FormSection`.
- [ ] **Step 3:** If a root `CLAUDE.md` exists, add a one-line pointer to `FORM_STYLING.md`.
- [ ] **Step 4: Verify** — `npm run --prefix ../.. app:test` + `app:analyze` (docs-only + comment; should be green).
- [ ] **Step 5: Commit** — `git commit -m "docs: form-styling convention (spacing + grouping idioms)"`.

---

## Self-Review

- **Plan coverage:** Phase 1 → Tasks 1–3; Phase 2 → Task 4; Phase 3 → Task 5. Off-grid rule + exceptions in Global Constraints (every task inherits). Row-height decision (14→lg) pinned in Task 1.
- **Risk staging:** primitive (every-screen) change lands first (Task 1) with the suite as net; mechanical bulk batched (Task 2); judgment files isolated (Task 3); the one structural change isolated + tested (Task 4).

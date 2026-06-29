# Design: Control Flow — Save-Model Coherence

**Date:** 2026-06-28
**Surface:** `software/lamp-app-flutter` — the Control screen + its color editors
**Branch:** `ux-pages` (first per-page pass on top of the design-system foundation)

## Problem

The app's persistence model is incoherent and the code lies about it. Reconciled
current behavior:

- **`writeSettingsBlob(payload, {reboot})`** is the real persistence path, called
  **per-setting, immediately on change**: name / SSID / devMode write it with
  `reboot:false` (instant), password / advanced-LED with `reboot:true` (→ `save()`
  → reboot + ~8–12s reconnect, shown as "Saving changes…" via `lampSaveStatus`).
- **`commit()`** (a separate char, RAM→NVS flush) handles **brightness** (live write
  + 500ms debounced auto-commit) and the **color gradient editors** (live-preview +
  explicit **Save** + **silent revert on any other dismiss**).
- The **"global Save Changes" pill is gone** (`actions: const []`), yet the color
  editors and ~6 files still carry comments asserting changes "ride the global Save
  Changes / settings_blob path." That half-migration is the core defect.

Net: almost everything already auto-commits on change; the **color gradient editor
is the lone holdout** (explicit Save, and a *silent* revert on casual dismiss that
throws away a gradient the user watched live-preview on the lamp).

## Goal

One legible model, and code/comments that match it. Make the boundary between
"auto-applies" and "Save/Cancel" obvious, and stop silently discarding gradient work.

## The Model (target)

Two modes, chosen by the *kind* of change:

1. **Direct single-value tweaks** (brightness, name, SSID, expression toggles) →
   **auto-commit on change.** Already how they work; no mechanism change.
2. **Composition editing** (the color gradient editor — you build a strip of
   swatches via gradient pane → picker) → **explicit Save / Cancel.** Cancel
   discards (reverts to the snapshot it opened with). This is correct: the user is
   trying out gradient palettes they may not want to keep.
3. **Reboot-bearing actions** (password, advanced-LED) → auto-apply, but the
   trigger is **clearly labeled before it fires** ("Saving this restarts the lamp
   (~10s)"). The post-reboot "Saving changes…" reconnect message stays.

**Out of scope (deferred to their own passes):**
- Reconnect silent-input-loss (a change made during a BLE drop being dropped on
  reconnect) — touches the connection state machine; separate effort.
- Color-edit flow depth — staying as-is. The gradient-pane-first flow is correct;
  a single-swatch shortcut would have to vanish once a 2nd swatch exists, which is
  a confusing state-dependent entry. Always gradient-pane-first.

## Section 1 — Gradient editor: Save / Cancel / discard guard

Files: `lib/features/control/presentation/widgets/base_editor_sheet.dart`,
`shade_editor_sheet.dart`. Both already snapshot on open (`_originalColors`,
`_originalAc`) and track `_committed`.

- **Explicit `Cancel` + `Save` controls**, visually unmistakable (header or footer).
  - `Save` → commits the gradient (current behavior: set `_committed = true`, persist
    via `commit()` / `writeSettingsBlob` as today, close).
  - `Cancel` → revert the working gradient to the open-snapshot, write the original
    back to the live lamp state, close.
- **Discard guard on gesture-dismiss:** `PopScope(canPop: !hasUnsavedChanges, …)`.
  If the user swipes-down / taps-outside / system-backs with unsaved swatch changes,
  the pop is **blocked** and a **"Discard changes?"** dialog appears:
  - **Discard** → revert to snapshot + close.
  - **Keep editing** → stay in the editor.
  With no unsaved changes, dismiss closes silently (no prompt).
- **Save/Cancel boundary is the gradient editor, not the nested picker.** The color
  picker edits one swatch and feeds it into the *working* gradient; it does not
  persist. The gradient editor's Save/Cancel commits or discards the whole
  composition.
- **Live-preview unchanged** during editing; Cancel/Discard restores the original
  live state (already the behavior).
- `hasUnsavedChanges` := working colors/ac differ from the open-snapshot
  (`_originalColors` / `_originalAc`).

## Section 2 — Rest of the model

- **Single-value tweaks** stay auto-commit-on-change — no mechanism change. Ensure
  nothing in the surrounding UI implies a pending "Save" for them.
- **Reboot-bearing actions** (password, advanced-LED): keep auto-apply, but the
  action that triggers a reboot is **labeled before it fires** — copy to the effect
  of "Saving this restarts the lamp (~10s)." `lampSaveStatus` + the post-reboot
  "Saving changes…" message stay as-is.

## Section 3 — Retire the dead "global Save Changes" scaffolding

Sweep stale comments app-wide that reference a "global Save Changes" / "Save Changes
pill" that no longer exists, rewriting them to describe the actual behavior
(per-setting auto-`writeSettingsBlob`; the gradient editor's Save/Cancel). Known
sites: `wifi_notifier.dart`, `social_screen.dart`, `home_mode_screen.dart`,
`advanced_leds_screen.dart`, `control_notifier.dart`, and the two editor-sheet
docstrings. **Keep `lampSaveStatus`** (still drives the reboot "Saving changes…"
message) — only fix comments that frame it as a global pill. This is comment/doc
cleanup, not a behavior change.

## Section 4 — Testing

- Gradient editor (base + shade):
  - **Save** commits and persists (existing commit/blob path) and closes.
  - **Cancel** reverts to the open-snapshot, restores live state, closes.
  - **Gesture-dismiss with unsaved changes** → discard dialog appears; **Discard**
    reverts + closes; **Keep editing** stays (editor still open, changes intact).
  - **Dismiss with no changes** → closes with no dialog.
- Existing control/editor tests stay green; update any that asserted the old
  silent-revert-on-dismiss to the new explicit Cancel / discard-guard behavior.
- No new golden/pixel tests.

## Notes for the implementer

- This is one page-pass on top of the merged design system; run via the root
  `npm run app:test` / `npm run app:analyze`. Keep the suite green.
- Don't touch the connection/reconnect state machine (reconnect-loss is a separate
  deferred pass).
- The flow depth (color card → gradient pane → picker) is intentional — do not
  add a single-swatch shortcut.

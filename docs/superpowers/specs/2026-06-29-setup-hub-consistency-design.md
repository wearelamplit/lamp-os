# Design: Setup Hub — UX consistency pass

**Date:** 2026-06-29
**Surface:** `software/lamp-app-flutter` — the lamp Setup tab (`setup_screen.dart`)
**Branch:** `ux-pages` (third per-page pass)

## Problem

The Setup hub is structurally fine (a grouped `ListView` of themed `SettingsRow`s,
with advanced/dev rows correctly gated). The friction is in consistency and
consumer-facing language, plus two genuine bugs:

- **Mis-grouped row:** "Boot-time setup AP" renders under the **LEDs** group heading
  (`setup_screen.dart:136–160`) but is a connectivity/recovery feature.
- **Invisible drill affordance:** `SettingsRow` only draws its chevron when there is no
  trailing widget (`settings_row.dart:70`). The Home Mode row has a `Switch` in the
  trailing slot, so its tap-to-configure drill-in has no visual cue.
- **Subtitle inconsistency:** subtitles mix live *status* with *instructions*. Password's
  subtitle is "Tap to change" — noise, since every row is tappable.
- **Engineer-speak in a consumer screen:** "Boot-time setup AP"; "LED setup" subtitle
  exposes byte-order (`Base 40×GRB · Shade 60×GRB`).
- **Icon-weight inconsistency:** visible rows mix outline (`label_outline`, `lock_outline`,
  `home_outlined`) with filled (`memory`, `wifi_tethering`).

## Goal

A consistent, plain-language Setup hub. Hub-only — the gated advanced/dev rows
(`effectiveAdvancedProvider` / `devMode`: Nearby debug, Cached firmwares, Factory reset)
stay exactly as they are, gated.

## Changes

### 1. Fix the mis-grouped row

Move the "Setup hotspot" row (currently "Boot-time setup AP") out from under the **LEDs**
group and under **Connectivity**, directly after the Home Mode row. The **LEDs** group then
contains only "LED setup". Group order stays: Lamp → Connectivity → LEDs.

### 2. Drill affordance on a row that also has a toggle

Give `SettingsRow` an opt-in way to show a drill chevron **alongside** a trailing widget,
so a switch-row that also drills in reads clearly. Add an optional `bool drillChevron`
(default `false`) to `SettingsRow`; when `true` and `onTap != null`, render the trailing
widget AND a `chevron_right` after it. Default behavior is unchanged for every existing
row (chevron still shows only when `onTap != null && trailing == null`). The Home Mode row
sets `drillChevron: true` so its Switch (quick on/off) and the drill-to-configure are both
discoverable.

### 3. Subtitles are status, not instructions

Password row: replace the `"Tap to change"` subtitle with real state from
`state.lamp` / the lamp section's `hasPassword` (`sections.dart:62`, type `bool?`):
- `hasPassword == true` → **"Protected"**
- `hasPassword == false` → **"Open · no password"**
- `hasPassword == null` (legacy lamps predating the field) → no subtitle (omit), so we
  never assert a state we don't know.

Confirm where `hasPassword` lands in `ControlState` before wiring (it is parsed in the
lamp section; surface it on the state the screen already reads). Do not read or display the
password value itself.

### 4. De-jargon two labels

- **"Boot-time setup AP"** → title **"Setup hotspot"**; subtitle when on →
  **"Broadcasts a setup network for 2 min after each power-on"**, when off → **"Off"**.
  (Behavior/toggle unchanged — `setLampWebappEnabled`.)
- **"LED setup"** subtitle drops the byte-order: `Base ${base.px}×${base.byteOrder} ·
  Shade ${shade.px}×${shade.byteOrder}` → **`Base ${base.px} · Shade ${shade.px} LEDs`**.
  The byte-order pickers stay editable (and gated) inside the LED setup screen — only the
  hub *summary* drops `GRB`.

### 5. Icon-weight consistency

Use outline-weight icons for the visible rows. Replace the two filled ones:
- LED setup: `Icons.memory` → `Icons.lightbulb_outline`.
- Setup hotspot: `Icons.wifi_tethering` → `Icons.router_outlined`.
Leave the gated advanced/dev rows' icons as-is (out of scope).

## Testing

`test/features/lamp_shell/setup_screen_test.dart` (extend/create), widget tests over
`SetupScreen` with a seeded `ControlState` via the existing control-test harness:
- **Grouping:** the "Setup hotspot" row renders under the Connectivity heading, not LEDs.
- **Drill chevron:** the Home Mode row shows both a `Switch` and a `chevron_right`; a
  toggle-only row (Setup hotspot) shows a `Switch` and no chevron; a plain drill row
  (LED setup) shows a `chevron_right` and no switch.
- **Password status:** `hasPassword: true` → "Protected"; `false` → "Open · no password";
  `null` → neither string present.
- **De-jargoned labels:** "Setup hotspot" present, "Boot-time setup AP" absent; LED setup
  subtitle shows "Base N · Shade M LEDs" with no "GRB".
- **`SettingsRow` unit:** `drillChevron:true` + trailing + onTap renders trailing AND
  chevron; default (false) keeps the existing chevron-only-when-no-trailing behavior
  (guard against regressing every other row).
- Existing setup/SettingsRow tests stay green; run via `npm run app:test` / `app:analyze`.

## Out of scope

- The sub-panes it drills into (Home Mode pane, LED setup screen) — except the LED
  setup summary text on the hub.
- The advanced/dev gated rows and their gating.
- Any behavior change to toggles/navigation (Home Mode enable-drills-in on first-on stays;
  the only change is making the drill discoverable).

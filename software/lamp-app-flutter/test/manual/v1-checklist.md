# V1 manual test checklist

Hardware verification for things automated tests can't cover. Walk through this before each TestFlight / internal build.

## Setup

- `flutter run --release -d <phone-id>` against a real lamp powered on in the room.

## Phase 2 — Control screen

### Happy path

- [ ] After Adopt, app lands on Control screen with `ConnectingView` showing the critter + "Connecting…" briefly.
- [ ] Control loads with the lamp's current brightness, shade, and base populated (not defaults).
- [ ] Brightness slider visibly changes the lamp's brightness while dragging.
- [ ] Releasing the slider leaves the lamp at the released value (no snap-back).
- [ ] Tapping the Shade card opens the color picker; picking a color + Save updates the lamp's shade.
- [ ] Dismissing the color picker with Cancel leaves the lamp shade unchanged.
- [ ] Tapping the Base card opens the editor sheet.
- [ ] Tapping a stop swatch in the editor opens the color picker; Save updates the lamp's base live.
- [ ] Adding a stop pushes a new white stop; the lamp's gradient reflects it.
- [ ] Reordering stops via drag changes the lamp's gradient in the new order.
- [ ] Removing a stop down to 1 disables the ✕ on the remaining stop.
- [ ] Tapping a stop (not its swatch) marks it as active; the active pink ring jumps to that stop.
- [ ] Backing out of the lamp and re-entering reads the lamp's current state (no stale data).

### Failure modes

- [ ] If the lamp is off / out of range, the screen shows the error message gracefully (centered, fogGrey text) instead of a stack trace.
- [ ] If the lamp loses power mid-session, the next write throws but the UI does not crash.
- [ ] Wrong password on an adopted lamp: writes silently no-op firmware-side; UI still appears to work. Known limitation — file a follow-up if it confuses real users.

### Critter friend

- [ ] Same lamp shows the same critter on every reconnect.
- [ ] Different lamps show different critters (within the set of 4).

## Phase 2.1 — Polish + persistence

### Realtime preview

- [ ] Open shade picker, drag the hue ring → lamp shade changes live while dragging (no need to tap Save).
- [ ] Tap Cancel in the shade picker → lamp reverts to the previous shade.
- [ ] Open base editor, tap a stop, drag in the color picker → that stop on the lamp updates live.
- [ ] Cancel in the per-stop picker → that stop reverts on the lamp.
- [ ] LampPreview critter under the brightness slider mirrors the picker in realtime — its shade matches the chosen shade color, its body shows the base gradient.

### Base editor polish

- [ ] Close (×) icon in the top-right of the Base editor sheet pops the sheet.
- [ ] Sheet height looks proportional to its content (≈60% screen, not full).

### App bar + Save

- [ ] App bar shows the lamp's friendly name (e.g. `jacko`), not the BLE device id.
- [ ] Save icon in the app bar is disabled when no edits have been made.
- [ ] After any change (brightness, shade, base), Save becomes enabled.
- [ ] Tap Save → screen shows ConnectingView for ~5-8s while the lamp fades out, reboots, and re-loads.
- [ ] After Save completes, edits are reflected in the freshly loaded state and Save is back to disabled.
- [ ] Power-cycle the lamp manually → all saved values persist (brightness, base colors, base active, shade).
- [ ] Expressions / Setup tabs do NOT show the Save icon (Control-tab-only for now).

## Phase 2.2 — Resilience (RGBW picker + disconnect handling)

### RGBW slider picker

- [ ] Shade picker shows 4 sliders (R, G, B, Warm White) with the lamp's current values pre-set.
- [ ] Dragging any slider updates the lamp and the LampPreview critter live.
- [ ] Hex string in the picker header updates as you drag.
- [ ] Picker swatch reflects the warm-white channel: pulling W up with RGB=0 turns the swatch toward warm orange; with RGB=255,255,255 the swatch stays white.
- [ ] Pulling Warm White down from 255 (default shade) makes the shade darken / colour become visible on the lamp.
- [ ] Base editor: tap a stop → picker shows R/G/B + Warm White; same behaviour.
- [ ] Cancel still reverts; Save commits the final values and the AppBar's Save action enables.

### Disconnect + auto-reconnect

- [ ] During rapid shade-drag editing the link no longer drops every few seconds (cached-service fix). Run for ≥30s of continuous slider movement.
- [ ] Walk the phone out of BLE range while editing — within a second or two, an amber "Reconnecting…" banner appears at the top of Control.
- [ ] Sliders and pickers stay interactive while disconnected (writes are silently dropped behind the scenes).
- [ ] AppBar Save icon disables while disconnected; tooltip reads "Reconnecting…".
- [ ] Walk back into range → banner clears within a few seconds; the lamp catches up to the last local values without losing the user's session.
- [ ] After reconnect, any unsaved edits from before the drop are still present and Save re-enables.
- [ ] Power-cycle the lamp manually → banner appears on the immediate disconnect, persists through the ~5 s boot, then clears once the lamp re-advertises.

## Phase 1c — Multi-lamp switching

### AppBar lamp chip

- [ ] AppBar shows the lamp's friendly name as a tappable chip with a status dot to its left.
- [ ] The status dot pulses green when connected; dim-green when in BLE range but not the current lamp; grey when out of range.
- [ ] Switching from Control → Expressions → back to Control no longer flashes the ConnectingView — the BLE link is held across tab switches.

### Picker contents

- [ ] Tap the chip → modal bottom sheet slides up showing "Your lamps".
- [ ] Each inventory row shows: status dot, lamp icon tinted by the lamp's last-seen colors, name.
- [ ] The currently-active lamp's row carries an "active" pill (no chevron).
- [ ] Inventory lamps currently within BLE range show a green/bluetooth status dot; others show grey.
- [ ] Power-cycle another lamp in the room → after ~30 s its status dot in the sheet transitions from green to grey (BLE adv staleness window).

### Switching lamps

- [ ] Tap a different inventory row → sheet pops, app navigates to that lamp's Control screen.
- [ ] After switching, the AppBar chip and ControlScreen reflect the new lamp.
- [ ] Switch back → previous lamp's Control state is loaded fresh (a new connect+section read; we don't currently cache other lamps' state).

### Other nearby lamps

- [ ] Bring an unconfigured ("stray") lamp near the phone → it appears under "Other nearby lamps" within seconds with an amber "adopt" pill.
- [ ] Tap it → AddLamp wizard opens (factory-default path; sheet pops).
- [ ] Bring a friend's already-configured lamp near the phone → it appears under "Other nearby lamps" with a green "add" pill.
- [ ] Tap → confirmation dialog → "Add" → lamp lands in inventory + becomes the active lamp. Sheet pops.
- [ ] Tap "+ Adopt a lamp" in the footer → onboarding shell opens.

### Live color cache

- [ ] After editing shade or base on a lamp and backing out without saving, re-opening the picker shows the inventory tile tinted by the *edited* colors (InventoryLamp.lastShadeColor / lastBaseColor cached on every live write).
- [ ] Edits to a warm-heavy color (W > 0, RGB low) render the tile with a warm orange wash, not black — the W byte is blended in via `LampColor.blendedRgb`.

### Persistent seen-lamps

- [ ] Force-quit the app and relaunch — inventory + previously-seen "Other nearby lamps" reappear without needing the lamps in range.
- [ ] A lamp last seen days ago is still in the inventory picker (no auto-eviction in v1).

## Phase 1d — BT-only lamps (non-mesh)

These cover the `lamp_route_resolver` branch where a lamp's adv mfg payload reports `isMesh = false`.

- [ ] A lamp NOT in the mesh (factory-default or wifi-not-yet-configured) routed from the inventory picker opens **BTOnlyLampScreen**, not the full Control screen.
- [ ] BT-only screen shows the `BTOnlyInfoPane` explaining the lamp isn't on the mesh and what's available vs limited.
- [ ] Setting up Home WiFi on the BT-only lamp via Setup → reboot → on next discovery the lamp's adv shows `isMesh = true` and the picker now routes it to the full Control screen.
- [ ] If you open Control directly via deep-link / saved-route for a lamp that's currently advertising `isMesh = false`, the screen redirects to BT-only on first paint.

## Phase 3.1 — Knockout shrink / grow round-trip

Regression test for the `knockoutPixels` vector / `base.px` sync (firmware-side fix in 71415e0).

- [ ] Set `base.px = 35` via Advanced LED settings. Save+reboot.
- [ ] Open Knockout → knock pixel 25 down to 0% → save+reboot → pixel 25 is dark.
- [ ] Set `base.px = 20` → save+reboot. The strip now uses only 20 LEDs.
- [ ] Set `base.px = 35` again → save+reboot → pixel 25 should be at **100% (no knockout)**, not the stale 0% from before the shrink.

## Phase 6 — My Lamps screen

- [ ] My Lamps screen lists every adopted lamp with its critter + last-seen shade/base colors.
- [ ] Tiles re-render after editing on the control screen (within the seen-flush debounce window, ~500 ms).
- [ ] W-channel edits (warm-white shifts) show up on the tile, not as black.
- [ ] In-range lamps show their status dot; out-of-range ones go grey within the BLE staleness window.

## Phase 3 — Knockout (per-LED brightness)

- [ ] Control screen shows a "Knockout · N dimmed" tile below the Base card.
- [ ] Subtitle reads "No pixels dimmed" when the knockout map is empty; "1 pixel dimmed" or "N pixels dimmed" otherwise.
- [ ] Tap the tile → KnockoutScreen opens with the back chevron in the AppBar and title "Pixel Knockout · &lt;lamp name&gt;".
- [ ] One slider row per LED (matches `base.px`); each shows index `#0..#N-1`, slider, percentage.
- [ ] Default pixels show 100%; previously-dimmed pixels show their saved value.
- [ ] Drag a slider → that LED on the lamp dims/brightens within ~100 ms.
- [ ] Footer left side updates to "N edited" as you change pixels.
- [ ] Tap "Reset all" → every slider returns to 100%, footer reads "0 edited", lamp is fully bright.
- [ ] Back, then tap global Save → power-cycle the lamp → knockout state persists.

## Phase 5 — Setup screen (name, home WiFi, MQTT, advanced)

### Lamp name

- [ ] Setup tab shows the lamp's current name in a TextField, pre-populated.
- [ ] Editing the name enables Save in the AppBar.
- [ ] Save → reboot → AppBar chip on Control reflects the new name.

### Home WiFi

- [ ] SSID + Password + Brightness slider visible under "Home WiFi".
- [ ] Password field shows the hint "(unchanged — type to replace)" when a password is set; typing replaces it. Leaving blank preserves the existing password across Save.
- [ ] Setting an SSID + Save → after reboot the lamp connects to that network.
- [ ] Brightness slider changes the home-mode brightness (the value the lamp uses while WiFi STA is connected) and persists across reboot.

### Smart Home (MQTT)

- [ ] MQTT section is hidden until a home SSID is set.
- [ ] Once an SSID is set, MQTT section appears as a collapsible card.
- [ ] Toggle Enabled, set Broker host:port, Save → lamp publishes to the broker (verify in Home Assistant or MQTT Explorer).
- [ ] Password hint behaves the same as Home WiFi (sentinel preserved unless typed).

### Advanced

- [ ] Setup tab shows "Enable advanced settings" button (not the Advanced section) by default.
- [ ] Tap it → Advanced section appears with base bpp / shade bpp segmented controls + Advanced-enabled switch.
- [ ] Flip base bpp to RGB (3) → save → reopen base color picker → Warm White slider is gone.
- [ ] Disable the Advanced switch → Advanced section retracts; Save still enabled while you have unsaved changes.

## Phase 4 — Expressions

### List view

- [ ] Empty state on a fresh lamp: "No expressions yet — tap + to add a Glitch, Pulse, Breath or Shift effect."
- [ ] After adding via the editor, the new expression appears in the list with its type, target ("shade"/"base"/"both"), interval range, and enable toggle.
- [ ] Toggle the Enabled switch → the change writes immediately to the lamp (no Save), and the expression starts/stops within its next interval window.
- [ ] Swipe a tile left → red delete background appears; release → the expression is removed and the lamp stops it.
- [ ] Tap a tile → editor opens pre-populated with that expression.

### Editor

- [ ] "+ Add expression" FAB opens the editor for a new expression with default `breathing` type.
- [ ] Type dropdown shows breathing / pulse / shifty / glitchy.
- [ ] Target SegmentedButton (Shade / Base / Both) updates the draft.
- [ ] Enabled switch toggles the draft state.
- [ ] Tap "+ Add color" → color picker opens; pick a color → swatch added to the row. Tap an existing swatch to edit it. Long-press to remove.
- [ ] Two interval sliders enforce `min <= max` automatically.
- [ ] Parameters JSON field accepts a JSON object with numeric values; invalid JSON shows an error and disables Save / Test.
- [ ] Tap **Test** → lamp runs the configured expression once.
- [ ] Tap **Save** → editor pops; list reflects the new entry; persists across power-cycle.
- [ ] On existing-entry edit, Delete button appears and removes the expression.

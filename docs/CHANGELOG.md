# Changelog

Notable changes per firmware version. The version is the root `VERSION` file;
add an entry here in the **same change** that bumps it. Entries describe what's
new for someone upgrading — what changed for you, not how it was built.
Highlights grouped Added / Fixed / Changed, not every commit.

## 1.2.4

### Fixed
- **Steadier in a crowded room.** In a space full of lamps, a lamp could slowly
  fill its memory and restart itself. The lamp now builds its app-facing lists
  (the nearby-lamps view and the expression catalogue) only while a phone is
  connected, frees them the rest of the time, and caps the nearby list — so a big
  crowd no longer chips away at its memory.
- **A bad save can't wipe your config.** A configuration save is now checked
  before it's stored, so an interrupted or partial write can no longer overwrite
  a good config with a broken one and drop the lamp back to defaults.

## 1.2.3

### Fixed
- **Updating a lamp no longer resets its Web Config.** A lamp that was already
  set up could be treated as brand-new after a firmware update and have its Web
  Config hotspot reset to a short window. An already-named lamp is now
  recognized as configured and keeps its settings across updates.

### Changed
- **The Social page now lists only lamps that are physically nearby** — so it
  reads as who's actually in the room, rather than every lamp on the mesh.

## 1.2.2

### Added
- **Loaf lamp.** A new custom variant: two rings drawn as seamless circular
  gradients of your own colours, with the base ring slowly rotating. When two
  loaf lamps are near each other, their base rings spin up faster as a greeting.
- **Lioness lamp.** A new fixed-install custom variant. Its base carries three
  "lion" zones that each mirror a nearby lamp's colours; when a new lamp shows
  up the lions pulse its colour and the shade glitches to greet it.
- **A "near" marker on the Lamp Network page.** Lamps that are physically
  close now show a small sensor icon, so you can tell them apart from lamps
  only reachable over the mesh.

### Fixed
- **Lamps no longer restart themselves after a while in a crowd.** A slow
  memory leak — the Bluetooth scanner kept a record of every nearby phone and
  wearable it had ever seen, and those addresses rotate constantly — gradually
  filled a lamp's memory until it rebooted, worst in busy, phone-dense spaces.
  Fixed.
- **Steadier around lots of lamps.** Nearby-lamp tracking is lighter on the
  lamp's limited memory and no longer caps the nearby view at 16 lamps, so
  large groups are handled without the memory fragmentation that built up
  before.
- **Continuous Pulse loops right away.** A Pulse set to continuous could take up
  to ~15 minutes to start and wouldn't restart on its own if it ever stopped — it
  now begins immediately and keeps looping.
- **Testing a looping expression actually shows it loop.** Previewing a
  continuous expression (Pulse, Breathing, Spotty) now runs a couple of cycles
  instead of a single one, so you can see the motion before saving it.
- **My Lamps keeps a lamp's colour instead of flashing a black base.** When
  fresh detail is missing it falls back to the last known colour rather than
  showing an empty swatch.
- **Shade and Base always keep their labels** on the colour pickers, including
  on older lamps upgrading from before named surfaces.
- **BLE-only lamps reappear on the Social tab** — the app scans for nearby
  lamps while that tab is open, so lamps that don't gossip over the mesh show
  up again.

### Changed
- **Expression trigger intervals share one 5 minute – 5 hour range** across
  Pulse, Shifty, and Glitchy, instead of each expression capping somewhere
  different.
- **Pulse's easing shapes only the on-strip sweep.** The wave enters and
  leaves the strip at a steady pace and the chosen easing applies to the
  travel across the strip, so an eased Pulse no longer feels rushed.
- **The web config UI drops the easing control** — it duplicated the app's and
  had grown unwieldy; set an expression's easing from the app.

## 1.2.0

### Added
- **A lamp sending firmware to a peer now shows as busy in the app.** It paints
  orange at scan time (no connection needed) with an in-voice note ("Jacko is
  busy teaching lily new tricks"), and a connect that fails against a busy lamp
  says so instead of a generic "out of range".
- **Custom lamps.** An author API for building bespoke lamp behaviours (fleet
  reachability / nearby, peer colour queries, gradients), with the staff lamp as
  a reference build (input layer, per-surface brightness, on-arrival bloom).

### Changed
- **Shimmer now flickers the lamp's own colours** instead of a fixed fire
  palette, so each lamp shimmers in its own identity — a red lamp rides
  maroon → red → yellow, a teal lamp toward bright cyan. Its four styles
  (Twinkle, Coals, Candle, Campfire) each gained their own motion — drift, sway,
  a fast flutter, ember sparks — and a floor that fades cold cells to a
  dark-but-not-off glow. It moved out of advanced mode into the general
  expression list and no longer takes a colour palette (it follows the surface).
- **Lamp names display in consistent casing** regardless of how they were typed.

### Fixed
- **Shifty picks its colour-shift time unpredictably again**, instead of the
  fixed cadence it had regressed to.
- **A lamp named "stray", "snafu", or "staff" keeps its name** instead of being
  reset to unnamed.

## 1.1.8

### Fixed
- **A saved wisp palette or source change reliably reaches the wisp** even when
  a mesh frame drops.

## 1.1.7

### Added
- **Shimmer expression.** A continuous shimmer on one "Style" dial — Twinkle,
  Coals, Candle, Campfire — with cool colors reading as sparkle and warm ones
  as flame. Behind the app's advanced mode.

### Fixed
- **Greetings show on top of a running expression** and ease back out to it,
  instead of being buried by a full-coverage effect.
- **Lamps no longer crash or reboot under heap pressure** — on a phone connect,
  with a busy roster, or during a long app session.
- **Lamps greet the nearest new arrival first.**
- **First-boot color is genuinely distinct per lamp** — same-reel lamps no
  longer collapse to the same color.
- **A transient config parse hiccup no longer factory-resets the lamp**, so a
  one-boot glitch can't lose a name, password, or expressions.
- **The wisp paints a newly-claimed lamp right away**, and take/release survives
  coex without snapping the fleet dark.
- **A quick reboot no longer advertises a stale roster.**

### Changed
- **Wisp status ring crossfades** on Off to painting instead of jumping.

## 1.1.6

### Changed
- **Faster fleet awareness** — lamps notice each other about twice as fast, with
  crowd airtime kept in budget.
- **Spotty motion recentred to Gentle → Dreamy** — the chaotic fast end is gone.
- **Pulse Size is a relative Small–Large scale** (1–10) instead of a pixel count.

### Fixed
- Adopting a lamp no longer silently pre-warms its disposition state.

## 1.1.5

### Added
- **Per-expression opacity + expanded motion.** Every expression gains a
  10–100% opacity control, plus a two-selector motion picker — Direction
  (In / Out / In-Out) and Feel — over eight motion families with live
  sparklines.
- **The wisp paints the whole fleet at once**, so a colour change lands across
  all its lamps together instead of walking them one by one.
- **Firmware auto-installs on connect.** When a lamp connects and a newer cached
  build for its variant+channel is on hand, the app pushes it without a manual
  tap, with a sparkle badge while it runs. Beta lamps can promote to stable.
- **Legacy lamps join the mesh OTA wave** — legacy receivers self-upgrade to a
  signed 1.1.5 over the air, skipping peers too weak to converge.
- **Learning-tricks card** in the Info tab.

### Fixed
- **Manual palette delivery is reliable**, and the editor populates on a fresh
  connect.
- **App→wisp control lands reliably** across a coex blackout; rapid source-pill
  taps no longer flip back.
- **Lamps no longer flap on/off wisp paint under coex loss** — only a genuinely
  vanished wisp releases the hold.
- **Web Config now serves on mesh firmware**, and legacy lamps recover their
  web-UI from a neighbor after a mesh OTA.
- **Deleting an expression with a malformed target now works.**
- **Manual-palette writes go out on Save only**, not on every picker tick.

### Changed
- **Lamp Network collapses Near/Far into one "Mesh" section** — RSSI proximity
  was too noisy for a meaningful split.
- **App is now "Lamp"** on the launcher, with the Lamplet critter icon.
- **Durations display with unit rollover** (`2h 4m`, `1m 30s`) app-wide.
- **Expressions dim under a wisp instead of pausing.**
- **Pulse can sweep as slowly as 20 s** (was 10 s).
- **Removed the wisp claim-range knob** — RSSI at close range wasn't a usable
  distance proxy.

## 1.1.4

### Added
- **Wisp presence, claims, and paint survive a dropped frame**, so a colour or
  claim change isn't lost to a brief radio blackout.

### Fixed
- **A full manual palette reaches the wisp** even as a long write, instead of
  being rejected on the phone.
- **App→wisp control is confirmed and retried** — setting the wisp's palette or
  source waits for its echo and resends on a miss.
- **Firmware version** shows verbatim, a dash when absent, and refreshes on
  connect.

### Changed
- **More reliable mesh broadcasts** across the fleet.

## 1.1.3

### Added
- **Reliable proximity greeting.** A BLE sighting always counts as near, a
  direct ESP-NOW HELLO marks a peer near above an RSSI threshold, and lamps greet
  even while an app holds the BLE connection.
- **Lamp Network screen** — peers split Near / Far, each row showing firmware
  version, channel, identity-color swatch, and an active-wisp glyph.
- **OTA update visibility.** The scanner tags a lamp mid-update "Updating" and
  the Lamp Network screen draws the OTA edge (arrow named with the peer) between
  sender and receiver.
- **Web Config picker** — Disabled / 2 min / 5 min / 15 min / Always on.

### Fixed
- **More reliable mesh delivery under BLE coex.**
- **Wisp paint dropouts** — the wisp keeps each lamp's color alive on a per-lamp
  schedule.
- **Expression color editing** previews just the edited color and pauses the
  running expression on that surface.
- **Wisp ArtNet off-state** — a wisp set Off stops emission instead of driving
  old lamps to the off-color.

### Changed
- **Lamps broadcast their blended identity color** instead of the first gradient
  stop.
- App-adopted lamps default Web Config off; legacy lamps stay on.

## 1.1.2

### Added
- **Firmware update panel** (app). Downloaded-firmware list with per-row delete
  and a newer-than install gate.
- **Nearby lamp surfacing.** Legacy BLE-only lamps that don't gossip now appear
  on the Social tab.
- **Wisp presence live-refresh** — a wisp coming online after the app connects
  appears without an app restart.
- **`wisp:flash:release`** — flash a signed wisp image over USB.

### Fixed
- **Wisp OFF release** frees its lamps promptly instead of stranding one.
- **Wisp source control** no longer flips back to Manual with empty colors.
- **USB re-flash boots the flashed image**, not a previously OTA'd slot.
- **Shifty** directional fills paint edge-to-edge.
- **My Lamps online status** reacts to a connection drop instead of going stale.

### Changed
- **Pulse** enters from off-strip and bunches at each edge.

## 1.1.1

### Added
- **Battery Saver.** A per-lamp brightness ceiling (Saver / Standard / Bright)
  with a battery-life estimate from the lamp's measured draw.
- **Faster OTA over the mesh** — firmware moves in fewer, larger packets with
  less radio contention.
- **OTA channel promotion.** A `-beta` lamp graduates to `-stable` over the air.
- **Only keyed builds can drive another lamp** — cascades and forced greetings
  are rejected from unkeyed firmware.
- **Expression motion modes** — a shared Motion control plus a Continuous mode
  that ping-pongs a wave forever.
- **Per-surface expressions** — target the shade, the base, or both.
- **Gradient shades** — up to six blended color stops along the strip.
- **Social greeting base-gradient.** Lamps exchange colors on greet and render
  the peer's base as a blended gradient; iOS and Android lamps recognize each
  other.
- **Home mode redesign** — network binding is opt-in, with per-expression-type
  and social quiet toggles.
- **Web config UI overhaul** — AP-duration setup, password gate, expressions
  modal, home-mode toggles, adopt-on-save.
- **Gamma-corrected LED output** so colors match the app instead of washing out.
- **Version reporting** shown in the app and web UI.

### Fixed
- **OTA no longer re-offers a declined or cross-variant build on a timer.**
- **A fresh lamp fills into the fleet quickly** so cascades reach it right away.
- **Expression-editor Test preview no longer leaves ghost rows.**
- **No brownout on boot** with the full segment config.

### Changed
- **Quieter idle mesh** — less background chatter, with a boot burst covering
  the initial fill.
- **Per-frame supply-budget power governor** replaces the fixed brightness cap.
- **Expression rework** across Glitchy, Shifty, Breathing, and Spotty, plus a
  universal Whole-strip / Region zone toggle.
- Local flash defaults to unsigned; signing is CI's job.

## 1.1.0

Initial mesh-era release: the ESP-NOW fleet firmware and the Flutter control
app. Everything since builds on this baseline.

### Added
- **ESP-NOW mesh networking.** Lamps discover each other, gossip presence, relay
  across hops, and prune stale peers, with a versioned wire format so
  mixed-version fleets interoperate.
- **Mesh expression cascades.** Triggering an expression on one lamp propagates
  it across the fleet with stagger timing.
- **BLE GATT control service** — real-time brightness/color plus a JSON section
  protocol, app-layer encrypted.
- **Mesh OTA firmware distribution.** Signed binaries move lamp-to-lamp with
  channel / version / variant gating, plus a web and USB installer for first
  flash and recovery.
- **Expressions system.** Per-surface firmware animations the lamp auto-triggers,
  each schema-driven so the app builds its editor from the descriptor.
- **Lamp personality and social greetings.** Each lamp has a SocialMode and
  per-peer disposition; lamps greet with a shade waveform on meeting and dim as
  the room crowds.
- **Wisp infrastructure node** — a Seeed XIAO ESP32-C6 that subscribes to an
  Aurora palette feed, distributes paint over the mesh, and beacons status.
- **Flutter control app** — adoption, color and gradient editing, the
  expressions editor, a firmware panel, and wisp control, all over BLE.

# Changelog

Notable changes per firmware version. The version is the root `VERSION` file;
add an entry here in the **same change** that bumps it. Highlights grouped
Added / Fixed / Changed, not every commit.

## 1.2.0

### Added
- **Lamps sourcing firmware to a peer now show as busy in the app.** A lamp
  distributing an OTA over the mesh is hard to reach over BLE (the OFFER and
  chunk bursts starve the connect), which read as an unexplained connect
  failure. The lamp now flips a new capability bit in its BLE advertisement
  while distributing, so the app paints it busy (orange) at scan time with no
  connection needed, and explains the wait in-voice ("Jacko is busy teaching
  lily new tricks"). The receiver's name is resolved from the connected lamp's
  nearby data when available; the scan-only view falls back to "a lamp". A
  connect that fails against a lamp last seen busy surfaces the same whimsical
  message instead of a generic "out of range" error. Advertisement-only, no
  mesh protocol or GATT schema change.

## 1.1.8

### Fixed
- **App→wisp control ops now survive a dropped frame.** Forwarding a
  `setManualPalette` / `setSource` from the paired lamp to the wisp was a single
  unacked unicast; on the C6 wisp's bursty RX-scan a lone dropped frame silently
  lost the op, so a saved palette could never reach the wisp. The lamp now
  re-sends the op a few times spaced apart (each copy still unicast and
  MAC-acked, collapsed by the wisp's dedup), so it lands even when the first
  frame is dropped.

## 1.1.7

### Added
- **Shimmer expression.** A continuous shimmer on one "Style" dial — Twinkle,
  Coals, Candle, Campfire — with cool colors reading as sparkle and warm ones
  as flame. Behind the app's advanced mode.

### Fixed
- **Social greetings show over any expression.** The greeting composites on top
  of the running expression and eases back out to it, instead of being buried by
  a full-coverage effect.
- **Lamps no longer reboot on a phone BLE connect** under heap fragmentation.
- **Greeting no longer churns the heap every frame** — an allocation-free
  in-place scan for the best ungreeted arrival replaces a per-tick
  roster-snapshot-and-sort; social now greets the nearest ungreeted peer first.
- **Roster sort no longer crashes on a fragmented heap** — in-place `std::sort`
  with a MAC tiebreaker.
- **BLE section pager no longer fragments the heap during an app session** — the
  `exprcat` catalog pages straight from its static buffer.
- **First-boot color is genuinely distinct per lamp** — same-reel lamps no
  longer collapse to the same color.
- **Config no longer factory-resets on a transient parse failure**, so a
  one-boot hiccup can't cement into a lost name, password, or expressions.
- **Wisp paints a newly-claimed lamp right away**, and take/release survives
  coex without snapping the fleet dark.
- **Random HELLO boot sequence**, so a quick reboot no longer advertises a stale
  roster version.
- **WiFi modem sleep is held off for ESP-NOW receive**, closing a wisp-paint and
  roster-staleness gap after the config AP comes down.

### Changed
- **Wisp status ring crossfades** on Off to painting instead of jumping.

## 1.1.6

### Changed
- **Faster fleet awareness.** The lamp HELLO interval drops from 60 s to 30 s,
  with density-adaptive relay suppression so crowd airtime stays in budget.
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
- **Declarative wisp paint.** The wisp broadcasts one full-fleet paint frame the
  lamps converge to, replacing per-lamp unicast paint walks.
- **Firmware auto-installs on connect.** When a lamp connects and a newer cached
  build for its variant+channel is on hand, the app pushes it without a manual
  tap, with a sparkle badge while it runs. Beta lamps can promote to stable.
- **Legacy lamps join the mesh OTA wave** — legacy receivers self-upgrade to a
  signed 1.1.5 over the air, with an RSSI floor skipping peers too weak to
  converge.
- **Learning-tricks card** in the Info tab.

### Fixed
- **Reliable manual-palette delivery** — the palette serves on its own page
  section and the editor populates on a fresh connect.
- **App→wisp control lands reliably** across a coex blackout; rapid source-pill
  taps no longer flip back.
- **Lamps no longer flap on/off wisp paint under coex loss** — the freshness
  failsafe widened so only a genuinely vanished wisp releases the hold.
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

### Changed
- **ESP-NOW radio rate → 1 Mbps across the board.** 1M DSSS is the robust rate
  for unacked broadcast beacons.

### Added
- **Wisp broadcast redundancy.** Presence / claim / paint broadcasts resend a
  few times ~40 ms apart so a copy survives a coex loss window; receivers dedup.

### Fixed
- **Manual-palette writes reach the wisp regardless of MTU** (long writes), so a
  full palette isn't rejected on the phone.
- **App→wisp control ops are confirmed-and-retried** — a set of the wisp's
  palette or source waits for its status echo and resends on a miss.
- **Firmware-version display** shows a reported version verbatim, a dash when
  absent, and refreshes on connect.

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
- **Mesh reliability under BLE coex.** Unacked broadcasts re-broadcast on spaced
  per-type resend rings so a copy clears the radio's RX-scan gaps.
- **Wisp paint dropouts** — the wisp keeps each lamp's color alive on the
  reliable unicast channel on a per-lamp schedule.
- **Expression color editing** previews just the edited color and pauses the
  running expression on that surface.
- **Wisp ArtNet off-state** — a wisp set Off stops emission instead of driving
  old lamps to the off-color.

### Changed
- **Identity color on the wire** — lamps broadcast and cache their blended
  identity color instead of the first gradient stop.
- App-adopted lamps default Web Config off; legacy lamps stay on.

## 1.1.2

### Added
- **Firmware update panel** (app). Downloaded-firmware list with per-row delete
  and a newer-than install gate.
- **Nearby lamp surfacing.** The passive BLE scan cache reveals legacy BLE-only
  lamps that don't gossip, on the Social tab.
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
- **ESP-NOW v2 mesh frames.** Larger frames (up to ~1470 B) with per-target OTA
  chunk-size negotiation and both-sided RSSI-gated offers, so OTA moves in fewer
  packets with less coex contention.
- **OTA channel promotion.** A `-beta` lamp graduates to `-stable` over the air.
- **Mesh command-auth.** HMAC-SHA256 tag on the "force another lamp" frames so
  unkeyed builds can't drive cascades or greetings.
- **Expression motion modes** — a shared Motion control plus a Continuous mode
  that ping-pongs a wave forever.
- **Per-surface expressions** — target the shade, the base, or both.
- **Gradient shades** — up to six blended color stops along the strip.
- **Social greeting base-gradient.** Lamps exchange colors on greet and render
  the peer's base as a blended gradient; iOS and Android lamps recognize each
  other via a self-reported BD_ADDR identity.
- **Home mode redesign** — network binding is opt-in, with per-expression-type
  and social quiet toggles.
- **Web config UI overhaul** — AP-duration setup, password gate, expressions
  modal, home-mode toggles, adopt-on-save.
- **Gamma-corrected LED output** so colors match the app instead of washing out.
- **Version reporting** derived from the root `VERSION` file, shown in the app
  and web UI.

### Fixed
- **OTA re-offer loop** — unverifiable and declined cross-variant offers are
  rejected up front and blocklisted instead of re-offered on a timer.
- **Post-boot roster dead zone** — a fresh lamp bursts HELLOs for its first 30 s
  so cascades can reach it quickly.
- **Expression-editor ghost rows** — the Test preview no longer writes to NVS.
- **Brownout on boot** with the full segment config.

### Changed
- **Mesh HELLO cadence** slowed to 60 s (roster prune at 240 s), cutting idle
  chatter; the boot burst covers initial fill.
- **Per-frame supply-budget power governor** replaces the fixed brightness cap.
- **Expression rework** across Glitchy, Shifty, Breathing, and Spotty, plus a
  universal Whole-strip / Region zone toggle.
- Mesh wire-format core extracted to a shared library shared by lamp and wisp.
- Local flash defaults to unsigned; signing is CI's job.

## 1.1.0

Initial mesh-era release: the ESP-NOW fleet firmware and the Flutter control
app. Everything since builds on this baseline.

### Added
- **ESP-NOW mesh networking.** Lamps discover each other, gossip presence over
  HELLO beacons, relay frames across hops with per-type dedup, and prune stale
  peers. A versioned wire format carries a receive range so mixed-version fleets
  interoperate.
- **Mesh expression cascades.** Triggering an expression on one lamp propagates
  it across the fleet with stagger timing.
- **BLE GATT control service** — a frozen positional attribute layout exposing
  real-time brightness/color plus a JSON section protocol, authenticated at the
  app layer with AES-GCM.
- **Mesh OTA firmware distribution.** Signed ed25519 binaries move lamp-to-lamp
  with channel / version / variant gating, plus a web and USB installer for first
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

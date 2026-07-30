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

Consolidates every post-1.1.6 bench iteration into one release. Highlights:

### Added
- **Shimmer expression (formerly the fire flicker).** A soft continuous shimmer
  spanning twinkling sparkles to a flickering flame, on one "Style" dial:
  Twinkle, Coals, Candle, Campfire. Cool colors read as sparkle, warm
  ones as flame. Per-pixel heat turbulence plus a global gust and occasional
  bright embers; gated behind the app's advanced mode via an additive `advanced`
  catalog field (no protocol or schema-version change).

### Fixed
- **BLE section pager no longer fragments the heap during an app session.** The
  `exprcat` catalog (~9.6 KB) now pages straight from its immutable static buffer
  instead of being copied into a per-connection buffer that stayed allocated for
  the connection's life, punching a pinned 9.6 KB hole in the middle of the heap.
  App-visible bytes and the chunked-read protocol are unchanged.
- **Social greetings now show over any expression.** The greeting composites on
  top of the expression band instead of beneath it, so it fades in over whatever
  is running (including a continuous full-coverage shimmer) and eases back out to
  the live effect. Previously a continuous expression buried the greeting and
  leaked its peer color as a dim tint.
- **Lamps no longer reboot on a phone BLE connect.** Uncaught `std::bad_alloc`
  from large contiguous nearby / exprcat / dispositions JSON builds under BLE
  heap fragmentation is caught and heap-gated (graceful last-good serve).
- **Roster sort no longer crashes on a fragmented heap.** `getNear` moved off
  `std::stable_sort` (temp merge-buffer allocation) to in-place `std::sort` with
  a MAC tiebreaker.
- **Greeting no longer churns the heap every frame.** The social and snafu
  greeting paths snapshotted and sorted a ~5 KB roster copy (two allocations) on
  every control tick; replaced with an allocation-free in-place scan for the best
  ungreeted arrival, removing a fragmentation driver and a BLE-path bad_alloc
  crash vector. Social now greets the nearest ungreeted peer first, where it
  previously greeted the farthest.
- **WiFi-state read hardened.** The `wifi::lastError()` cross-core string race is
  locked, and the BLE wifi-state read and notify are wrapped in a bad_alloc guard
  so an out-of-memory on the host task degrades to an empty read instead of
  aborting.
- **Config no longer factory-resets on a transient parse failure.** A present-
  but-unparseable config blob serves RAM defaults without overwriting NVS, so a
  one-boot hiccup cannot cement into a permanent loss of name, password, or
  expressions.
- **First-boot color is genuinely distinct per lamp.** The MAC is hashed
  (FNV-1a) before seeding the hue RNG, so same-reel lamps no longer collapse to
  the same color.
- **The web-config softAP is torn down on a BLE connect**, so the WiFi stack no
  longer fragments the heap and starves the connection.
- **OTA offer bounds its chunk count** before sizing the tracking bitmap, so a
  forged or replayed offer (tiny chunk size, huge length) cannot trigger a giant
  allocation and reset every lamp in range.
- **Wisp Aurora decompression is bounded to its output budget**, so a crafted
  compressed frame cannot overflow the C6 heap and reset the wisp.
- **Wisp paints a newly-claimed lamp right away** on the 2 s claim cadence
  instead of a separate 5 s poll.
- **Wisp take / release survives coex** (a short re-broadcast burst after a
  source edge) and no longer snaps the fleet dark on release.
- **Random HELLO boot sequence**, so a quick reboot no longer leaves a lamp
  advertised at its stale pre-reboot roster version.
- **WiFi modem sleep is held off for ESP-NOW receive**, closing an intermittent
  wisp-paint and roster-staleness gap after the config AP comes down.

### Changed
- **Wisp status ring crossfades** on Off to painting instead of jumping; the
  `[wispcoex]` / `[wispstate]` meters drop the invalid seq-gap loss figure.
- **Internal hardening:** HELLO relay suppressor portMUX-guarded, wisp STATE
  burst cannot respin on a zero-length build, the paint distributor sheds a
  per-tick allocation, flicker wind math is factored into a tested header, and
  the firmware distributor's per-peer skip log is throttled (debug builds).

## 1.1.6

### Changed
- **Faster fleet awareness.** The lamp HELLO broadcast interval drops from 60 s
  to 30 s so roster/presence updates propagate twice as fast. Added airtime
  stays well within the ESP-NOW budget at bench scale, and the 240 s prune
  window still tolerates 8 missed beacons.
- **HELLO relay suppression (density-adaptive).** A lamp defers relaying a
  first-seen HELLO for a short jittered window and skips its own rebroadcast
  once it has heard enough neighbors relay the same beacon — cutting the
  N²/interval gossip airtime at crowd density while still relaying (preserving
  coverage) where the fleet is sparse. Receive-side only, no wire/protocol
  change; a `LAMP_DEBUG` `[hellosupp]` counter reports the per-window
  suppression rate.
- **Spotty motion recentred to Gentle → Dreamy.** The Speed slider drops its
  chaotic fast/flicker end; the whole range now lives in gentle territory —
  from ~2 s fades (Gentle) to slow ~45 s drifts (Dreamy) — and is relabeled
  accordingly.
- **Pulse Size is a relative Small–Large scale.** Size becomes a 1–10 relative
  control rather than an absolute pixel count.

### Fixed
- **Adopt copy no longer implies dispositions auto-warm.** Adopting a lamp no
  longer silently pre-warms its disposition state.

## 1.1.5

### Added
- **Per-expression opacity + expanded motion.** Every expression gains an
  opacity control (10–100%, grouped with motion in the editor's
  "Motion & Appearance" card) that rides the same per-layer scale the wisp
  dim uses. The app's motion control is a two-selector picker — **Direction**
  (In / Out / In-Out) and **Feel** (the curve, shown as live sparklines that
  redraw per direction) — over eight motion families (Linear, Smooth, Snap,
  Float, Overshoot, Spring, Bounce, Random). Overshoot-family curves that
  exceed `[0,1]` are clamped at the narrowing consumers so they can't wrap.
- **Declarative wisp paint (`MSG_WISP_STATE`).** The wisp broadcasts a
  full-fleet paint frame the lamps converge to, replacing per-lamp unicast
  paint walks (O(N) → O(1)); lamps render it through a reversible eased-opacity
  layer stack.
- **Reliable manual-palette delivery.** The palette is served to the app on a
  dedicated `wisppalette` page section instead of the notify-churned
  `CHAR_WISP_STATUS`, whose frequent palette-less NOTIFY shared one
  characteristic value with the palette-full read and clobbered it. The editor
  now populates on a fresh connect.
- **Firmware auto-installs on connect.** When a lamp connects and a newer
  cached build for its variant+channel is on hand, the app pushes it without a
  manual tap, with a sparkle badge on the Info tab while the update runs.
  Push eligibility mirrors the firmware's own `otaAcceptable` rule, so the app
  offers exactly what the lamp will accept — a beta lamp takes an equal-version
  stable image and promotes to stable. Auto-download pulls the stable channel
  only; beta lamps promote on the next push.
- **Legacy lamps join the mesh OTA wave.** Legacy (`catch_ota`) receivers now
  self-upgrade to a signed 1.1.5 over the air. Streaming holds the 30 ms chunk
  baseline the legacy receiver's 10-minute OTA budget expects, a serve-once
  coordination gate stops two senders re-erasing the same receiver mid-transfer,
  and a −92 dBm RSSI floor skips peers too weak to converge a full image.
- **Learning-tricks card** in the Info tab — a whimsical primer on what the
  lamps can do.

### Fixed
- **OTA busy-state covers the verify window.** A lamp reports busy through the
  post-OTA verify phase, closing a gap where it briefly looked idle mid-update.
- **OTA source pill names its target.** When you're connected to the lamp
  that's distributing an update, its Lamp Network pill now shows `→ <peer>`
  (name + critter) instead of a bare arrow — the lamp reports its own OTA
  target, matching what peers already show.
- **Expression catalog no longer OOM-crashes at boot.** The catalog JSON grew
  with the expanded motion/opacity params, and serializing it into a
  doubling-growth `std::string` momentarily needed ~2x its size contiguous —
  on the beta channel (OTA distributor active, tighter boot heap) that tipped
  the standard variant into `bad_alloc` → boot loop. The serializer now
  reserves the measured size up front, a single right-sized allocation.
- **Config save no longer OOM-crashes.** The full-config serialize on save
  (`persistConfig` and the commit-drain hash) reserves its measured size and
  guards `bad_alloc`, so a save under tight heap retries instead of crashing —
  the same class as the catalog-boot fix above.
- **Social list no longer bleeds a greet highlight onto the wrong lamp.** Rows
  carry a stable per-lamp key, so an RSSI-driven re-sort can't hand one peer's
  flash/highlight to whichever row lands in its slot.
- **App→wisp control lands reliably.** After any change the wisp re-broadcasts
  its status in a ~20 s burst, and the app widens its confirm window and
  resends across it, so a source toggle or palette edit doesn't silently miss a
  coex blackout. Rapid source-pill taps no longer flip back — a superseded op
  stops resending instead of a late copy landing after a newer one.
- **Lamps no longer flap on/off wisp paint under coex loss.** A lamp holds the
  wisp's paint (`MSG_WISP_STATE`) behind a freshness failsafe; at its old 16 s
  width a run of coex-dropped STATE frames could age the paint out and briefly
  drop the lamp to its autonomous scene before the next frame snapped it back.
  Widened to 60 s, so only a genuinely vanished wisp (unplugged / out of range)
  releases the hold — a real color change or an Off still lands immediately via
  the event-driven frame, unaffected by the window.
- **Zone-slider preview shows over an active wisp.** Editing an expression's
  zone while a wisp painted the lamp's base left the zone marker invisible — it
  composites below the wisp layer and never told the wisp to yield. It now
  yields the painted surface for the duration of the preview, the same
  operator-editing gate the color editor already uses.
- **Deleting an expression with a malformed target now works.** The remove
  guard rejected any target outside 1–3, so an entry carrying a bad or legacy
  target value was permanently undeletable — the delete silently no-op'd and
  the expression reappeared on reconnect. Remove now matches the stored
  (type, target) exactly regardless of the value.
- **Manual-palette writes go out on Save only**, not on every color-picker
  tick, so editing no longer floods the mesh.
- **Shuffle preview** spins the shuffle icon while the re-rolled colors
  propagate back over the mesh, instead of silently showing stale swatches.
- **Web config UI stays reachable during home-mode WiFi scans.** Background
  scans are suppressed while a webapp client is connected, so the scan's
  channel hop no longer stalls the softAP HTTP server.
- **Web Config now serves on mesh firmware.** BLE is torn down while a config
  client is connected, freeing enough heap for the page to render (it
  OOM-rebooted before); BLE is restored by reboot when the session ends.
- **Legacy lamps recover their web-UI after a mesh OTA.** Firmware-only mesh
  OTA never carries the SPIFFS web-UI, so a legacy lamp that self-upgraded
  served a blank config page. The silent-FS auto-heal meant to re-push the UI
  from a neighbor was gated on the config AP merely being *up* rather than a
  client being connected — and lamps keep the AP up, so a seed was perpetually
  blocked from offering. It now gates on an active client, so a needy lamp
  pulls the current UI off any up-to-date peer over the mesh, no app or USB.

### Changed
- **Lamp Network collapses Near/Far into one "Mesh" section**, and the Near/Far
  label is dropped from the Social tab — RSSI proximity was too noisy to be a
  meaningful near/far split.
- **App is now "Lamp"** on the launcher, with the Lamplet critter as its icon.
- **Durations display with unit rollover** (`2h 4m`, `1m 30s`) across the app
  and web UI, via one shared formatter, instead of a flat single unit.
- **Expressions dim under a wisp instead of pausing**, easing back in lockstep
  with the wisp fade on release.
- **Pulse can sweep as slowly as 20 s** (was 10 s) — the Pulse speed slider's
  slow end doubles for a longer, gentler wave.
- **Pulse Size is a Small…Large 1–10 scale** (floor 25%, default 50%),
  replacing the confusing percentage where 100% was only half the strip.
  Old or absent values fall to the default.
- **Removed the wisp claim-range knob.** RSSI at close range is too noisy to be
  a distance proxy, so the Close/Camp/Stage/Wide floor was misleading; the wisp
  now claims any lamp it directly hears (inter-wisp arbitration unchanged).

## 1.1.4

### Changed
- **ESP-NOW radio rate → 1 Mbps across the board.** Lamps drop from 2M to 1M
  (the wisp was already 1M). 1M DSSS is the robust rate; broadcast beacons get
  no MAC ACK/retry so robustness beats airtime, and BLE coex is slot-scheduled
  so a faster rate frees no BLE time.

### Added
- **Wisp broadcast redundancy.** The wisp resends each presence / claim / paint
  broadcast spaced ~40 ms apart (HELLO 3×, CLAIM/PAINT 2×) so a copy survives a
  coex loss window; receivers dedup the copies to a single apply.
- **Debug diagnostics.** LAMP_DEBUG builds log wisp heap (`[wispheap]` free +
  largest block) per status emit and loop stalls (`[wisploop]` > 100 ms), plus an
  LED snap detector (`[ledsnap]`) flagging abrupt frame-to-frame color jumps.

### Fixed
- **Manual-palette writes reach the wisp regardless of MTU.** Wisp-op writes use
  long writes (prepare/execute), so a full manual palette isn't rejected on the
  phone for exceeding a small negotiated ATT MTU. A source op fits one ATT write
  and landed; a larger palette did not, so the color never left the phone.
- **App→wisp control ops are confirmed-and-retried.** Setting the wisp's manual
  palette or source (Off / Manual / Aurora) now waits for the wisp's status echo,
  resends on a miss, and surfaces failure — instead of a fire-and-forget write
  that silently dropped, leaving the wisp painting a stale color or ignoring Off.
- **Firmware-version display.** A lamp that reports a version shows it verbatim; a
  lamp that reports none shows a dash. The connected lamp's version refreshes on
  connect, and the wisp manual-palette swatch bar updates live on edit.

## 1.1.3

### Added
- **Reliable proximity greeting.** A BLE sighting always counts as near; a
  direct ESP-NOW HELLO also marks a peer near once its RSSI clears a threshold.
  Lamps now greet even while an app holds the BLE connection; the extrovert
  greeting cooldown floor is 30 s.
- **Continuous passive neighbor scan.** Lamps run a single uniform low-duty
  (1.5%) BLE scan with jittered advertising, so legacy BLE-only lamps surface
  for greeting whether or not an app is attached, without a periodic high-duty
  burst blacking out ESP-NOW reception.
- **Lamp Network screen.** The old "Mesh lamps" screen is now "Lamp Network";
  peers split Near / Far off the firmware's proximity flag (debug screen and
  Social tab), and each peer row shows its firmware version and channel
  (e.g. "1.1.3 · standard-beta"). The connected lamp is pinned at the top as
  "this lamp", each peer carries a small identity-color swatch, and an active
  wisp shows a glyph.
- **OTA update visibility.** The My Lamps scanner tags a lamp mid-update
  "Updating"; the Lamp Network screen draws the OTA edge between lamps (an arrow
  labelled with the peer name on the firmware row, distinct send vs receive
  colors). A sending lamp advertises the receiver over the mesh (a HELLO
  `OTA_SENDING_TO` TLV names it), and the app derives the receive side, which is
  otherwise HELLO-silent during its own update.
- **Web Config picker.** The app's old "Setup hotspot" control is now
  "Web Config", a single picker with options Disabled / 2 min / 5 min / 15 min /
  Always on.

### Fixed
- **Mesh reliability under BLE coex.** Unacked broadcasts (`CONTROL_OP`,
  `COMMAND`, `COLOR_QUERY`/`COLOR_INFO`) re-broadcast on spaced per-type resend
  rings so a copy clears the radio's RX-scan gaps, with the receive-side dedup
  rings sized to absorb the extra copies, and a MAC-seeded HELLO boot phase
  desyncs fleet-wide power-on bursts.
  The command resend ring covers a 384 B frame (358 B payload) so a rich cascade
  payload replays instead of sending once. Fan-out ring sizing (10 slots) is
  observability-gated: a debug warning fires when a command frame overflows the
  ring, and the slot count only grows if that fires with more than 10 real peers.
- **Wisp paint dropouts.** Lamps no longer intermittently drop the wisp's
  manual-mode color and slowly recover. The wisp keeps each lamp's color alive by
  re-sending the paint on the reliable unicast channel on a per-lamp schedule,
  instead of relying on the unacked 2 s presence beacon.
- **Wisp radio reliability.** Paint keep-alive retries on a send failure,
  `CONTROL_OP` re-broadcasts so an Off actually sticks, and ESP-NOW TX churn is
  cut across the paint walks.
- **Expression color editing.** Picking colors for an expression now previews
  just that color; the running expression pauses on the edited surface instead of
  drawing over the preview (the editor opens the same edit session the main
  colors tab uses).
- **Expression color snap at cycle end.** Removed the breathing sections/stagger
  primitive that caused the snap.
- **Greeting yields to the color editor.** A running greeting no longer
  overpaints the color-edit preview.
- **Firmware cache no-downgrade.** Syncing the firmware inventory no longer
  overwrites a newer manually-pushed cached build with an older GitHub release.
- **Wisp ArtNet off-state.** A wisp with its source Off stops ArtNet emission
  instead of driving old lamps to the off-color; they hold their last frame.
- **Graceful OTA quiet-mode handoff.** The shade eases into OTA quiet mode
  instead of snapping, so a receiving lamp doesn't flicker as its update starts.
- **Onboarding "Continue"** respects the bottom safe-area inset.

### Changed
- **Wisp full-set CLAIM/PAINT.** The wisp broadcasts its full claim/paint set
  each cadence (the rotating-window scheme is dropped); the peer-claim aging
  window is 20 s, 5× the 4 s CLAIM cadence, so a wisp survives ~5 missed CLAIMs
  without spurious failover. Wisp ESP-NOW TX runs at the 1M PHY rate, and the
  recv-task pending slots are statically allocated off the loop stack.
- **Identity color on the wire.** Lamps broadcast and cache their blended
  identity color (webui favicon and nameplate, BLE inventory) instead of the
  first gradient stop.
- App-adopted lamps default Web Config off; legacy lamps stay on.
- The connecting screen's bouncing critter is seeded by the lamp's identity so it
  matches that lamp.
- Wisp settings/sources UI restyled to the expressions-editor form convention;
  full-width LED-type and battery-saver pickers on the LED setup screen.
- Dev-mode toggle moved from the Info screen to Setup's Debug group.
- Dropped the expressions swipe-to-delete undo snackbar (the confirm dialog
  already guards accidental deletes).

## 1.1.2

### Added
- **Firmware update panel** (app). Downloaded-firmware list with per-row delete
  and a newer-than install gate, so it only offers a genuinely newer build.
- **Nearby lamp surfacing.** The continuous passive BLE scan cache (surfaced on
  the Social tab) reveals legacy BLE-only lamps that don't gossip; they also get
  their own section on the Mesh-lamps debug screen.
- **Wisp presence live-refresh.** A wisp that comes online after the app connects
  now appears without an app restart.
- **Range-slider minimum gap.** Range-pair sliders can enforce a minimum spread;
  the glitchy interval is now 10 min–5 h with a 30 min floor between its ends.
- **`wisp:flash:release`.** Flash a signed wisp image over USB from the console.

### Fixed
- **Wisp OFF release.** Turning a wisp off releases its lamps promptly instead of
  occasionally stranding one until the 60 s watchdog.
- **Wisp source control.** Setting a source no longer flips back to Manual with
  empty colors under a slow multi-hop round-trip.
- **USB re-flash boots the flashed image.** Flash tasks reset the OTA selector, so
  a re-flashed lamp runs the new image instead of a previously OTA'd slot.
- **Shifty.** Directional fills (Up/Down/Bloom) now paint edge-to-edge.
- **Legacy peers in Social.** A legacy lamp's white shade shows white, not black.
- **Reaching-lamp overlay** blocks navigation while it's shown.
- **My Lamps online status** reacts to a connection drop instead of showing a
  stale online state.

### Changed
- **Pulse** enters from off-strip and bunches at each edge.
- Wisp controls standardized on the gradient bottom bar; reaching-lamp overlay
  frosted; Add-Color moved out of the Save/Cancel row.

## 1.1.1

### Added
- **Battery Saver.** A per-lamp brightness ceiling (Saver / Standard / Bright)
  that trades brightness for runtime, with a battery-life estimate from the
  lamp's measured draw.
- **ESP-NOW v2 mesh frames.** Larger frames (up to ~1470 B vs the classic
  ~250 B) with per-target OTA chunk-size negotiation (200 B baseline → 1444 B
  ceiling, advertised via a HELLO TLV) and both-sided RSSI-gated offers, so OTA
  moves in fewer packets with less BLE-coex contention. Non-advertising peers
  fall back to the 200 B baseline.
- **OTA channel promotion.** A `-beta` lamp graduates to `-stable` over the air
  (no USB reflash), gated on top of the ed25519 verify.
- **Mesh command-auth.** HMAC-SHA256 tag on the "force another lamp" frames
  (EVENT/COMMAND); dev / from-source builds can't drive cascades or greetings on
  keyed lamps.
- **Expression motion modes.** A shared Motion control (Linear, Smooth, Float
  lava-lamp drift, Settle, Swell) shapes an expression's travel, and a
  Continuous mode ping-pongs a wave forever instead of running one sweep and
  stopping.
- **Per-surface expressions.** An expression targets the shade, the base, or
  both, so a lamp can run a distinct animation per surface instead of one global
  set; the editor's target switcher retargets in place.
- **Gradient shades.** The shade takes up to six color stops that blend into a
  gradient along the strip, matching the base rather than a single flat color.
- **Social greeting base-gradient.** Lamps exchange colors on greet
  (`MSG_COLOR_QUERY` / `MSG_COLOR_INFO`) and render the peer's base as a blended
  gradient. Cross-platform: iOS and Android lamps recognize each other via a
  self-reported BD_ADDR social identity.
- **Home mode redesign.** Network binding is opt-in (plain on/off by default,
  no WiFi scan unless enabled); per-expression-type + social quiet toggles
  replace the hardcoded `allowedInHomeMode` flag.
- **Per-lamp LED byte-order** picker (RGB/GRB/…), end to end.
- **Web config UI overhaul.** AP-duration setup, password gate, expressions
  modal (duplicate types + preview-release), 5-tap advanced LED-type picker,
  home-mode toggles, adopt-on-save, `Features::WebApp` snafu gate.
- **Gamma-corrected LED output.** Colors match the app instead of washing to
  white on the strips; tunable `kOutputGamma`.
- **Version reporting.** Firmware version derived from the root `VERSION` file;
  the wisp broadcasts it and the app + web UI show it.
- **Dev firmware panel** (dev-channel app). A mesh-lamps roster screen with a
  5 s poll for bench visibility into fleet state.

### Fixed
- **OTA re-offer loop.** Reject unverifiable firmware offers up front; the
  distributor self-verifies at boot and won't offer if it can't. Cross-variant
  offers that a peer declines are now blocklisted instead of re-offered on a
  timer (kills the standard→snafu offer/reject churn).
- **Post-boot roster dead zone.** A fresh lamp sends HELLOs every 5 s for its
  first 30 s (then settles to the 60 s cadence), so it fills its neighbor roster
  quickly instead of waiting a full interval before cascades can reach it.
- **OTA offer authentication.** The firmware offer carries an auth tag.
- **Expression-editor ghost rows.** The Test preview no longer writes a
  transient config to NVS, so previewing doesn't leave duplicate rows behind.
- **Brownout on boot** with the full segment config.
- Full Salty **snub floors at ~10%** instead of full black.
- Config migration for the per-lamp LED type.

### Changed
- **Mesh HELLO cadence** slowed to 60 s (roster prune at 240 s), lamp + wisp.
  Idle mesh chatter drops; the boot burst covers fast initial fill.
- **Cascade payload budget** raised from 224 B to the v2 frame ceiling (1444 B),
  so big-palette cascades send. The oversized broadcast reaches v2 peers only; a
  mixed-fleet capability gate is owed before public beta.
- **Per-frame supply-budget power governor** replaces the fixed brightness cap.
  Brightness is scaled to a measured power budget each frame instead of a flat
  ceiling.
- **Expression rework:**
  - *Glitchy.* A `Coverage` slider (0–100%, always active) sets how much of the
    active region glitches each frame as single-pixel specks; the old
    `count`/points, `Size`, and `Grain` controls are gone and each frame repaints
    from the background, so coverage is a steady scatter density, not an
    accumulating solid.
  - *Shifty.* The dead interval control is dropped (continuous fill); Uniform
    fill honors the configured zone.
  - *Breathing.* Reworked into a Sections wave: a soft-edge vignette (taper +
    curve), staggered random section order, 8 s breath floor.
  - *Spotty.* Size is a Small–Large control (1–6 px), each spot gets a random
    lifetime band, and the fast end of the speed range flickers like a candle.
  - A **universal Whole-strip / Region zone toggle** across expressions via a
    shared `resolveZone()` helper; Pulse's size is now a percentage of the zone.
- **Nearby roster cap** raised to 50.
- Mesh wire-format core **extracted to a shared library** (lamp and wisp build
  from one source instead of drifting mirror copies).
- Local flash defaults to **unsigned**; signing is CI's job.
- Firmware tests exercise the **real code** (drifting mirror doubles removed).
- Comment-policy pass across the lamp + wisp firmware.

## 1.1.0

Initial mesh-era release: the ESP-NOW fleet firmware and the Flutter control
app. Everything since builds on this baseline.

### Added
- **ESP-NOW mesh networking.** Lamps discover each other and gossip presence
  over ESP-NOW (HELLO beacons carrying name, colors, and firmware version),
  relay frames across multiple hops with per-message-type dedup rings, and prune
  stale peers from the roster. A versioned wire format carries a receive range so
  mixed-version fleets interoperate.
- **Mesh expression cascades.** Triggering an expression on one lamp propagates
  it across the fleet, with stagger timing so the change ripples out instead of
  flashing in unison.
- **BLE GATT control service.** A frozen positional attribute layout exposes
  real-time brightness and color plus a JSON section protocol (paged reads,
  settings-blob writes) for name, expressions, and mood. App traffic is
  authenticated at the app layer with AES-GCM.
- **Mesh OTA firmware distribution.** Signed beta/stable binaries (ed25519)
  move lamp-to-lamp over the mesh: a single-peer distributor, a receiver with
  progress indication, and channel / version / variant gating so a build only
  lands where it belongs. A web and USB installer (update.lamplit.ca, full
  distribution image) covers first flash and recovery.
- **Expressions system.** Per-surface firmware animations (glitchy, pulse,
  breathing, shifty, spotty) the lamp auto-triggers on its own cadence, each
  schema-driven so the app builds its editor straight from the descriptor.
  Persisted in NVS and round-tripped through the app.
- **Lamp personality and social greetings.** Each lamp has a SocialMode
  (introvert / ambivert / extrovert) and a per-peer disposition; when lamps meet,
  they greet with a waveform on the shade shaped by that pairing, and dim as the
  room crowds.
- **Wisp infrastructure node.** A Seeed XIAO ESP32-C6 that subscribes to an
  Aurora palette feed, distributes paint to lamps over the mesh, and beacons its
  status; the app reads wisp state by proxy off a connected lamp.
- **Flutter control app.** Lamp adoption and onboarding, color and gradient
  editing, the expressions editor, a firmware panel, and wisp control, all over
  BLE.

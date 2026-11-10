# Changelog

Notable changes per firmware version. The version is the root `VERSION` file;
add an entry here in the **same change** that bumps it. Entries describe what's
new for someone upgrading — what changed for you, not how it was built.
Highlights grouped Added / Fixed / Changed, not every commit.

## Unreleased

### Fixed
- **Lamps and wisps left on for weeks keep working past day 49.** A lamp's
  millisecond clock rolls back to zero every 49.7 days of uptime. Around that
  moment a lamp could freeze its light or dim to half for a few seconds, keep
  showing the update indicator after a firmware update gave up, stall a
  firmware update it was passing to a neighbour or keep skipping that
  neighbour, close the web config page while you used it, drop a lamp it had
  just heard from its nearby list, greet a lamp it had just greeted again,
  relay every mesh check-in instead of thinning them, and for a minute accept
  the BOOT-button password reset as if it had just started. A wisp could keep lamps and other wisps that had gone silent,
  forget ones it had just heard, or hold back its brightness re-sends. All of
  these now carry on through the rollover.

## 1.3.2

### Fixed
- **A frozen lamp restarts itself.** If the lamp's main loop ever stops
  making progress, the lamp now resets after about 5 seconds (30 while it
  receives a firmware update) instead of staying frozen or dark until it is
  unplugged, and the reset shows in the lamp's crash record. A freeze in the
  first 30 seconds after a firmware update rolls the lamp back to its
  previous firmware. A lamp that freezes again and again right after
  starting stops restarting after the third time, so it never flickers
  through endless restarts; if it freezes again it stays frozen until you
  unplug it. Unplugging it, or any other restart, brings the freeze check
  back, unless that restart is itself a freeze in the lamp's first minute.
  A freeze while the lamp is still starting up is not caught yet.
- **Dropped writes show on the debug console.** On a development build, a
  colour or control write the lamp ignores, during a firmware update or
  because it could not read it, prints a `[drop]` line naming the setting
  and why.
- **No more edits that go nowhere while the app updates a lamp.** While the
  app sends a lamp new firmware, the lamp ignores every other change, so the
  app used to show your colour and brightness edits while the lamp stayed put.
  Now a strip at the top reads "Teaching <name> new tricks 🎓" with a progress
  line, and every control that changes the lamp greys out until the update
  ends. A sheet or dialog that was open freezes with your edit kept, and
  thaws when the update ends. The Info tab stays live. If the update fails
  or is cancelled, the strip stays until the lamp says it's listening again.
  The lamp waits up to a minute for the rest of an update before giving up,
  so that can take about 70 seconds, never more than 75. Then the app
  re-sends what the lamp missed while it was busy (Home Mode, an open colour
  sheet, the Social page) and reads the lamp's settings back so the screen
  matches it. The app re-sends the same after every reconnect too. Lamps now
  tell the app when they stop ignoring changes. A lamp on older
  firmware can't, so editing comes back as soon as the update ends and Home
  Mode's brightness routing is put right on your next brightness change.

## 1.3.1

### Added
- **Rotate colours.** A standard lamp's shade and base can each flow their
  own colours round an endless loop. In the app, the Shade and Base colour
  sheets carry a Rotate colors switch with a Loop length (5 seconds to 2
  minutes, slow on the left) and a Direction (Up or Down, with the shade
  starting Down and the base Up so they meet at the seam). The lamp follows
  live while you edit, Save keeps it and Cancel puts it back, and it keeps
  flowing after a reboot, a web config save, an expression test and a
  firmware update. With one colour the switch greys out with "Add a second
  color to rotate." and keeps your setting for when you add another. A
  surface split into segments stays still. While a role rotates, its card on
  the Colors page moves and reads "Rotating · 40s loop". Shifty's colour
  shift follows a moving base through its fades and lands on it with no
  jump. The Base and Shade sheets now grow to fit their colours, up to most
  of the screen, on every lamp. Lamps that can't rotate show no Rotate switch
  and no moving card.

### Fixed
- **Clearing a wisp's manual palette reaches the wisp.** Remove every colour
  from the manual palette and save, and the wisp stops painting: lamps under
  it ease back to their own colours. The app shows the palette as saved once
  the wisp reports it after the save, so a lamp re-sending a report the app
  already had doesn't mark it saved, and a wisp restart doesn't get in the
  way. If the wisp never reports it, the app says the palette didn't reach
  the wisp, as it does for any other palette. When the wisp's palette was
  already empty, a report from just before the save can still mark it saved.
  Rarely, an emptied palette still shows as not reaching the wisp even when
  it did: after a wisp restart, or once the wisp's uptime passes about 49
  days, when no report from the lamp since connecting has carried the lamp's
  own timestamp, or the report answering the save comes through trimmed
  without it; and on a connection held while the lamp's uptime passes about
  49 days, until the app reconnects. Before, the app showed the palette empty
  but never sent it, so the wisp kept painting the old colours.
- **Wisp palette and source changes show as saved only once the wisp reports
  them.** Save a palette, empty one, or change the wisp's source, and the app
  marks it saved when the wisp itself reports the change; for a palette, only
  a report from the wisp you saved to counts. Save twice in quick succession
  and the newer save wins: the older one stops resending and reports no
  failure. Before, a background settings read could mark a change saved that
  the wisp never confirmed.
- **`lamp:flash:release` no longer flashes another release's cached image.**
  Downloads are cached per variant and release tag, so flashing one tag
  can't silently reuse the image cached for a different one.

### Changed
- **The Colors page draws a standard lamp's shade the way the lamp shows it.**
  The lamp picture at the top and the Shade card put the shade's first colour
  at the bottom, where the standard lamp's shade starts. The Shade sheet's
  colour list keeps the first colour in the top row. Other lamps look as
  before.

## 1.3.0

### Fixed
- **Opening a lamp connects more reliably.** The app retries a failed connect
  a few times before giving up, and holds its Bluetooth scan off while it
  connects so the two don't fight over the phone's radio.
- **The app no longer connects to lamps in the background.** My Lamps doesn't
  open a link to every paired lamp it hears; the app connects only to the lamp
  you open.
- **Re-opening a lamp right after backing out keeps the connection.** Back out
  of a lamp while it's still connecting or loading, open it again, and it
  connects and stays connected. It can take several seconds longer while the
  phone finishes with the earlier attempt.
- **A lamp you had open stays on My Lamps after the app is closed.** Leave the
  app with a lamp open and close it soon after, and the lamp is still listed
  when you relaunch. If the phone keeps the app in the background for a long
  while before closing it, the lamp can show offline or drop off the list
  until it's heard again.
- **Wisp paints black and warm white.** A lamp the wisp paints black, or warm
  white with no colour in it, takes that paint instead of dropping back to its
  own colours, and warm white from a wisp palette now reaches the lamps. Black
  needs only the wisp updated. Warm white needs both the wisp and the lamp on
  the new firmware: an older lamp ignores the warm white and shows only the
  colour part (so warm white alone shows as black), and an older wisp sends
  none. The app's painted-lamps preview shows warm white and black as painted,
  for up to 34 lamps under the wisp; past 34 it shows colour only, as before.
  That needs both the lamp firmware and the app updated; with either one
  older, the preview shows the lamp's predicted colour as before.
- **Wisp-painted lamps hold their colour.** Lamps painted by a wisp no longer
  drop back to their own colours for a minute or two at a time. The wisp now
  sends its colour updates twice as often and at a varying moment, so a lamp
  that kept missing them at one fixed moment catches them.

### Changed
- **My Lamps lists the lamps around you.** A paired lamp the phone can't hear
  shows as offline for five minutes after it was last seen, then leaves the
  list until it's heard again. It stays paired and keeps everything the app
  knows about it. With no paired lamp heard in the last five minutes, My
  Lamps shows just the "Adopt a lamp" tile.
- **The app downloads lamp firmware once each time it starts.** It used to
  download it again every time your list of saved lamps changed. Now it
  fetches firmware for each kind of lamp you own once when the app starts, and
  once more for a new kind of lamp you add that it has no firmware for yet. If
  you're offline it keeps the firmware it already has and tries again the
  next time the app is restarted. Updating a lamp when it connects, and the
  firmware panel, work from what's already downloaded, and the panel's empty
  message now says firmware downloads when the app starts online.

## 1.2.9

### Added
- **Moments.** Once in a long while a room of lamps that like each other
  shares a moment, with nothing to set up, on each lamp's base and in a colour
  that stands out against it. **Fireflies**: when three or more lamps a lamp
  is fond of have stayed together for ten minutes, the room's bases start
  sparkling faintly with stars in each lamp's shade colours, scattered at
  first, then drawing into one shared beat for a couple of minutes.
  **Whisper**: a soft glow in the starting lamp's colour travels across a base
  and is handed on, lamp to lamp for up to eight hops, each time to the
  closest lamp (strongest signal) it is fond of or smitten with that hasn't
  had it yet. Salty and Wary lamps sit out. Each lamp's Moments grow rarer the
  more it takes part (back to normal after a restart), Extroverts start them
  more often and Introverts less, and nothing happens during an update, while
  the wisp is painting, while you preview in the app, or in home mode with
  social off. Lamps on older firmware just ignore them.

### Fixed
- **Breathing fades more smoothly.** Breathing blends at full precision instead
  of stepping in whole percents, so a slow breath moves in finer steps.

## 1.2.8

### Added
- **The wisp can follow Straylight.** Pick Straylight as the wisp's palette
  source in the app and choose which of Straylight's palettes to follow —
  Primary, Secondary, Tertiary or Fill. The wisp takes it over USB or the
  network (USB is the steadier of the two). If Straylight goes quiet for
  about thirty seconds, or the palette you picked has nothing in it, your
  lamps ease back to their own colours and the app says it's waiting; when
  Straylight comes back the lamps pick its palette up again on their own.
- **The wisp can take Straylight over a network cable.** Fit a W5500
  Ethernet module (wiring in the wisp README) and plug the wisp into the
  venue network: Straylight's feed arrives over the cable, the wisp's WiFi
  stays off so the mesh keeps its radio, and the app shows the wisp's
  network address. A wisp with no module works exactly as before.
- **Pulse has a Direction.** Up is the default. Down runs the other way: on
  both surfaces it drips from the top of the shade down to the bottom of the
  base.

### Fixed
- **Home mode with social off now ignores other lamps completely.** Before,
  another lamp could still make yours pulse, flicker or shift while it was
  resting at home; only glitch was kept out. Now, while home mode is on with
  social switched off, your lamp ignores every effect other lamps send it,
  doesn't greet them and behaves as if it were alone in the room, whichever
  kind of lamp it is: a Lioness's lions stop showing neighbours' colours and
  a Loaf's ring stops speeding up for nearby loaves. Effects other lamps had
  already sent fade out gently when home mode comes on, and when you leave
  home your lamp doesn't greet the lamps that were there all along. It also
  stops dimming itself for a crowd, easing back to its full home brightness
  over about a second. Previewing home mode from the app shows what home mode
  looks like without using up arrivals, so the lamps around it are greeted as
  usual once you leave the page; a greeting or borrowed palette the preview
  cuts off stays ended, and the app's greet button still plays a greeting
  during the preview. Your own expressions, the app's Test button and your
  wisp keep working, and your lamp still passes messages
  along so the lamps around it keep talking.
- **A staff lamp blooms for every friend in a big crowd.** With more than 16
  lamps nearby, friends past the sixteenth used to arrive without a bloom.
- **Crowd dimming follows the crowd right away.** When a lamp decided to dim
  or brighten for a change in the crowd, the change could wait until the next
  unrelated brightness change to show. It now eases in within a moment.
- **Every lamp passes updates on.** A lamp at home with social turned off,
  and snafu and lioness lamps at any time, never offered a firmware or web-UI
  update to the lamps around them, so an update could stall at them. Every
  lamp type now passes updates on in every mode.
- **Lamps passing an update along stay on the update display.** A lamp
  updating several neighbours in a row no longer flashes back to its normal
  colours between one neighbour and the next. If an update to a neighbour
  fails, the lamp goes back to its normal colours about ten seconds later, as
  before.
- **Shimmer's Candle and Campfire are calmer.** Candle is now a slow sway
  with a soft flicker now and then instead of a fast buzz, and Campfire keeps
  its gusts and flaring embers without looking hectic. Neither speeds up on a
  lamp that runs faster.
- **Lamps hold up in big crowds.** A lamp among dozens of others could run out
  of memory and restart, with or without the app open: saving settings or
  social dispositions on a crowded lamp could crash it outright, and a crowd's
  traffic piled up memory on its own. A save that can't go through now waits
  and tries again a few seconds later instead of crashing, keeping the
  settings stored before it, and the lamp caps how much a crowd can make it hold
  at once: other lamps' effects can take at most half of its four effect
  slots, leaving the rest for its own Test and greetings while memory allows.
  When the lamp is too short of memory to show a Test itself, the Test still
  goes out to the lamps around it. A cascade still reaches every lamp around,
  sent in waves rather than all at once, so in a crowd of 50 the farthest
  lamps can fire up to about a second after their usual moment, nearly two
  seconds when two cascades go off together, and later still on a busy mesh.
  Expressions keep at most 8 colours each, and a stored setup beyond the app's
  limits is trimmed back to them when the lamp updates.
- **Big setups keep saving.** A lamp with many expressions could run out of
  room to store its settings and quietly stop saving changes, and a full
  store could leave a lamp unable to start Bluetooth until it was
  reflashed. Settings are now stored compactly and in pieces the storage can
  always place, so every expression the app allows fits with room to spare,
  and a lamp whose storage is already full frees space on its first start.
  Your existing settings carry over on update. If the lamp's storage reports
  itself unusable at startup it is cleared so the lamp still comes up and
  can be set up again from the app. A big setup saved from the web page now
  fits more often, since the lamp reads the page's settings once instead of
  twice. A setup near the limits may still be refused by the web page when
  the lamp is short of memory; save it from the app instead. A second save
  from the web page while the first waits for its restart is refused, so the
  first one is kept.
- **Settings the lamp can't keep are refused up front.** A control password
  longer than 64 characters, or a home network name longer than 32, is
  refused from the app and from the web page, instead of being stored in a
  form that could never work. An expression type the lamp doesn't have is
  refused instead of being saved and never showing.
- **Going back to an older firmware.** A lamp with a small setup (about two
  expressions with full colour lists) keeps its settings on an older
  release. A larger setup keeps only its name, password, brightness and
  setup state there; everything else comes up as defaults, and that release
  may fail to save. Social dispositions reset there either way. Updating
  again brings the larger setup back unless the older release saved over
  it.
- **A fully set-up wisp no longer blanks the app's wisp screen.** With a name,
  a password, Ethernet and custom LED and drift settings, the wisp's status
  could outgrow what one Bluetooth read carries, and the app showed no wisp at
  all. The app now reads the wisp's status and its settings separately, in
  pieces, with no size limit, once your lamps are updated. Updated lamps also
  trim what they hand older apps, which brings a typical fully set-up wisp
  back under the limit, though not every possible one. Until the wisp's settings
  have arrived, the drift, brightness and LED controls show that they're
  loading instead of showing made-up values you could accidentally save.

### Changed
- **Lamps no longer pass settings on to other lamps.** A lamp used to be able
  to forward brightness, colour and expression changes it was handed to
  another lamp across the mesh. No released app has ever used this, so
  it is gone: a lamp only takes settings from the app connected to it. Lamps
  still pass along everything your wisp sends and receives, so the wisp keeps
  working everywhere it did, and lamps on older firmware keep talking to
  newer ones.
- **The wisp sends its settings on their own.** Its LED, brightness and drift
  settings now go out when they change or when the app asks, rather than only
  inside the status every lamp passes along every thirty seconds. For this
  release the status still carries them too, so lamps that haven't updated
  keep showing them; the mesh airtime this saves arrives with a later update,
  once every lamp understands the new message.
- **Aurora is gone.** The wisp's Aurora palette source, which never shipped
  past "untested", has been removed from the wisp and the app, along with its
  zone picker. A wisp that was set to Aurora comes up Off.
- **The wisp stops hunting for missing WiFi.** It joins its saved network
  only while following Straylight, looks for it on the lamps' own channel
  only, joins only when it finds it there, and waits longer between tries (up
  to five minutes) when it doesn't. Searching for a network that was gone kept pulling the wisp away
  from your lamps until they dropped out and went back to their own colours.
- **A Pulse on both base and shade is one pulse rising up the lamp.** It
  starts at the bottom of the base, climbs to its top and carries straight on
  up through the shade, instead of two pulses running side by side. At the
  default speed it takes about as long as each half used to; at slower
  settings Pulse speed is the time to climb the whole lamp, so each pixel goes
  by faster than before. Size is measured against the whole lamp. A Region
  applies to each surface, and a Pulse on just the shade looks exactly as it
  did. On a lamp whose base and shade don't stack, the
  pulse sweeps one and then the other. While lamps are partway through an
  update, a lamp still on the old firmware shows a received both-surface
  Pulse as two pulses side by side, ignores Down, and still runs a base-only
  Pulse down the base.
- **A Pulse on just the base rises.** It used to run down the base; Up now
  climbs it from the bottom, like the shade, and Down runs it downward.

## 1.2.7

### Changed
- **Your lamp password now locks its setup WiFi.** The setup network a lamp
  puts up for its web config page used to be open, so anyone nearby could join
  and change settings even on a password-protected lamp. Set a password and the
  setup network is WPA2-encrypted with it — you'll need that password to reach
  the web config page. Lamps with no password, or a password shorter than 8
  characters, still put up an open network, and the web config page says which
  of the two you have instead of implying it's private. On a lamp whose
  password is too short to lock the network, the web page goes read-only rather
  than handing settings — or the password itself — to anyone who joins.

### Added
- **Forgot your lamp's password?** Hold the small BOOT button on the lamp's
  board for five seconds within a minute of switching it on and the password is
  cleared, so the app can connect again. The whole lamp pulses twice to confirm
  it worked. The lamp keeps its name, colours and everything else. Its setup
  WiFi re-opens the next time the lamp restarts.

### Fixed
- **Lamp names can't hide a space any more.** A name with a space in it made
  the setup WiFi network appear under a different name than the app showed
  ("gramp -lamp" for a lamp called "gramp"), so it looked missing. Spaces are
  now rejected as you type and stripped from names already saved, which also
  tidies the lamp's Bluetooth name.
- **Your colours stay where you put them.** A lamp would quietly fall back to
  its factory colours about a minute after you set new ones, so it looked like
  nothing had saved. Your colours were always stored correctly — the lamp was
  painting the wrong ones, which is why reopening the app or restarting the
  lamp brought them back.
- **Every lamp starts with its own colours.** New lamps were all coming up on
  the same blue and red instead of the distinct colour pair each lamp is meant
  to pick for itself the first time it's set up.

## 1.2.6

### Fixed
- **The expression tab is reliable in a crowded room.** Opening, adding, or
  editing expressions could come up blank when a lot of lamps were around — the
  lamp couldn't build the expression catalogue under memory pressure. The
  catalogue now lives in the lamp's flash and is handed to the app without using
  any working memory, so the list, add, and edit always work no matter how busy
  the room. The app also fetches a given catalogue once and reuses it.
- **Nearby lamps actually show up in Social.** "Nearby" was tuned so tight that
  only a lamp on the same desk counted; a lamp across a normal room never
  registered as near. Lamps a room apart now show up.
- **Lamps don't flash black in Social during an update.** While a lamp was
  passing a firmware update to its neighbours it blanked its own advertised
  colour, so it briefly showed black in other lamps' Social lists. It keeps its
  real colour throughout now.

### Changed
- **My Lamps shows four lamps per row** for a tighter overview.
- **The updating indicator shows direction** — blue while a lamp is receiving an
  update, purple while it's sending one to another lamp.

## 1.2.5

### Added
- **The app tells you when your router is on the wrong Wi-Fi channel.** If you
  point the wisp at a home network that isn't on channel 6 (the channel the
  lamps use to talk to each other), the Wi-Fi screen now explains the conflict
  and offers a Retry button, instead of a misleading "not connected."

### Fixed
- **Connecting the wisp to Wi-Fi no longer stops it lighting the lamps.** The
  wisp has a single radio and the lamps talk on channel 6; joining a home
  network on a different channel used to pull the wisp off that channel so it
  could no longer reach the lamps. It now refuses to leave the lamps' channel,
  stays on the mesh, and reports the mismatch so you can fix it (set your
  router's 2.4 GHz band to channel 6).
- **The wisp no longer stalls while looking for Aurora.** Searching for the
  Aurora server could briefly freeze the wisp and drop lamps from its view; that
  search now runs in the background without pausing everything else.

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

# Pass: BLE + app end-to-end

Verifies the frozen GATT contract, auth, settings round-trips, and
connection-state behavior against a real phone + a host-side BLE client. Run
after any change to `components/network/ble/`, the settings/apply path, or
the Flutter app's BLE layer.

## Host-driven rows: feasibility

**A lamp with NO password set accepts plaintext GATT writes.** Verified in
`ble_control.cpp` (`decodeIncomingOp` / `isAuthed`) + `crypto.cpp`:

- Bare JSON, or JSON prefixed with `MAGIC_PLAINTEXT` (0x01), is accepted;
  auth is checked via `isAuthed()`, which returns true immediately when
  `lamp.password` is empty.
- `MAGIC_CIPHERTEXT` (0x02, AES-GCM) is the only path once a password is set;
  plaintext writes are then silently dropped unless a prior CHAR_AUTH write
  succeeded on that connection.
- Reads are plain JSON always (auth-gated, but never encrypted);
  CHAR_SCHEMA_VERSION is readable with no auth at all.

So the H rows below run from the Mac with `bleak` (already installed in
`~/.platformio/penv`) — no phone needed — as long as the lamp has no
password. The password rows themselves are what turn this off.

## Hardware

- **1 lamp on USB serial** (beta/dev build for the serial cross-checks), NO
  password set at the start.
- **A phone with the app** for the P rows.
- **A 2nd powered lamp** only for the greet-suppression row (P5).
- Host rows: Mac Bluetooth on; stop anything else talking to the lamp.

## Steps — host rows (bleak)

H1 — scan by manufacturer data, read schema, read a section, write a
settings op, read back:

```sh
~/.platformio/penv/bin/python - <<'EOF'
import asyncio
from bleak import BleakScanner, BleakClient

SCHEMA    = "5f64f4ea-d6d9-4a44-9b3f-3a8d6f7e6b40"  # read, no auth
PAGE_CTRL = "5f64f4dc-d6d9-4a44-9b3f-3a8d6f7e6b40"  # write section name
PAGE_DATA = "5f64f4dd-d6d9-4a44-9b3f-3a8d6f7e6b40"  # chunked read
EXPR_OP   = "5f64f4d9-d6d9-4a44-9b3f-3a8d6f7e6b40"  # JSON op
LAMP_MFG_ID = 42069  # 0xA455, little-endian in the adv payload

async def read_section(c, name):
    await c.write_gatt_char(PAGE_CTRL, name.encode(), response=True)
    buf, first = b"", None
    while True:
        chunk = await c.read_gatt_char(PAGE_DATA)
        buf += chunk
        if first is None: first = len(chunk)
        if len(chunk) < first or len(chunk) == 0: break  # short chunk = EOF
    return buf.decode()

async def main():
    found = await BleakScanner.discover(timeout=8.0, return_adv=True)
    dev = next((d for d, adv in found.values()
                if LAMP_MFG_ID in adv.manufacturer_data), None)
    assert dev, "no lamp advertisement (mfg 0xA455) seen"
    print("lamp:", dev.name, dev.address)
    async with BleakClient(dev) as c:
        print("schemaVersion:", (await c.read_gatt_char(SCHEMA))[0])
        print("expr before:", await read_section(c, "expr"))
        await c.write_gatt_char(EXPR_OP,
            b'{"op":"upsert","entry":{"type":"pulse","enabled":true,'
            b'"intervalMin":600,"intervalMax":900,"target":3,'
            b'"colors":["#00100000"],"pulseSpeed":3}}', response=True)
        print("expr after:", await read_section(c, "expr"))
asyncio.run(main())
EOF
```

- Snapshot first (`expr.get` over serial) if the lamp has real expressions;
  restore last (`expr.set` the snapshot / `{"op":"remove","type":"pulse","target":3}`).
- Cross-check: `expr.get` over serial must show the same upserted entry —
  proves the BLE write hit the same config the firmware serves.
- The EOF heuristic (chunk shorter than the first) tracks the negotiated
  MTU; page chunks are `min(MTU-3, 244)` bytes.
- Other readable sections for `PAGE_CTRL`: `lamp`, `base`, `shade`, `home`,
  `nearby`, `exprcat`. A config section (`lamp`, `base`, `shade`, `expr`,
  `home`) reads `!` when the lamp is too short of heap to build it; read it
  again after a moment. `nearby` is built only while it is read: the first
  read waits up to 100 ms for the lamp to build it and reads empty if it
  could not; it is freed 30 s after the last read, or on the next loop tick
  once the app leaves Social (`editSession surface=0x08 closed`), also when
  Social is opened and left while the app pushes firmware.
- A section's copy over 1 KB (nearby, a big expr) is freed by the read that
  returns the empty chunk; a smaller one keeps its buffer, so re-reading the
  same small section on a tight heap serves it. A small section read right
  after a large one can still read `!` on a tight heap (needs a bench
  number: `largest` during a re-read with a big expr).

H2 — schema version is 4 (`kGattSchemaVersion`, `gatt_layout.hpp`); reading
it must work even later, after a password is set (it is the one un-gated
characteristic).

## Steps — phone rows

P1 — **password + auth.** Set a password in the app (8–16 chars). Serial on
auth attempts logs `[ble_control] Auth via ciphertext handle=N OK` /
`Auth attempt handle=N ACCEPTED` / `REJECTED`. Then re-run H1's EXPR_OP write:
it must be silently dropped (no `[cmd]`-side effect, `expr.get` unchanged) —
plaintext is dead once a password exists. Wrong password in the app must fail
to unlock. Remove the password at the end of the pass (or factory-reset per
[config-nvs.md](config-nvs.md)).

P2 — **settings write → readback → persist.** In the app change brightness +
a base color, force a readback (leave/re-enter the lamp screen), then reboot
the lamp (`python3 scripts/bench_cmd.py <port> --reset --wait-ready
'\[show\] ready' --duration 20`) and confirm the app reads the same values
back and the boot log's `[cfg] loaded ...` reflects a persist.

P3 — **expression editor round-trip.** Create an expression in the app's
editor, save, then `expr.get` over serial: the entry must be present with the
values as edited. Delete it in the app; `expr.get` shows it gone. Then drag a
Zone slider and hold it, and switch the phone's Bluetooth off mid-hold: the
lamp puts back the scene it showed before the marker, serial shows `Client
disconnected` with no `Guru Meditation` or `abort()`, and after reconnecting
the `lamp` section's `lastCrash` is unchanged. Repeat a few times, some while
the shade is still fading.

P4 — **reconnect after force-stop (frozen GATT contract).** Force-stop the
app (Android: App info → Force stop), reopen, reconnect. All reads and writes
must still land correctly — positional handles must not have shifted (they
can't unless the layout changed; a wrong-handle symptom here means someone
broke the append-only rule and owes a `kGattSchemaVersion` bump).

P5 — **greet suppression while connected.** Mechanism: the lamp stops its
BLE scan while a GATT client is connected (`Client connected` /
`Client disconnected` in serial) — no scan, no new arrivals, no greets. With
the app connected, power-cycle the 2nd lamp (off ≥2 min first so it re-counts
as an arrival): NO `[social] greet` line while connected; disconnect the app
and the greet fires on the peer's next sighting. **Known race window
(observed 2026-07-15):** a peer scanned in the seconds before the app
connects is already in the roster as an ungreeted arrival, so a greet can
still start right after connect. That is current behavior, not a failure.

P6 — **editing pauses while the app updates a lamp.** Connect to a lamp the
app holds a newer cached build for, so the update starts on connect. While it
sends: a strip at the top reads "Teaching <name> new tricks 🎓" with a
progress line; the Colors, Social, Expressions and Config tabs are greyed and
ignore taps; Info stays live. Open a colour sheet first, then reconnect so the
update starts with it open: the sheet greys and keeps your colours, a tap
outside still closes it (after a live "Discard changes?" if you changed
anything), and once the update ends it saves normally. The expression
editor's colours sheet does the same. On Info, the card reads "Learning some
new tricks 🪄" while it sends and "Practising the new tricks 🪄" once the bar
is full. The strip goes when the lamp reboots after a good update and editing
comes back; once the lamp reconnects, a message reads "<name> learned some
new tricks ✨", and only once. A second lamp's screen shows nothing while the
first updates. A good update here also proves the lamp's BLE chunk path takes
chunks for a session the app opened.

P7 — **a cancelled update holds editing until the lamp listens.** On Info,
start an update from the dev-mode Firmware panel and cancel it there partway.
The panel reads "Lesson cancelled. <name> stops waiting for the rest in about
a minute." The app sends the lamp no cancel, so the lamp keeps its update
indicator for about 70 s (it waits a minute for more chunks, then holds 10 s).
Open Colors: the strip now reads "Giving <name> a moment to settle… 🎓", and it
and the grey stay up the whole time, then both go together when the lamp's
indicator ends, with no message (a cancel says nothing). Open an expression in
the editor during the hold: Test is greyed. Change the base colour after: the
lamp follows. On a lamp running 1.3.1 the strip and the grey go the moment you
cancel, and a colour change in the next minute does nothing on the lamp. On
the current lamp, cancel again, go back to My Lamps and reopen it within the
minute: the strip and the grey are back, and go when its indicator ends.
Change the base colour after: the lamp follows. The Home Mode cases (a focus
change dropped mid-update, a reconnect while Home Mode is open) can't be set
up from the app's screens and are covered by push_resync_test.dart.

P8 — **one reconnect after a save that restarts the lamp.** Needs a dev build
on the lamp (the `[ble_control]` lines below are debug-only) and a password
set. Start one tap and leave it open: `npm run lamp:tap -- <port>:lamp -o
/tmp/p8.log`. Open the lamp in the app, go to Advanced LEDs, change the strip
type and tap Update. After the lamp's boot banner, count the lines in the log:
exactly one `[ble_control] Client connected` and one `Auth via ciphertext ...
OK`, and no `Client disconnected` in the minute after them. Two of each means
two reconnects ran; a disconnect right after the second connect is the app
cycling a link it had already restored. Then leave the lamp open and idle for
a minute: `page CTRL section=lamp` should not appear at all.

P9 — **how an update ended.** Start an update in normal mode and walk the
phone out of range partway so it fails, and stay away over a minute so the
lamp gives up on the lesson (a quicker return is unmeasured: the lamp may
turn the retry down as busy). Come back: the app reconnects and the update
starts again on its own, with no message for the lost one. The "<name>
didn't catch the new tricks this time. Trying again next time you connect."
message is for an update that fails while the phone stays connected (a
lamp-side failure, shown once the lamp is listening again); it can't be set
up from the app's screens and is covered by firmware_push_lock_test.dart. In
dev mode, the Firmware panel shows a plain line first (for a dropped link,
"Lost <name> partway through. Bring your phone closer and try again.") with
the raw reason in small monospace beneath. Walk out of range right as the bar
fills instead: if the lamp still installs and comes back on the new version,
the message is "<name> learned some new tricks ✨", with no miss before it,
on a lamp running 1.3.1 too. Install a build the lamp already has from the
panel: it reads "<name> already knows these tricks." and the lamp does not
restart. Open a lamp that is busy teaching another lamp from My Lamps: the
page reads "<Name> is busy teaching a lamp new tricks 🎓" with the name
capitalized as elsewhere, and "It's hard to reach while teaching".

P10 — **names and saves.** Rename a lamp on Config to a long name: the field
stops at 32 bytes (an emoji takes 4, an accented letter 2) even with Gboard
underlining the word as you type, and the lamp's web config shows the same
name. Select the whole name and paste a longer one: the field keeps the
start of the paste. Do the same on a new lamp's naming step. On an iPhone
with the Japanese keyboard, type 12 kana without confirming and tap Save: My
Lamps and the lamp's web config both show the first 10. Tap the Advanced LEDs
Update, and the password Save, twice fast: the button greys with a spinner
after the first tap and the lamp restarts once. A rename the lamp refuses
("Couldn't set name. Try again.") can't be set up from the app's screens and
is covered by setup_screen_test.dart.

P11 — **a strip's data pin.** On a standard lamp with advanced mode off, LED
setup shows no data pin row. Turn advanced mode on: the Shade strip shows
"Shade data pin" reading "Built-in (GPIO 12)" and the Base strip "Base data
pin" reading "Built-in (GPIO 14)". Open the shade's list: GPIO 14 is greyed
with "Used by Base strip", the built-in entry has the check, and the list
stops short of the top of the screen and scrolls. Set the base to a GPIO the
base isn't wired to (say 27) and tap Update: the lamp restarts, the base
stays dark, and the row reads GPIO 27. Set it back to Built-in and Update:
the base lights again. On a staff lamp, set the base to GPIO 19 (its button)
and Update: both strips stay lit, the app reads "Base data pin didn't take.
It clashes with another strip, a button or a touch pad, so the lamp kept
its built-in pin.", and the row reads Built-in. Save a colour from the lamp's web config
with a pin set: the row still reads the pin afterwards. On a lamp running
1.3.3, LED setup in advanced mode shows no data pin row.

P12 — **the network picker scans on first open.** On a lamp with Home Mode on
and no phone connected for at least two minutes, connect and open Home Mode:
the picker shows its spinner for about 5 s, then lists the networks it found.
Close and reopen it: the list shows at once. Disconnect for a minute,
reconnect and open it again: the spinner shows again. With no phone connected
the lamp still goes into Home Mode within about a minute while the saved
network is in range, and leaves it within about two once the network is gone.
On a lamp running 1.3.5 the first open lists networks at once.

P13 — **the wisp screen across a reconnect and a revisit.** Needs a wisp in
Manual near the lamp and two phones. Open the lamp's wisp screen on phone A,
power-cycle the lamp and let A reconnect. Change the wisp's source from phone
B: A follows within a few seconds. Back in Manual, change a swatch on A and
Save: it saves, with no "not saved" after 20 s. Leave the lamp on A, change
the palette from B, then open the lamp on A again: the editor shows B's
palette. Empty the palette from B and reopen on A: the editor is empty.
Change a swatch on A without saving, leave and reopen: the edit is still
there. Open the lamp a few times on a wisp whose Manual palette is empty:
"Reading palette from wisp…" never stays past a few seconds.

P14 — **the palette editor shows the palette the wisp states.** Needs a wisp
flashed from this branch (`npm run wisp:flash`), two phones, and
`npm run lamp:tap` on the lamp, opened once; the display-switch steps need a
second wisp flashed from this branch. With the new wisp in
Manual and its palette empty, open the lamp: the editor shows empty within a
second or two, and the lamp log's `[wisp_state] notify` preview carries
`"manualId":""`. Put the new wisp in Osc with Straylight changing palettes and
a non-empty Manual palette, then tap Manual: the loading line shows, then the
real palette; never an empty editor with Save. Empty the palette from phone B
while phone A is off the lamp, then open it on A: empty. Power-cycle the lamp
while A has it open, and change the palette from B meanwhile: after A
reconnects it shows "Reading palette from wisp…" until the wisp's next status
(up to 30 s when idle), then B's palette, and never offers Save over the old
one. Move the lamp from the first wisp's zone to the second's: the editor
shows the loading line, then the second wisp's palette, never the first's.
Power-cycle the lamp, and open it on A the moment it connects: the editor
shows "Reading palette from wisp…" with no Save until the lamp log's
`[wisp_state] notify` preview carries `"source"`, then the wisp's palette.

P15 — **a palette save confirms on the wisp's echo.** Same setup as P14. On
the new wisp, edit and Save: the wisp's serial prints `[wisp.op]
setManualPalette colors=N seq=S`, the lamp log's `[wisp_state] notify` preview
carries `"manualSeq":S`, and no "not saved" shows. Save, then reset the wisp
at once: either the save confirms once the wisp is back, or "not saved" shows
and reopening the lamp shows what the wisp holds; never "saved" while the wisp
holds something else. Save different palettes from two phones (Pixel and the
emulator bridge) within a second of each other: both end showing the wisp's
palette, and at most one shows "not saved". Save, then tap Osc at once: the
save confirms, and back in Manual the palette shows.
Give the new wisp a rich config (20-character name, password, oscRole
secondary, brightness 80) and note the lamp's `notify len=`: saves still
confirm within a few seconds. Save on the new wisp with a 1.3.8 app: it
behaves as on 1.3.8.

P16 — **a Manual palette holds 16 colours.** Same setup as P14. In the
editor add colours: the `+` stops at 16. Save 16 colours with white on each:
the wisp's serial prints `setManualPalette colors=16`, the save confirms, and
the wisp's ring shows all 16. A wisp holding more than 16 takes a raw
`CHAR_WISP_OP` write, since no released app sends more than 10; the app and
wisp tests cover that case (the editor shows the first 16 with a note and
Save live, and the wisp loads a longer stored palette as its first 16).

P17 — **a settings change the wisp never gets is read back.** Same setup as
P14, the lamp on the new wisp. Open the Settings tab, move brightness, and
within a second power the wisp off: about 5 s later the slider goes back to
the value the lamp last heard from the wisp. With the wisp up, change drift,
brightness and the LED strip once each: each still shows its new value 10 s
later.

P18 — **no Save over a palette the app has not read.** Same setup as P14. On
the new wisp, open the palette sheet, edit a swatch, and power-cycle the lamp:
once the app reconnects the open sheet's Save greys under "Reading palette
from wisp…" and the edit stays. When the palette is read the note goes and
Save comes back with the sheet still open and the edit kept; until then the
wisp's serial prints no `setManualPalette`. Repeat with the sheet open while the
lamp moves from one wisp's zone to the other's. Tap Manual from Osc
with the palette shown, and within 20 s move the lamp to the other wisp's
zone: the editor turns to "Reading palette from wisp…" with no tap.

## Pass criteria

- H1 completes: mfg-data scan finds the lamp, schema reads 4, section reads
  parse as JSON, the plaintext op lands and reads back on BOTH transports.
- Password flips the contract: ciphertext/authed writes work, plaintext
  writes are dropped, wrong password rejected with the REJECTED log.
- App round-trips (P2, P3) byte-accurate and reboot-persistent.
- P4 reconnect fully functional with zero re-pair steps.
- P5: zero greet lines while connected (outside the connect race window).
- P6: no control takes a tap during the update, the strip names the lamp,
  and editing returns when it ends.
- P7: after a cancelled update editing stays frozen until the lamp leaves
  its update indicator, never past 75 s, and the first edit after lands.
- P8: one connect and one auth after an Advanced LEDs save, no disconnect
  after them, and no `lamp` section reads while the lamp sits open.
- P9: the retry on the next connect, a lamp that took a lost update says
  learned with no miss, and no message for a cancel or a declined offer.
- P10: no name field takes more than 32 bytes, a refused rename says so, and
  a double tap on a restarting save restarts the lamp once.
- P11: the data pin row shows only in advanced mode on firmware that reports
  pins, a picked pin drives the strip after the restart, another strip's pin
  is greyed out, and a pin that clashes with a button is reported by name.
- P12: the first picker open after a connect scans before it lists, a reopen
  lists at once, and home presence still follows the saved network.
- P13: the wisp screen follows the wisp after a reconnect and a save then
  confirms; a reopened lamp shows a palette changed or emptied elsewhere and
  keeps an unsaved edit; the palette editor never sticks on reading.
- P14: on a wisp from this branch the editor shows an empty palette as empty,
  never shows an empty or stale palette as the wisp's, and shows the loading
  line after a reconnect or a display switch until the wisp states its
  palette; on any wisp, a lamp that just booted shows the loading line with
  no Save until the wisp's own status arrives.
- P15: a save on a wisp from this branch confirms only on its own echo, in any
  source mode, and never shows "saved" for a palette the wisp does not hold.
- P16: the editor stops at 16 colours, and 16 white-heavy colours save
  whole.
- P17: a settings change the wisp never applied goes back to the wisp's value
  within about 5 s; one it applied stays.
- P18: no `setManualPalette` leaves the app while the palette is unread, a
  sheet left open greys its Save until the palette is read and then gives it
  back without closing, and a display switch shows the loading line with no
  tap.

## Not covered here

- Encrypted-path host testing (AES-GCM CHAR_AUTH from bleak) — the app is the
  ciphertext client; host rows go plaintext-only by design.
- CHAR_FW_CONTROL/CHAR_FW_CHUNK (app-driven OTA) and the wisp proxy chars —
  own passes.
- iOS-specific reconnect behavior; P4 as written is the Android
  force-stop case.

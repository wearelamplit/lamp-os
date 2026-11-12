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
  `nearby`, `exprcat`.

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
cancel, and a colour change in the next minute does nothing on the lamp. The
Home Mode cases (a focus change dropped mid-update, a reconnect while Home Mode
is open) can't be set up from the app's screens and are covered by
push_resync_test.dart.

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

## Not covered here

- Encrypted-path host testing (AES-GCM CHAR_AUTH from bleak) — the app is the
  ciphertext client; host rows go plaintext-only by design.
- CHAR_FW_CONTROL/CHAR_FW_CHUNK (app-driven OTA) and the wisp proxy chars —
  own passes.
- iOS-specific reconnect behavior; P4 as written is the Android
  force-stop case.

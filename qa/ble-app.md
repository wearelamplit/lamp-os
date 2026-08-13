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
values as edited. Delete it in the app; `expr.get` shows it gone.

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

## Pass criteria

- H1 completes: mfg-data scan finds the lamp, schema reads 4, section reads
  parse as JSON, the plaintext op lands and reads back on BOTH transports.
- Password flips the contract: ciphertext/authed writes work, plaintext
  writes are dropped, wrong password rejected with the REJECTED log.
- App round-trips (P2, P3) byte-accurate and reboot-persistent.
- P4 reconnect fully functional with zero re-pair steps.
- P5: zero greet lines while connected (outside the connect race window).

## Not covered here

- Encrypted-path host testing (AES-GCM CHAR_AUTH from bleak) — the app is the
  ciphertext client; host rows go plaintext-only by design.
- CHAR_FW_CONTROL/CHAR_FW_CHUNK (app-driven OTA) and the wisp proxy chars —
  own passes.
- iOS-specific reconnect behavior; P4 as written is the Android
  force-stop case.

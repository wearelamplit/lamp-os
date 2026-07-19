# Pass: config + NVS lifecycle

Verifies config persistence end-to-end: load, clamp, persist, propagate,
wipe. Run after changes to `config/` (codec, NVS store, disposition store),
the settings apply/drain path, or first-boot wiring in `main.cpp` /
`Lamp::setup` (the variant-resolution + NVS-timing invariants there are
subtle — see `docs/dev/lamp-framework.md` before touching them).

There is no serial verb for general config (serial ingress is
`{"a":...}` actions + `expr.get`/`expr.set` only), so the non-expression rows
drive over BLE — plaintext writes from host `bleak` work while no password is
set (see [ble-app.md](ble-app.md) for the feasibility basis and snippet
scaffolding).

## Hardware

- **1 lamp on USB serial** (beta/dev build), no password at the start.
- Host Bluetooth (bleak) or the phone app for the BLE-only rows (factory
  reset, name, password).
- Optional: a powered wisp — its 10 s roster dump prints lamp NAMES, the
  easiest mesh-side check for the name-propagation row.

## Steps

Snapshot first: `expr.get`, and note the boot line
`[cfg] loaded name=<n> pw=<set|unset> expressions=<n> nvs_bytes=<n>`.

**C1 — boot/config load lines.** `--reset` reboot; expect in order:
`[lamp] lampType="standard"` then the `[cfg] loaded ...` line matching the
known config. A `parse failed` variant of that line = corrupt NVS, instant
finding.

**C2 — persist across reboot.** `expr.set` a marker (dim pulse, long
intervals per README conventions), reboot via
`python3 scripts/bench_cmd.py <port> --reset --wait-ready '\[show\] ready'
--cmd 'expr.get' --duration 20`, diff against pre-reboot `expr.get` —
identical.

**C3 — persist across firmware re-flash.** NVS is not in the flashed image:
`PORT=<port> npm run lamp:flash` writes app only;
`PORT=<port> npm run lamp:flash:release` writes app0 + spiffs and skips NVS
(0x9000) + otadata by construction. After each, `[cfg] loaded name=` and
`expr.get` unchanged.

**C4 — clamp on load.** Two layers, both must hold:
- Runtime clamp: store an over-cap expression —
  `expr.set [{"type":"spotty","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#10000000"],"count":99,"size":98,"spotSpeed":99}]` —
  then trigger it (`{"a":"test_expression","type":"spotty","target":2}`).
  `expr.get` returns the raw stored values (stored as-given), but
  `configureFromParameters` clamps at use (count/size to the descriptor caps,
  even size rounded down to odd, spotSpeed to 1–10): expect a normal render,
  `[cmd] ok` + `[trigger]`, zero crash lines. Partially verified 2026-07-15.
- Codec clamp (BLE settings path, `config_codec.cpp`): out-of-range
  `socialMode` (>2) falls back to 1, `base.ac` beyond the color list resets
  to 0, segment `px` values clamp to the 255-pixel budget. Exercise via a
  settings write with a bad value and read the `lamp` section back clamped.

Verified 2026-07-15: a `{"lamp":{…}}` settings-blob write persists, then
intentionally reboots the lamp ~2 s later (`wantsReboot` path — rst:0xc, no
crash signature), so BLE readback must reconnect after the write. Uptime
checks require an ALREADY-OPEN serial monitor: any new port open resets the
lamp.

**C5 — first-boot variant resolution.** After C7's factory reset the resolved
type must still log `[lamp] lampType="standard"` (compiled variant wins; NVS
`lampType` is (re)written from the build). The cross-variant path logs
`[lamp] NVS lampType="<old>" overwritten with compiled "<new>"` — only
testable for real by flashing the other variant; optional row, log line
quoted here so a sighting is recognizable.

**C6 — password set + auth change + name propagation.**
- Set a password in the app. Boot line flips to `pw=set` after reboot;
  plaintext host writes are now silently dropped and auth attempts log
  `ACCEPTED`/`REJECTED` (full matrix in [ble-app.md](ble-app.md) P1 — run it
  there; here just confirm `pw=set` persists across reboot).
- Change the name (app or web UI). BLE advertisement updates WITHOUT a
  reboot (advertising restarts in place — verify with a bleak/nRF scan);
  the mesh HELLO carries the new name on its next cycle — steady-state that
  is up to 60 s (the 5 s burst interval only holds in the first ~30 s of
  uptime), so allow up to a minute; verify via the wisp roster dump (name
  column) or the app's fleet view on a second phone. The SoftAP SSID
  `<name>-lamp` updates only on the next boot.
- Remove the password before C7 so the reset write can go plaintext (or do
  C7 from the authed app session).

**C7 — factory reset (destructive; last).** The only mechanism is the
settings-blob sentinel `{"factoryReset":true}` — no serial verb, no button.
Write it to CHAR_SETTINGS_BLOB (`5f64f4d7-d6d9-4a44-9b3f-3a8d6f7e6b40`) from
bleak (plaintext, no password) or trigger it from the app. Expect:
`[loop] settingsBlob: factoryReset sentinel, wiping NVS`, a fade-out, a
self-reboot, then a fresh boot with `[cfg] loaded name=stray pw=unset
expressions=0 ...` and the AP back as `stray-lamp`. A
`[nvs] factory reset failed` line is an instant fail. (Co-shipping other
keys with the sentinel logs a WARNING and drops them — by design.)

Restore: reconfigure the lamp (name/colors/expressions) to its bench
baseline.

## Pass criteria

- Every row's boot line matches the expected config state; zero
  `parse failed` / `persist wrote 0 bytes` / `factory reset failed` lines.
- Config survives reboot and BOTH re-flash paths byte-identically
  (expressions diffed via `expr.get`).
- Out-of-range values never crash and never render un-clamped.
- Name propagates to BLE adv immediately and to HELLO within ~60 s
  (steady-state interval; 5 s only during the first ~30 s of uptime), no reboot.
- Factory reset wipes name/password/expressions/dispositions and lands on
  clean defaults in one self-reboot.

## Not covered here

- Cross-variant `lampType` overwrite (needs a snafu flash) — log line
  documented in C5, not executed.
- Home-mode WiFi credentials and knockout persistence — same NVS blob, not
  individually diffed here.
- NVS wear/fragmentation behavior over hundreds of writes.
- The disposition store's LRU eviction at 100 entries.

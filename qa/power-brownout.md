# Pass: power / brownout matrix

Pokes every stimulus known or suspected to trip the ESP32 brownout detector,
bisected across power sources. Exists because bright overlapping expressions
BOD-reset a USB-powered lamp on the bench (2026-07-15) and a reset mid-test
reads as "the lamp ignored me". Run after LED-drive, brightness-path, or
power-relevant changes — or when deciding whether a bench BOD is firmware's
fault or the supply's.

## Bench wiring caveat (2026-07-15)

Bench lamps power the LED strip THROUGH the dev board from USB — field lamps
feed the strip separately from the bank (~2 A) and the board only carries its
own draw. Through-board results (boot-inrush BOD, full-white BOD loop)
overstate field fragility: strip current sagging the ESP32 rail directly is a
bench-only circuit. Field-representative rows need the real wiring; the field
failure mode to hunt is the BANK sagging or latching off on overcurrent
(lamp goes dark until the bank's button is pressed), not the BOD.

## Hardware

- **1 lamp on USB serial** (beta/dev build), plus the ability to move it
  across power sources (human step).
- **A 2nd lamp** only for the OTA row. **Wisp** only for the wisp-paint row.
- Power sources under test:

| Source | Serial observation | Pass bar |
|---|---|---|
| USB 2 computer port | native | known-marginal: boot-inrush BOD is the documented artifact; mid-run BODs on the bright rows are possible — record, not a firmware failure |
| USB 3 / wall adapter (2 A+) | native | expected clean; any BOD is a finding |
| Battery | visual, or data-only USB tether | must NEVER BOD |
| Bench PSU (5 V, current headroom) | visual, or data-only USB tether | must NEVER BOD |

On battery/PSU the USB cable would back-feed 5 V and contaminate the row: use
a data-only tether (VBUS-isolated cable/adapter) if you have one, otherwise
verify visually — an unexpected jump back to the boot sequence is the BOD
tell, then re-tether and read the reset reason from the next boot.

## Serial signature

The convicting line is the ROM banner `Brownout detector was triggered`
followed by a reset. Filter every session with:

```sh
grep -E "Brownout|BOD|Guru|abort|\[cmd\]"
```

## Stimuli rows

Set brightness to 100% first (app or web UI) — the rows are worst-case by
design. Run the full row set once per power source.

**B1 — cold boot (inrush).** Power on from cold ×3. On USB 2, one BOD at
inrush is the known artifact; it must then boot clean. On battery/PSU: zero.

**B2 — full-white max-brightness hold.** Zone preview paints and HOLDS until
released — a serial-drivable steady-state worst case:

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd '{"a":"test_zone_preview","posMin":0,"posMax":999,"target":3,"color":"#FFFFFFFF"}' \
  --cmd 'sleep:30' \
  --cmd '{"a":"test_expression_complete"}' \
  -o /tmp/qa_bod_b2.log --duration 60 2>&1 | grep -E "Brownout|BOD|\[cmd\]"
```

**B3 — simultaneous expression burst** (the exact 2026-07-15 USB repro:
bright colors, overlapping one-shots on the same tick):

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3,"colors":["#FFFFFFFF","#FF00FF00"]}' \
  --cmd '{"a":"test_expression","type":"pulse","target":3,"colors":["#FFFFFFFF"]}' \
  --cmd 'sleep:3' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3,"colors":["#FFFFFFFF"]}' \
  --cmd '{"a":"test_expression","type":"breathing","target":2,"colors":["#FFFFFFFF"]}' \
  --cmd 'sleep:15' \
  -o /tmp/qa_bod_b3.log --duration 60 2>&1 | grep -E "Brownout|BOD|\[cmd\]|\[expr\]"
```

**B4 — OTA transfer** (flash writes + sustained radio TX concurrently). Run
the transfer from [ota.md](ota.md) steps 1–2 on this power source; watch both
lamps' taps for the signature. A BOD mid-transfer also aborts the OTA — the
receiver's no-progress watchdog firing right after a reset is the composite
tell.

**B5 — BLE connect during bright output.** Start B2's hold, then connect the
app (or the H1 bleak client from [ble-app.md](ble-app.md)) while it holds.
Radio + full LED load together.

**B6 — wisp paint at full brightness.** Wisp powered and painting a bright
manual palette (set via the app's wisp screen) at 100% lamp brightness; gate
on the lamp's `[recv] OVERRIDE_COLORS`, then watch 60 s.

## Pass criteria

- Battery and bench PSU: ZERO `Brownout` lines across all rows, including
  cold boot.
- USB 3 / wall adapter: zero expected; any BOD is recorded as a finding.
- USB 2: boot-inrush BOD tolerated (B1); mid-run BODs on B2/B3 recorded with
  the exact stimulus — they calibrate the "known-marginal" claim, they don't
  fail the pass.
- Every BOD that does occur must be VISIBLE in the filtered log (never let a
  reset hide — the grep includes the signature by design) and must recover to
  a clean boot without config loss (`[cfg] loaded name=` unchanged).

## Not covered here

- Actual current measurement / rail-droop scoping — no instrumentation
  assumed beyond the supplies themselves.
- Long-soak thermal behavior; rows are minutes, not hours.
- Snafu-variant power profile (different pixel counts/caps).
- Distinguishing WHICH rail sagged — the ESP32 BOD only says "mine did".

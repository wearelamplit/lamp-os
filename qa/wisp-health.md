# Diagnostic: wisp health triage

NOT a test pass — a check-ordered triage for "the wisp vanished / isn't
painting". Work top to bottom; each cause lists the exact evidence that
convicts or clears it. Run it whenever lamps stop showing wisp paint or the
app's wisp status goes stale.

The wisp is **USB-flash-only** (`PLATFORMIO_UPLOAD_PORT=<wisp-port> npm run
wisp:flash`) and its serial console is line-command based (`paint:on/off`,
`src:off|manual|aurora`, `wifi:show`, `wifi:set <ssid> <pass>`, `wifi:clear`,
`stage:on/off`) — no JSON action grammar like the lamps.

## First: get eyes on it

```sh
python3 scripts/bench_tap.py <wisp-port>:wisp -o /tmp/wisp.log
```

A HEALTHY wisp prints a roster dump every 10 s:

```
[wisp] roster (2 lamps):
  AA:BB:CC:DD:EE:01  flora         fw=1.1.1  age=1042ms
  AA:BB:CC:DD:EE:02  gramp         fw=1.1.1  age=3311ms
[wisp] zone=... source=... observed=...
```

plus, when painting: `[paint] walk Paint peers=N`,
`[paint] send Pair-><mac> seq=N base=r,g,b,w shade=r,g,b,w`, and for drift
`[drift] send Pair-><mac> seq=N fade=Nms`. Lamp-side confirmation of a
working link (LAMP_DEBUG build): `[loop] drain wispHello flags=... v=...`
and `[recv] OVERRIDE_COLORS surface=... src=... seq=... fade=...`.

## Check order

**0. Was it just unplugged?** The wisp is USB-powered. No serial output at
all → it's power/cable, not firmware. Also: opening the port reboots it;
give it a few seconds to rejoin.

**1. wispStatus JSON over the 576 B cap.**

```sh
grep "wispStatus JSON build failed" /tmp/wisp.log
```

Exact line: `[wisp.beacon] wispStatus JSON build failed`
(`status_emitter.cpp`; cap is `CONTROL_MAX_PAYLOAD = 576` in the shared
`control_op.hpp`). Effect: the status beacon stops (app-side wisp status goes
stale/absent) while paint keeps working. Fix is trimming whatever grew the
JSON.

**2. Protocol version skew after a PROTOCOL_VERSION bump.** Both sides
compile the shared `header.hpp` (`PROTOCOL_VERSION_EMIT = 0x05`, RX range
0x04–0x05), but the wisp only gets new firmware over USB — after a fleet
bump it silently falls off. Mismatch is a **silent drop on both sides**
(`lamp_protocol::inspect()` returns 0, no log), so the tell is
double-silence:

- wisp roster shrinks to `roster (0 lamps):` while lamps are up and meshing
  with each other;
- no lamp ever logs `[loop] drain wispHello`.

Convict/clear by comparing trees: what tree was the wisp last flashed from
vs what the lamps run. Fix: reflash the wisp from the lamps' tree.

**3. HELLO-buffer bug class** (fixed 2026-06-27, commit `f87b3045`: the
WISP_HELLO emit buffer was sized for the pre-TLV frame, the builder returned
0, and the emit path returned early — no presence beacon at all). A
recurrence looks like:

- wisp serial fully healthy — roster POPULATED (it hears lamps fine), paint
  sends flowing;
- lamps never log `[loop] drain wispHello`; the wisp never appears in the
  app;
- no error on the wisp side (the build failure is silent).

Distinguish from cause 2 by the roster: skew empties the wisp's roster,
this bug leaves it full. Guarded by `test_wisp_hello_build`; if the
signature matches, suspect any recent change to the WISP_HELLO build/emit
path.

**4. C6 BLE-scan starvation of ESP-NOW RX.** The wisp is a single-core
ESP32-C6; a continuous BLE scan starves mesh receive (the lamps' 1.5% scan
duty does NOT transfer). Signature: the wisp stops HEARING — roster ages grow
unbounded, then the roster empties — while its own TX (paint sends, beacons)
may keep going. The current wisp code runs NO BLE scan (Aurora comes in over
WiFi/WebSocket), so this cause only applies if someone reintroduced a
BLE-scanning feature — check recent wisp diffs for BLE scan code before
chasing it.

## Benign noise (observed 2026-07-15 — do not chase)

- `[mesh.send] FAIL to <mac>` while that lamp is mid-reboot (every bench_cmd
  port open reboots the lamp). Steady-state FAILs to a lamp that is up and
  meshing are real.
- After a lamp reboot, the wisp re-establishes its paint hold on that lamp in
  up to **~2 min**. Gate any downstream check on the lamp's
  `[recv] OVERRIDE_COLORS` line, not on wall time.
- Boot brownout on USB power (lamp side) — one BOD at inrush is the known
  artifact.

## Healthy verdict

Roster dumps every 10 s listing the expected lamps with ages that reset well
under `LAMP_PRUNE_TIME_MS`, paint/drift sends flowing when a source is
active, no `wispStatus JSON build failed`, and at least one lamp tap showing
`drain wispHello` + `OVERRIDE_COLORS`. If all that holds and lamps still
look wrong, the problem is on the lamp side —
[wisp-hold-matrix.md](wisp-hold-matrix.md) is the next stop.

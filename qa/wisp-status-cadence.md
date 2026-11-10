# Pass: wisp status cadence

Verifies that a wisp-screen open and a WiFi flap each send a single
wispStatus instead of the 20 s re-send burst an applied op opens. Run after
changes to `StatusCadence`, `StatusEmitter` or any of their call sites.

## Hardware

- **Wisp on USB serial**, dev build (`npm run wisp:flash`; dev carries
  `LAMP_DEBUG`, so every wispStatus emit prints one `[wispheap]` line).
- **1 lamp** powered and meshing, paired to a phone running the app.
- For D only: a WiFi AP the wisp joins that can be power-cycled. Without one,
  mark D skipped.

The wisp C6 does not reboot on a port open, so tail it with
`npm run lamp:tap -- <wisp-port>:wisp -o /tmp/wisp_cadence.log`, opened once
for the whole pass. Leave the app off the wisp screen between windows, and
send no wispOp during B or D: an applied op opens the burst and spoils the
count.

## Steps and pass criteria

`[wispheap]` lines counted in a window are status emits; the 30 s heartbeat
adds at most one to any 20 s window.

| Phase | Do, 4 windows | Pass |
|---|---|---|
| B: screen open | Open the wisp screen once, wait 20 s, leave it | `[wispheap]` per window <= 2 (one for the open, at most one heartbeat); a wisp before StatusCadence gives 10 |
| D: AP flap | Power-cycle the AP every 15 s for 2 min | `[wisp.beacon] connectivity edge` lines appear; `[wispheap]` <= 25 over the 2 min (at most one per 5 s), never the 2 s burst spacing |

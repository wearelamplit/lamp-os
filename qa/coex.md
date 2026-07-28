# Pass: wisp coex-hole meter

Reads how many `WISP_HELLO` broadcasts a lamp actually receives, without
changing any behavior. Exists to settle whether BLE/ESP-NOW coex (the
proximity idle scan bursts in particular) is causing the paint holes
described in
[`docs/superpowers/specs/2026-07-20-wisp-reconciliation-design.md`](../docs/superpowers/specs/2026-07-20-wisp-reconciliation-design.md),
and later to confirm the reconciliation fix closes them.

## What it measures

The wisp broadcasts `WISP_HELLO` every 2 s with a monotonic `seq`. The lamp's
`MeshLink::handleRecv` (`components/network/mesh/mesh_link.cpp`) feeds every
received `seq` into a per-wisp `WispCoexMeter`
(`components/network/mesh/wisp_coex.hpp`) before the dedup ring consumes it,
so a wisp-side gossip-relayed duplicate is counted too, same as a direct hit.
Every ~10 s per wisp, a dev build (`LAMP_DEBUG`) prints:

```
[wispcoex] wisp=EB:64 recv=418 missed=7 loss=1.6% maxgap=4200ms
```

- `wisp` — last two MAC bytes of the wisp (enough to tell multiple wisps
  apart on a shared serial log).
- `recv` — hellos actually received, cumulative since boot.
- `missed` — gap-inferred drops (`seq` jumped by more than 1), cumulative
  since boot. A wisp reboot (seq resets near 0) is detected and excluded, not
  counted as a spike.
- `loss` — `100 * missed / (recv + missed)`, cumulative.
- `maxgap` — longest span between two consecutive hellos since the last
  printed line, then reset. This is the field that matters for the drop bug:
  the colour-override watchdog (`kPaintWatchdogMs`, `color_override.hpp`) trips
  at 60-100 s of silence. `maxgap` climbing into the tens of seconds means a
  drop is close; a `maxgap` that clears the watchdog means one already
  happened between reads.

## Reading it

- `loss` in the low single digits with `maxgap` well under the watchdog:
  normal RF jitter, not the bug.
- `maxgap` repeatedly approaching the watchdog window: broadcast holes are
  real and long enough to matter, independent of `loss`% (a handful of long
  holes can carry a low overall `loss`% and still trip the watchdog).

## Lamp-side scan is uniform continuous

The lamp runs a single low-duty (1.5%) BLE scan with no periodic high-duty
burst, so there is no lamp-side scan-burst A/B: a `maxgap` that clears the
watchdog points at C6 wisp-side TX/RX starvation or general RF conditions,
not a lamp scan burst.

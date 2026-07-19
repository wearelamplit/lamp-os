# Pass: power governor

Proves the supply-budget governor clamps when frame demand exceeds the
budget, stays byte-identical to an ungoverned build when it doesn't, and
releases cleanly. Run after any change to the governor state machine, the
estimator constants in led-common, or the brightness funnel.

## Hardware

- **1 lamp on USB serial**, dev-channel build — `gov.get` and the `[gov]`
  engage/release lines are LAMP_DEBUG-gated.
- **The app in dev mode** (or the web config UI): the stimuli ride the
  test-panel actions; there is no serial stimulus path.
- Battery or bench PSU for the pass-bar rows. A bench lamp that feeds the
  strip through the dev board from USB overstates field fragility — field
  wiring powers the strip from the bank directly. USB rows are
  known-marginal: record BODs, don't fail on them.

## Constants under test

`core/power_governor.cpp`: reserve 400 mA while the radio is hot or the
lamp is booting, 200 mA quiet; demand is sensed every frame at the flush
boundary, before that frame's pixels are written. Any frame over budget at
the applied level is clamped to the level that fits before it reaches the
strip; while clamped, per-frame re-solves only move the ceiling down.
Release at 88 % of budget, paced at 1 s; 400 ms ceiling glide on release;
boot hold at ceiling 128 for 5 s, full by 10 s. `led_power.hpp` (shared led-common): 10 mA per channel full duty,
0.7 mA idle per pixel — idle strip draw counts as demand, not reserve.
`HwConfig::supplyBudgetMa` is 1400 on both variants.

Design-point anchors (native `test_power_estimator`: 64 px, 2 A supply, quiet
reserve → 1800 mA pixel budget, requested 255): clamp level ≈350 vivid
two-channel (full magenta), ≈233 three-channel white, ≈174.5 four-channel
full white. Bench numbers shift with pixel count, radio reserve, and the
max-brightness scale — sanity anchors, not pass bars.

## Serial signature

```sh
grep -E "Brownout|\[gov\]|\[power\]|\[cmd\]"
```

`gov.get` (typed into the serial monitor) replies on one line:

```
[cmd] ok gov state=<boot|dormant|clamped> demand=<mA> age=<ms> budget=<mA> ceiling=<0..255> requested=<0..255>
```

`demand` is estimated at the requested (pre-clamp) level, so it stays above
`budget` for as long as the stimulus holds — read engagement from `state`
and `ceiling`, not from demand dropping. `age` is milliseconds since the
last flushed frame was sensed; more than a few hundred ms means frames have
stopped flushing. `requested` is the post-scale driver level (0–255 after
maxBrightness), not the app's 0–100 slider. A connected app flips the
radio-hot reserve (400 mA vs 200 mA quiet): expect `budget=1000` while
connected, `budget=1200` quiet.

`[gov] clamp` marks a same-frame clamp of an over-budget frame — that
frame goes out pre-clamped. One line per engagement; deeper snaps while
clamped stay silent. `[gov] release` marks the return to dormant.

## Stimuli rows

Set lamp brightness to 100 % first. Run once per power source.

**G1 — clamp and release.** From the app, hold a full-range full-white zone
drag in the color editor (or fire overlapping white `glitchy` + `pulse`
expressions on both surfaces from the dev test panel). Send `gov.get` at
~0 / 5 / 15 s into the hold, then release.
Pass: the white step goes out pre-clamped — no unclamped window, no BOD on
any supply at or above the budget. `[gov] clamp` on the step; `ceiling`
settles below `requested` and holds steady, zero `Brownout` lines,
`[gov] release` after the stimulus ends, then `state=dormant` with
`ceiling=255`. If
`gov.get` shows `demand` under `budget` during the hold, the lamp has too
few pixels to engage — the row is vacuous on that hardware; note it.

**G2 — dormancy proof.** Ambient scene, then a W-only white scene, both at
100 %, 60 s each. Pass: `gov.get` stays `dormant` with `ceiling=255`, zero
`[gov]` lines — brightness identical to an ungoverned build.

**G3 — boot ramp.** Cold boot ×3: output starts visibly dimmer (ceiling
128), glides to full by ~10 s, `gov.get` reports `boot` then `dormant`.
Expected, not a finding. A `[power] brownout reset` line at boot is the
tell that the supply-budget constants are mis-sized for the hardware.

## Pass criteria

- Battery / bench PSU: zero `Brownout` lines across all rows, including
  cold boot.
- G1 clamps on the step, holds a steady sub-requested ceiling, and
  releases to `dormant` / `ceiling=255` with the `[gov] clamp` line and
  the release line both visible in the filtered log.
- G2 shows zero governor interference at legitimate full brightness.
- G3 boot ramp visible on every cold boot; no `[power] brownout reset`.

## Results log

| Date | Build | G1 clamp | G2 dormancy | G3 boot ramp | Notes |
|---|---|---|---|---|---|
| 2026-07-16 | `36ea1a6f` | **PASS** — `[gov] clamp demand=2372 budget=1600 ceiling=147` same-millisecond as the white-step drain; 14 s sustained full-white on a 5 V/3 A bank, zero BOD; `[gov] release demand=1001` 200 ms after clear, paced glide | **PASS** (demand ~530, ceiling 255, pass-through) | **PASS** (128 hold → glide → 255; budget 1600→1800 flip; demand age ≤ 9 ms) | Live greet ran through the clamp window; composed cleanly. Bench wiring: strip fed from bank, field-representative. |

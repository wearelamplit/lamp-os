# Pass: wisp color drift

Verifies the app's Color drift sliders change what the lamps show. Run after
changes to drift scheduling (`PaintDistributor`), the drift fade
(`driftFadeMs`, `WispRoster::setLampDrift`) or the STATE frame builder.

## Hardware

- **Wisp** flashed from this tree
  (`PLATFORMIO_UPLOAD_PORT=<wisp-port> npm run wisp:flash`, a dev build, so
  each drift step prints `[drift] <mac> fade=Nms`), in Manual with a palette
  of a few clearly different colours.
- **Two or more lamps** the wisp claims, side by side.
- Phone with the app, paired to one of the lamps.
- Optional: tap the wisp to read each step's fade
  (`npm run lamp:tap -- <wisp-port>:wisp -o /tmp/drift.log`; the C6 wisp does
  not reboot on port open).

## Key facts the timing depends on

- One lamp re-colours per slot, so every lamp moves once per How often
  interval: with 2 lamps at 2 min, one of them starts a new colour every
  60 s and each lamp every 2 min.
- A step's fade is drawn from 20 s up to 20 s + (interval − 20 s) × Fade %.
- STATE carries the fade every ~2 s, so a long fade moves in small steps.

## Steps

| # | Check | Do | Pass |
|---|---|---|---|
| 1 | Fade low vs high shows on the lamps | Set How often to 2 min and Fade to 0% (Snappy). Watch each lamp change colour twice. Then set Fade to 100% (Flowing) and watch two more changes. | At 0% each change settles in about 20 s and the wisp log shows `fade=20000ms` on every step. At 100% changes take visibly longer, often most of a minute or more, with log fades between 20000 and 120000 ms. No change at either setting snaps in a second. |

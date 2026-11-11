# Pass: wisp color drift

Verifies the app's Color drift sliders change what the lamps show. Run after
changes to drift scheduling (`PaintDistributor`), the drift fade
(`driftFadeMs`, `WispRoster::setLampDrift`), the STATE frame builder or its
drift fade trailer, the lamp's wisp layer (`LayerStack::syncWispFade`), or the
lamp's fleet paint cache (`LampRoster::cacheWispState`).

## Hardware

- **Wisp** flashed from this tree
  (`PLATFORMIO_UPLOAD_PORT=<wisp-port> npm run wisp:flash`, a dev build, so
  each drift step prints `[drift] <mac> fade=Nms`), in Manual with a palette
  of a few clearly different colours.
- **Two or more lamps** the wisp claims, side by side. For row 2, lamp A
  dev-flashed from this tree (each drift fade start prints
  `[loop] wispState drift fade start`, a retime `... retime`) and lamp B on
  the 1.3.1 beta.
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
| 2 | New lamps fade smoothly, older ones step | How often 30 s, Fade 100%. Open one `npm run lamp:tap -- <A-port>:a <B-port>:b -o /tmp/drift.log` and leave it open. Watch several steps. Power-cycle A mid-fade. Change the palette mid-fade. Then switch to a Manual colour and to Off. | A fades continuously; its log shows one `drift fade start` per step and no `retime`. B moves in 2 s steps as before. After the power cycle A fades in and settles beside B within about 1 s. A palette change moves both within about 0.5 s. Manual and Off land as before. (The partial-frame form needs 58+ claimed lamps; native tests cover it.) |
| 3 | The app's chips show each lamp's next colour | App installed from this tree (`npm run app:install`), phone paired to lamp A, the wisp's Lamps tab open. How often 30 s, Fade 100%. Watch a lamp start a step, then tap Shuffle mid-fade, then tap it twice more a second apart. | When a step starts, that lamp's chips jump to its new colour while the lamp itself is still fading toward it. Each Shuffle changes every chip within about 3 s and the shuffle icon spins until the wisp reports the re-roll; no chip shows an in-between colour. The lamps themselves change within about 0.5 s of each Shuffle. |

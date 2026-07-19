# Pass: personality / social matrix

Deterministic personality + greeting pass driven by the `inject_nearby` /
`clear_nearby` / `triggerGreet` debug verbs. Run after changes to
`core/personality_engine.*`, `behaviors/social.*`, the disposition store, or
`docs/dev/personality-greetings.md` (the profile contract). Snafu-variant
greeting/behavior content is explicitly OUT of scope (owner decision).

Two facts shape every row (verified in code):

- **Greets fire from the LIVE roster** (`nearbyLamps.getUngreetedArrivals`),
  not from the injected override. `inject_nearby` shapes the personality
  engine (crowd dim, composition) but never fires a greet, and injected
  peers carry no `lampId`, so `triggerGreet` cannot target them either. Greet
  rows need the real peer lamp.
- **Dispositions are lampId-keyed** (mesh-mac). `inject_nearby`'s `disposition`
  field writes the store keyed by the injected `name` — so injecting a peer whose
  `name` IS the real peer's lampId is the serial-side lever for setting that
  peer's disposition (the store doesn't validate the key on set; a real
  lampId-form key also survives the load-time filter).

## Hardware

- **1 lamp on USB serial** (beta/dev build — the inject verbs are
  LAMP_DEBUG-only), Ambivert unless a row says otherwise (socialMode is set
  via the app).
- **1 real peer lamp**, powered, no serial needed. Get its lampId from the
  app's fleet screen.
- **Home mode MUST be off** — this branch suppresses ALL social behavior
  (greets included) while home mode is active, silently. Verify first: bleak
  page-read of the `home` section → `enabled:false` (or `networkBound` bound
  and away). A lamp with `enabled:true, networkBound:false` never greets.
- **lampId keys are UPPERCASE** — the roster stores uppercase; the
  disposition store compares case-sensitively. `B8:D6:1A:44:A3:5E`, not
  lowercase (lowercase writes are silently inert — known app-side bug).
- **No app BLE-connected during greet rows** (scan pauses under a
  connection — greets won't fire; by design).
- No wisp.

## Steps

**M1 — injected crowd → crowd dim → clear returns to live.** Crowd dim only
applies to Introvert (floor 0.5) and Ambivert (floor 0.7); Extrovert is
exempt. There is NO serial log for the dim — verification is visual
(brightness at a steady mid level makes the drop obvious):

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd '{"a":"inject_nearby","peers":[{"name":"qa1","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa2","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa3","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa4","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa5","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa6","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa7","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa8","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa9","baseColor":"#10000000","disposition":3,"rssi":-40},{"name":"qa10","baseColor":"#10000000","disposition":3,"rssi":-40}]}' \
  --cmd 'sleep:45' \
  --cmd '{"a":"clear_nearby"}' \
  --cmd 'sleep:45' \
  -o /tmp/qa_pers_m1.log --duration 120 2>&1 | grep -E "\[cmd\]|\[personality\]|BOD|Guru|abort"
```

Expect `[personality] inject_nearby count=10` + `[cmd] ok`; brightness
visibly settles toward the mode floor within ~10–30 s (1 Hz median + EMA
smoothing, 80 ms micro-fades — a glide, not a snap), and after
`[personality] clear_nearby` it recovers to full on the same timescale. With
the real peer nearby, post-clear behavior (greeting it on arrival) confirms
live data resumed.

**M2 — disposition extremes shape the natural greet (log-verified).** Set
the REAL peer's disposition via the lampId-as-name trick, then produce a
natural arrival:

```sh
--cmd '{"a":"inject_nearby","peers":[{"name":"<PEER_LAMPID>","baseColor":"#10000000","disposition":1}]}' \
--cmd '{"a":"clear_nearby"}'
```

(The inject writes the disposition store; the immediate clear drops the fake
roster.) Power the peer OFF for ≥2 min (prune window — resets its
greeted/acknowledged state), then ON, and watch for the greet line:

```
[social] greet <peername> (mode=M frames=F pulse=P count=C)
```

`mode` is the lamp's own socialMode (0=Introvert 1=Ambivert 2=Extrovert);
frames/pulse/count must match the `kProfile*` constants in
`personality_engine.cpp` (frames at 60 fps). Ambivert expectations:

| disposition | profile | frames | pulse | count |
|---|---|---|---|---|
| 1 (Salty) | Snub | 540 | 191 | 1 |
| 5 (Smitten) | Enthused | 1512 | 65 | 255 |

(Code sets full-snub dim to `kFullSnubDim = 191`; count 255 = `kPulseCountContinuous`.)
Repeat with disposition 5.
**Same-peer pacing:** Ambivert re-greets the same peer at most every 5 min
(Introvert 10 min); for fast iteration set the lamp Extrovert in the app
first — 26 s base cooldown, no per-peer window, expectations become Snub
frames=540/pulse=191/count=1 and Effusive frames=1620/pulse=80/count=255.
Restore the peer's disposition to 3 at the end (same inject with
`"disposition":3`).

**M3 — triggerGreet (immediate, visual-only).** With the peer live in the
roster (recently scanned):

```sh
--cmd '{"a":"triggerGreet","lampId":"<PEER_LAMPID>"}'
```

This path bypasses all cooldowns and — unlike the natural path — emits NO
`[social] greet` line (only the `[cmd] ok a=triggerGreet` ACK). Verify by
eye: with disposition 1 the shade snubs (~4.5 s dip-to-dark in the peer's
color); with disposition 5 under Extrovert it pulses continuously for ~24 s.
The duration difference is unmistakable with a stopwatch. If nothing plays,
the peer wasn't in the live roster (stale scan or app connected).

**M4 — cooldown / no-respam.** Ambivert. After an M2 greet fires, power-cycle
the peer again (off ≥2 min, on) INSIDE the 5-min regreet window: NO second
`[social] greet` for that peer — there is no "suppressed" log, absence of the
line is the signal, so keep the tap running across the whole window. After
the window lapses, the next arrival greets again. Optional Introvert fatigue
check: 3 greets within 5 min logs `[social] introvert tired until +300000 ms`
and blocks further greets for 5 min.

**M5 — greet gradient (MSG_COLOR_QUERY / MSG_COLOR_INFO) — still-open bench
verify.** The color exchange at greet start is COMPLETELY silent under
LAMP_DEBUG (verified: no log on query send, query receive, info send, or
info receive) — this row needs eyes on the lamp. Configure the PEER with a
multi-stop base gradient (2+ distinct colors), trigger a natural greet (M2
flow): during the hold phase the shade should start at the peer's solid base
color and blend into the peer's base GRADIENT over roughly a quarter second
(blend ramps ~12 frames). Failure mode: the greeting holds a flat single
color for a peer whose base is clearly multi-colored. Record the observation
either way — this exchange is merged but has never been bench-verified.

## Pass criteria

- M1: inject/clear ACK lines; visible dim toward the mode floor and full
  recovery after clear; zero crash lines.
- M2: greet line fires with frames/pulse/count exactly matching the profile
  table for each disposition × socialMode tried.
- M3: correct waveform + duration by eye for both extremes; ACK present.
- M4: zero greet lines inside the regreet window, greet resumes after it.
- M5: gradient blend observed (or the flat-color failure recorded as a
  finding).

## Not covered here

- Snafu greeting/behavior redesign — out of scope by owner decision.
- Closest-Smitten 45 s recurring pulse: keys off the closest peer's lampId,
  so it is NOT injectable (injected peers have none); needs a real peer set
  Smitten and a long observation window — fold into a future session if it
  regresses.
- Crowd-dim numeric factor (not exposed on any interface — visual only) and
  Ambivert's disposition-weighted crowd pressure (injected peers all read
  Neutral for composition, since composition also keys off lampId).
- Greet suppression under an app connection — covered in
  [ble-app.md](ble-app.md) P5.

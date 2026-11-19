# Moments

Rare crowd-level expressions driven by dispositions: a room of lamps that like
each other sometimes sparkles together or passes a glow from lamp to lamp.
Always on, no setting.
Part of the social system ([`social.md`](social.md)). **The code wins ties**;
update this doc when it doesn't.

Code: `core/moment_math.{hpp,cpp}` (pure arithmetic), `core/moments.{hpp,cpp}`
(`MomentDirector`, global `momentDirector`), `core/lamp_moments.cpp` (binds it
to the live lamp). Native suite: `test/test_moments/`.

Numbers marked **(T)** are starting values for the bench; the rest is contract.

## What a Moment is

An **orchestration layer**. The director owns triggers, pacing and the mesh
hand-off and renders nothing itself: every pixel comes from an existing
expression fired as a one-shot transient through
`ExpressionManager::triggerInvocation(inv, selfMac, /*broadcast=*/false,
TransientSource::Remote)`. No expression type is added for it.

- `broadcast=false`: a Moment render never cascades and never announces a
  `MSG_EVENT`, so no peer mirrors it.
- `Remote`: Moments compete only for the two peer transient slots, so the two
  held for the lamp's own Test and bloom stay free. A render refused for want
  of a slot or heap is skipped and the timeline carries on.
- One surface per render, so one slot per render.
- One Moment per lamp at a time, of any kind.

## Kinds and their renders

Every Moment renders on the **base**: the shade hides subtle effects. None
renders in the base's own colours, so each stands out there.

| Kind | Expression | Colour | Params |
|---|---|---|---|
| Fireflies (one beat) | `flicker`, Twinkle burst | this lamp's shade colours (up to 4, one per beat) | `fire=0`, `count` = 25% of the base's pixels **(T)**, `durationMax=700` ms **(T)**, `opacity` per envelope (10 to 25) **(T)** |
| Whisper (one glow) | `pulse` | the origin's base colour; the origin itself glows in its shade colours | `loop=0`, `fullStrip=1`, `pulseSpeed=3`, `size=6`, `easing=1`, `opacity=30` **(T)** |

**Why flicker.** Fireflies is many soft points: each lamp's beat fires a short,
faint Twinkle burst of stars, so a crowd sparkles together on the shared
rhythm. Glitchy is not used.
Breathing clamps to at least 8 s a cycle and pulse to at least 100 ms a pixel,
too slow for a beat; spotty runs independent random lifecycles that cannot
land on a shared beat.

**The flicker burst.** Flicker is continuous and its sparks roll at random per
frame, so a trigger alone does not sparkle on cue. Two existing keys it does
not otherwise read make a trigger one short burst, hidden from its descriptor
so the app, `exprcat` and `test_builtin_descriptors` are unchanged:

- `count` (stars) and `durationMax` (burst length, clamped 100 to 10000 ms),
  both above 0, on a transient (an instance whose auto-trigger is off). Absent
  either, or on a configured, auto-triggering flicker, it runs continuously as
  before.
- At trigger exactly `count` cells (clamped to the zone) take a spark target
  from the style's spark band; the rest stay at rest. Sparks rise for the first
  half of the burst and fall for the second (six step constants across it, so
  a spark reaches about 95% and falls back to about 5%), and the instance
  completes at `durationMax`, so the transient GC reaps it. A burst longer
  than 2000 ms (`kBurstSwellMaxMs`) keeps the 2000 ms burst's step and its
  1 s fall, and holds its stars at peak from the rise until 1 s before the
  end; up to 2000 ms the shape is unchanged, so Fireflies' 700 ms beat is too.
- A cell below `kBurstPaintHeat` (0.37, the heat at which Twinkle paints its
  own anchor) is left untouched, so sparks fade in and out of the surface
  instead of dimming the cells around them. A fully saturated channel has no
  headroom to brighten into, so an untinted burst on a white lamp reads weaker.
- A colour in the invocation's colour list **tints** the burst: one is picked
  per trigger, and each star blends from the colour under it at the paint
  threshold to the tint's own warmth ramp at the hottest spark. Only burst
  mode reads it; a configured flicker, whose descriptor offers no colours,
  ignores it.
- No key is added to `kExpressionParamKeys`, so `ExpressionParams` and stored
  configs are unchanged. Math and tests: `expressions/shimmer/shimmer_math.hpp`,
  `test/test_shimmer_math/`; the real expression driven to completion:
  `test/test_shimmer_burst/`.

**Spore tint.** A mushroom's spore reaches nearby lamps as a `MSG_COMMAND`
whose invocation is a tinted flicker burst that also carries `fadeDuration`
(seconds), a key flicker does not read. Every lamp plays the burst. A lamp
that knows the spore tint (`SporeTint`, `expressions/spore_tint.hpp`) books a
tint and starts it from the loop once the burst's length has passed and the
burst has been reaped, so the tint takes the Remote slot the burst held. The
tint is a transient `shifty` from the same sender on the base: Bloom fill, 4 s
fade in, 20 s hold, opacity 80 **(T)**, fading back over the command's
`fadeDuration`, capped at 100 s. Bloom staggers pixel starts, and shifty
holds each fade's window open for half a fade past the fade length
(`ShiftyExpression::maxCycleMs`), though every pixel has landed by then. So
at the cap the tint holds its slot for 6 s + 20 s + 150 s = 176 s, the last
50 s with nothing changing on the strip, inside the 180 s transient lifetime
(a static assert in `spore_tint.cpp` ties the two). Only a
command that started its burst at once (no `delayMs`) is followed. While a
tint is booked or a peer's `shifty` transient is in flight, another spore
still sparkles but tints nothing. A booked tint the manager refuses when its
time comes (home mode, no Remote slot free) is dropped. No lamp sends this
shape: flicker is continuous, so the cascade and announce paths never send
it, and Fireflies' beat carries no `fadeDuration`. Tests:
`test/test_spore_tint/`, `test/test_shifty_one_shot/`.

**Colours.** Whisper's origin
colour rides the frame as `c`, so a hop far from the origin still has it. Any
colour within 48 (`colorDistance`) of any of this lamp's base colours is whitened
40% with `bloomWhiten`, so it still shows.

## Mesh

Moment traffic rides **`MSG_EVENT` (0x30)**: authenticated, single-hop,
nearby, no relay. The payload is invocation JSON with a sentinel `type` and one
top-level `mo` object:

```json
{"type":"moment","mo":{"k":1,"s":3405691582,"a":0}}
{"type":"moment","mo":{"k":2,"s":3405691582,"h":3,"a":0,"to":"AABBCCDDEEFF","v":"<12 hex per visited MAC, origin first>"}}
```

| Key | Meaning |
|---|---|
| `k` | Kind: 1 Fireflies, 2 Whisper. Any other `k` is ignored whole, so later kinds stay additive. |
| `s` | uint32 seed, also the Moment id. |
| `a` | Age in ms since the sender's reference instant, 0 to 2000: the Fireflies `t0`, or the Whisper sender's glow. A receiver recovers that instant as `arrival - a`. |
| `h` | Whisper: lamps visited so far, 1 to 8. |
| `to` | Whisper: MAC of the next hop. |
| `v` | Whisper: visited MACs, origin first; exactly `12*h` hex chars. |
| `c` | Whisper: the origin's first base colour, packed `rrggbbww` lowercase hex. Optional; any other form drops the frame. A hop without it glows in its own shade colours. |

The worst case (Whisper, `h=8`, with `c`) is 198 B, inside `EVENT_MAX_PAYLOAD`
(230) and the classic 250 B frame; `test_moments` pins it. No new message type
and no protocol bump: `parseInvocation` ignores unknown keys, and an added JSON
key changes no parser contract. `MSG_EVENT` rather than `MSG_COMMAND` because a
command is filtered by `addressedToUs`, and every bystander has to overhear a
Whisper hop to learn the crowd cooldown.

**Receive.** `Lamp::drainEvent` hands the payload to `deliverEvent`: one
carrying `mo` goes to `momentDirector.onFrame` and returns before the
expression mirror's fan-out, so the mirror never sees a Moment.

**Older lamps** parse the frame as an invocation of type `moment`. A warm
mirror may schedule a replay, which `makeExpression` refuses (no such type):
no allocation, no render, no forward. The roll still spends that mirror's
chance (and an Introvert's mirror cooldown) on a frame that shows nothing, so
an older lamp near a Moment mirrors its peers' real expressions a little less
often until it updates. A Whisper addressed to an older lamp
dead-ends there. No variant registers the id `moment`
(`test_builtin_descriptors`).

**Reliability.** Every frame goes out twice, 200 ms apart **(T)**, each with a
fresh `a`; the director's dedup ring collapses the copy. A send waits for
`canSend` (TX credit and no OTA) for up to 1 s per copy, then gives up; a
Fireflies start whose first copy never went out ends the Moment, and the
initiator's growth still applies.

**Airtime.** Fireflies 2 frames a Moment, Whisper 2 a hop (16 at most).

## Pacing

All in RAM, reset on reboot, nothing in NVS. Per kind:

| Kind | Start range | Growth per participation | Cap | Crowd cooldown | Initiation backoff |
|---|---|---|---|---|---|
| Fireflies | 20 to 60 min | +30 min both ends | 6 steps (200 to 240) **(T)** | 45 min **(T)** | U(0, 90 s) **(T)** |
| Whisper | 8 to 20 min **(T)** | +10 min **(T)** | 6 steps (68 to 80) **(T)** | 10 min **(T)** | U(0, 30 s) **(T)** |

- **Wait.** At boot and after each participation:
  `U(lo + step*g, hi + step*g) * mode`, with `g` capped and mode Introvert 1.5,
  Ambivert 1.0, Extrovert 0.7 **(T)**. Social mode scales only the lamp's own
  wait. A mode change mid-wait does not re-roll. The lamp is **eligible** once
  the wait has passed and stays eligible until it takes part.
- **Participation** (initiating, joining a Fireflies, accepting a Whisper)
  grows `g` up to the cap, re-rolls the wait from now and sets the kind's
  crowd cooldown.
- **Crowd cooldown.** Any lamp that receives a valid frame of a kind, taking
  part or not, sets that kind's cooldown to the later of its current end and
  now plus the cooldown. It blocks **initiation only**: joining and accepting
  stay open, so a running Moment is never cut short by its own cooldown.
- **Herd control.** An eligible lamp whose conditions hold arms a backoff of
  U(0, max) once (`FastRng`, seeded from the MAC and `esp_random`). At expiry
  it re-checks eligibility, conditions, the cooldown and the one-Moment rule,
  then initiates. Hearing a frame of the kind meanwhile sets the cooldown, so
  the expiry refuses. If conditions lapse the backoff disarms and a later
  return rolls a new one. With N eligible lamps the first two expiries are
  about 90/N s apart, 1.8 s at N = 50 against about 30 ms of propagation. A
  Whisper origin announces its first hop the moment it starts, so its frame
  is what cancels the others. The herd simulations in `test_moments` (every
  lamp eligible at once, a fake bus with 30 ms latency, 1000 seeded runs each)
  start exactly one Moment in: Fireflies, 50 lamps, 975; Whisper, 5 lamps,
  993; 10 lamps, 990; 20 lamps, 971. None started more than two. Two that do
  start together split the room: each hearer joins the first start it hears.

Rough rates: a room sees a Fireflies every 45 to 60 min early in a session,
slowing as ranges grow; a Whisper every 10 to 15 min per radio neighbourhood.

## Kinds

Peers come from `PersonalityEngine::nearbyPeers()`, the near view crowd-dim
samples (and the `inject_nearby` override). A peer is **fresh** when a BLE
advert from it was heard within 15 s **(T)**, read off the roster's
`lastSeenBleMs` with a signed compare. Only a BLE sighting refreshes
`lastRssi`; a direct ESP-NOW HELLO renews `lastSeenNearMs` (presence) while
the RSSI stands still, so every RSSI threshold reads freshness off the BLE
stamp. **Liked** means Fond (4) or Smitten (5). Every scan is in place, with no
copy and no sort.

### Fireflies

**Initiate** when eligible, not cooling, unsuppressed, no greeting active, and
at least 3 liked peers are in the near view and that liked set has held still
for 10 min **(T)**. Stillness is a fingerprint, `count<<24 ^ xor(hash(mac))`
over the liked peers, recomputed at 1 Hz; any change (arrival, departure,
disposition edit) restarts the clock. Departures register when the 240 s near
window lapses. The set is the liked peers, not everyone near: a busy room is
never still for 10 minutes, and the liked set is who takes part.

**Start.** Roll a seed, set `t0` to now, send `{k:1,s,a}`.

**Join** on hearing a start when not in a Moment, unsuppressed, and this
lamp's disposition toward the initiator is Neutral or warmer; Salty and Wary
lamps sit out, which shows on the rig. The cooldown does not block a join.

**Timeline**, identical on every lamp, from `s` and the lamp's own MAC:

- period `T = 1000 + s mod 600` ms **(T)**; beats `R_k = t0 + k*T` for k = 1 to 120 **(T)**;
- first offset `d_1 = hash(s ^ macLow32) mod T - T/2`, in [-T/2, T/2);
- beat k fires at `R_k + d_k`; for k < 9 `d_{k+1} = d_k` (scatter), then
  `d_{k+1} = round(d_k * 0.94)` **(T)** in integer arithmetic, snapping to 0
  under 10 ms. From the worst offset that is zero by beat 84 (about 90 s), then
  lockstep. Integer rounding keeps `d_k` identical on every lamp and in the
  native vectors.
- opacity ramps 10 to 25 over beats 1 to 6, holds 25 to beat 110, ramps to 10
  by 120 **(T)**; the Moment ends after beat 120 (about 2.6 min).

A late joiner recovers `t0` from `a` and starts at its first beat still ahead.
Radio and drain latency (about 30 ms) and crystal drift (about 3 ms over the
Moment) sit below what the eye resolves at a 1 to 1.6 s period.

**Why a seeded nudge and not coupling to heard blinks.** Coupling means every
lamp broadcasts every beat (about 6% of airtime for 50 lamps for minutes); once
in sync all 50 transmit in the same milliseconds and collisions plus the
single-slot, newest-wins event mailbox drop nearly all of them; older lamps'
mirrors would replay them; and the result is not reproducible on the bench.
The seeded nudge looks the same, scattered beats drawing into one, for 2
frames. The owner chose it.

### Whisper

**Initiate** when eligible, not cooling, unsuppressed, no greeting active, and
a fresh peer is Smitten at -70 dBm or better or Fond at -62 dBm or better **(T)**.
The origin glows and, in the same instant, hands on as below with `h=1`,
`v=[self]`, so every bystander hears the Whisper at its start.

**Accept** a frame when `to` is this lamp, `1 <= h <= 8` and `len(v) == 12*h`,
`(v[0], s)` is not in the dedup ring, the lamp is not in a Moment, nothing
suppresses it, and its disposition toward the sender is Neutral or warmer. A
Wary or Salty lamp drops it and the chain ends, visibly. The cooldown does not
block an accept. On accept: count the participation and glow at
`arrival - a + 3 s` **(T)**, one hand-off after the sender's glow (about
mid-sweep, so the light reads as travelling on); hand on in the same instant if
`h < 8`. The lamp is in the Moment from the accept to its glow.

**Hand-on.** An in-place scan picks the peer that is fresh, liked by this
lamp, at -80 dBm or better **(T)**, not self and not in `v`, strongest first,
ties to Smitten then the lower MAC. It sends `{k:2, s, h+1, to, v + [self]}`;
with no candidate the chain ends. There is no acknowledgement: a lost frame or
a refusal ends the chain.

**Loops and duplicates.** `v` stops a lamp being picked twice; the dedup ring
on `(v[0], s)` stops a lamp glowing twice for one Whisper, a resent copy
included; `h <= 8` bounds a chain at 8 hops (the origin plus 8 lamps glow),
16 frames, and the payload.

## Suppressors

No Moment initiates, joins or accepts while one holds; one arising mid-Moment
ends it (the outbound frame with it), and transients already drawing finish on
their own.

| Suppressor | Check (`core/lamp_moments.cpp`) |
|---|---|
| OTA | `meshLink.isOtaInProgress() \|\| ota_quiet_mode::isQuiet()` |
| Wisp holds a surface | `compositor.wispPresence(false or true) > 0.01`; flicker's burst and pulse would paint over the show |
| App preview | `expressionManager.isAnyTestActive()`; an open colour editor already refuses the trigger |
| Home mode with social off | `Compositor::homeSuppressesSocial()` |
| Greeting active | blocks initiating any kind; joining and accepting stay open, since layers compose |

Crowd-dim multiplies the whole frame, so Moments dim with the lamp.

## State and footprint

`MomentDirector` is one global; all its state is members, fixed size: two
kind states, the liked fingerprint, one active Moment (with an 8-entry visited
list), one outbound frame, and an 8-entry dedup ring of `{origin, seed, kind}`.
Heap is the transients only: a flicker burst (about 8 B a cell plus the
Expression) for 700 ms, a pulse for about 6 s, one at a time. A
Fireflies churns about 120 same-size alloc/free pairs over 2.6 min, under 1 Hz,
off the hot path. Building an invocation allocates its type string and colour
vector, at no more than a few per second. The roster's BLE stamp adds 4 bytes
a peer (`RosterEntry` 112 B, `NearbyCopy` 64 B).

## Debug (LAMP_DEBUG builds)

- `{"a":"moment","k":"fireflies|whisper","seed":N}` starts a kind
  now, past eligibility, conditions and the cooldown, never past a suppressor
  or a running Moment. `seed` 0 or absent rolls one.
- `{"a":"moment_state"}` (debug builds only, like every test action) prints each kind's growth, wait, cooldown and backoff,
  the liked count and stillness, and any running Moment.
- `inject_nearby` takes `"mac"` per peer, so its disposition is stored under
  that MAC; an injected peer reads as seen (and BLE-sighted) at injection and
  goes stale after 15 s, so re-send it to keep Whisper candidates fresh.
- `[moment]` lines carry kind, seed, role, hop and the reason for any skip;
  dev builds also log each Fireflies beat as `[moment] ff k=<beat> d=<ms>`.

## Bench

- **Conditions** on one lamp: `inject_nearby` with 3 or more liked MACs and
  RSSI, then wait 10 min, or force with a fixed seed.
- **Fireflies sync**: 3 or more real lamps with mutual Fond, forced with a
  fixed seed; the `ff` lines' `d` must match the native vectors for those MACs.
- **Whisper chain**: 4 or more real lamps on a Fond chain; force at one end,
  read hop order and timing across the tapped logs.
- **Load**: the wisp's crowd sim with events while Fireflies runs; watch
  `[heap]` `largest`, `txrefused`, `[expr] transient drop`, and that each
  burst logs `transient reaped type=flicker (complete)`, never `expired`.
- **The crowd sim cannot satisfy conditions.** Its fake HELLOs are sent by the
  wisp, so they fail `isDirectHello` and are never near, and they carry no BLE
  RSSI. The sim gives mesh and roster load only; near-based conditions come
  from `inject_nearby`. A Whisper hop to an injected fake MAC dead-ends, so
  Whisper benches use real lamps.
- **Older lamp** in range on pre-Moments firmware: no render, no forward, no crash.

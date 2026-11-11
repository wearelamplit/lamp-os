# Social system overview

How a lamp behaves around other lamps — greeting them, dimming in a crowd, and
the rare Moments a room of friendly lamps shares.
This is the map; the per-subsystem detail lives in the linked docs. **The
code wins ties**; update these docs when it doesn't.

## Two inputs drive everything

Every social behavior is a function of the same two values:

- **SocialMode** — the lamp's own personality, `Introvert` / `Ambivert` /
  `Extrovert`. One per lamp, persisted in NVS. Ambivert is the default /
  neutral baseline.
- **Disposition** — how *this* lamp feels about *that* peer, `1=Salty`,
  `2=Wary`, `3=Neutral`, `4=Fond`, `5=Smitten`. Per peer, per lamp,
  asymmetric (A can be Smitten with B while B is Wary with A), persisted
  in NVS keyed by peer `lampId` (mesh mac). Unknown peers default to
  Neutral.

A peer's BLE presence (and RSSI) comes from the unified `LampRoster`
store. Disposition lookups come from `Config`. Both feed every
subsystem below.

## Stored social rows

`DispositionStore` (`config/disposition_store.hpp`) owns every per-peer row
and is the only thing that changes one. A row is the disposition plus what
the fondness engine learns: a score (127 = one disposition step), the count
of meetings, the on-hours since the last counted meeting and the on-minutes
since the peer was last seen. The lamp has no wall clock, so every age counts
on-time only. Everything reads a row through `DispositionStore::peer`
(`Config::getDisposition` is its disposition); an unknown peer reads as
Neutral with nothing learned.

- **Capacity.** 100 rows, reserved once at boot. At capacity the weakest
  relationship goes: strength is `|disp - 3| x 128 + |score| + min(meets, 16)
  x 8`, ties to the one unseen longest. A user edit
  always gets a row. A newcomer from the engine gets one only when stronger
  than the weakest row, or as strong while that row has been unseen an hour,
  so a crowd cannot churn out regulars or non-neutral peers.
- **User edits.** A changed disposition (app slider, serial) sets it and
  zeroes the score; an equal one leaves the row alone. The app's JSON write
  merges: rows it does not list stay. The JSON the app reads is unchanged.
- **Engine writes.** `nudge`, `meet`, `markSeen`, `decayTowardZero` and
  `tickHour` do nothing while the master switch (`engineEnabled`, persisted
  in the blob's flags byte, on by default) is off. Home mode is the caller's
  gate. Familiarity alone moves a peer to Fond and no further.
- **Flushes.** A user edit flushes 5 s after the slider goes idle (and on BLE
  disconnect); an engine change at most once per 10 minutes and an age-only
  change once per 60, both counted from the last flush, and any flush writes
  everything, so while the engine runs a power cut loses at most an hour of
  ages. With the engine off nothing marks ages for saving, so the stored ages
  move only when some other flush writes them.
- **Boot.** Neutral rows unseen for 14 on-days (20160 on-minutes) are dropped
  at load, and the boot writes nothing.
- **Formats.** The `disp` blob is format 2, `[2][flags]` then
  `[mac:6][disp][score][meets][meetAgeH][unseenMin:2 LE]` per peer. Format 1
  (`[1]` then `[mac:6][disp]`, up to 1.3.3) loads with score 0, meets 0,
  unseen 0 and a next meeting that counts, and becomes format 2 on the first
  flush; a legacy `dispositions` JSON key is moved at load and erased only
  once the blob reads back as written. NVS figures are in
  [`embedded-heap.md`](embedded-heap.md), downgrade loss under its
  Downgrades.

## Three subsystems

| Subsystem | What it does | Lives in | Detail doc |
|---|---|---|---|
| **Greetings** | A one-shot waveform on the shade when the lamp meets a peer — fade to their color, hold, fade back. Shape varies by SocialMode × Disposition. | `behaviors/social.cpp` (renderer), `core/personality_engine.cpp` (timing source) | [`personality-greetings.md`](personality-greetings.md) |
| **Crowd-dim** | Continuous brightness damping as the room fills with peers, weighted by disposition and floored per mode. Introverts hide harder; Extroverts never dim. | `core/personality_engine.cpp` | [`personality-signals.md`](personality-signals.md) |
| **Moments** | Rare crowd-level expressions: Fireflies (liked lamps sparkle in sync), Whisper (a glow handed lamp to lamp along friendly peers). Always on, paced to grow rarer with each one. | `core/moments.cpp`, `core/moment_math.cpp` | [`moments.md`](moments.md) |

### Greetings

When a peer is heard close by (short-range BLE) and isn't on cooldown,
`SocialBehavior` plays a greeting. `PersonalityEngine::greetingFor(lampId)`
maps `(SocialMode × Disposition)` to a `GreetingTuning` (ease-in / hold /
fade-out frames + dim parameters); the behavior renders it on the shade.
Warm relationships greet longer and pop in faster, and the warmest ones
*breathe* — one or more slow pulses during the hold (Fond/Smitten);
neutral greetings are a plain in/hold/out; cold ones snub (fade to
dark-in-their-color and back). Cooldowns + introvert fatigue throttle how
often greetings fire. Full waveform + cadence contract:
[`personality-greetings.md`](personality-greetings.md).

The **snafu** variant replaces `SocialBehavior` with its own greeting
stack, `snafu::Greeting` plus a `DotsBehavior` borrow/melt afterglow, in
`lamps/snafu/greeting.{hpp,cpp}` and `lamps/snafu/dots_behavior.{hpp,cpp}`;
the description above doesn't apply to snafu.

### Crowd-dim

`PersonalityEngine` samples BLE-reachable peers at 1 Hz, weights each by
disposition (mode-dependent — Ambivert ignores Fond/Smitten pressure,
Introvert weights almost everyone), medians over a short window + EMA-
smooths, then maps the weighted count through a log curve to a brightness
multiplier floored per mode (Introvert dims further than Ambivert;
Extrovert disabled = 1.0; floors in `personality_engine.cpp`). A small
deadband stops jitter from committing. The result is applied to the lamp's
baseline brightness — a
busy room makes an introverted lamp recede. The read-only accessors a
custom lamp can react to (`crowdDimFactor()`, `smoothedCrowdWeight()`,
`crowdComposition()`) are in [`personality-signals.md`](personality-signals.md).

### Moments

`MomentDirector` reads the same near view crowd-dim samples
(`PersonalityEngine::nearbyPeers()`) and the same dispositions, and once in a
long while starts a Moment: it renders nothing itself, firing existing
expressions as transients (a Twinkle `flicker` burst, a `pulse`), and rides
`MSG_EVENT` for the mesh hand-off. Each kind has its own conditions, a wait
that grows with every participation and scales with SocialMode, and a crowd
cooldown so one room starts one Moment. Salty and Wary lamps sit out. Full
contract: [`moments.md`](moments.md).

## How they interact

- **Shared inputs, independent outputs.** Both read the same
  presence + disposition state, but render on different paths — greetings
  on the lamp's own surfaces via the behavior stack, crowd-dim via the
  brightness pipeline. They can both be active at once (e.g. dimmed for the
  crowd while mid-greeting on a new arrival).
- **Moments layer over both.** A Moment draws as transients over the
  greeting and the scene, dims with crowd-dim, never starts while a greeting
  plays, and stands down for OTA, a wisp hold, an app preview, and home mode
  with social off.
- **Extrovert disengages crowd-dim.** In Extrovert mode `crowdDimFactor()`
  is hard 1.0 — an extrovert lamp greets eagerly (short cooldowns) and
  never recedes.
- **Home mode can switch social off.** While home mode is active with social
  off (the default), greetings don't play, the lamp ignores every expression
  another lamp pushes onto it (cascades and mirrors) and reads the room as
  empty; peer effects already running fade out, crowd-dim eases back to full
  brightness over a second, and leaving home greets nobody who was near all
  along. The app's Home Mode preview blocks the same peer paths but consumes
  no arrival and leaves peer effects already running to finish. With social
  left on at home, peer effects still play, but the lamp copies none of them.
  Detail in [`lamp-framework.md`](lamp-framework.md#home-mode).
- **Yawns spread.** A tired introvert yawns now and then, and a lamp that
  sees a peer yawn may yawn back, likelier the fonder it is of that peer and
  less likely in a crowd. Both run only while the social engine is on and
  home mode is off. Detail in
  [`expressions.md`](expressions.md#yawn-contagion).
- **OTA outranks personality.** Offering an update is not a social
  behaviour. The offer scan (`tickOtaOffers`, `components/firmware/ota_offer.cpp`)
  runs in the framework loop on every variant, so a lamp distributes and
  receives OTA whatever its social mode, mid-greeting, in home mode with
  social off, and on variants built without `SocialBehavior` (snafu,
  lioness). The greeting's appearance is personality's department, not
  whether bytes move.

## Code map

- `core/personality_engine.{hpp,cpp}` — the engine: greeting tuning lookup,
  crowd-dim sampler/smoother. Global singleton `personalityEngine`, wired +
  ticked by the framework.
- `behaviors/social.{hpp,cpp}` — the greeting renderer + discovery /
  cooldown / fatigue gating.
- `core/moments.{hpp,cpp}`, `core/moment_math.{hpp,cpp}` — the Moment
  director and its pure arithmetic; `core/lamp_moments.cpp` binds it to the
  lamp.
- `config/` — SocialMode + per-peer dispositions storage (NVS) and their
  BLE sync surfaces.

## Cross-references

- [`personality-greetings.md`](personality-greetings.md) — greeting
  waveforms, the profile shapes, cooldown + fatigue rules.
- [`personality-signals.md`](personality-signals.md) — the read-only
  signal API custom lamps react to, with worked examples.
- [`moments.md`](moments.md) — Moments: kinds, wire format, pacing,
  suppressors, debug hooks and bench steps.
- [`lamp-social-api.md`](lamp-social-api.md) — the author-facing API a custom
  behavior reaches these signals through: `forEachArrival` / `onArrival` /
  `greetingFor` / `dispositionOf` / `crowd` / `crowdWeight`, via its
  `BehaviorContext` rather than the globals.

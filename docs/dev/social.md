# Social system overview

How a lamp behaves around other lamps — greeting them, dimming in a crowd,
fixating on a favorite. This is the map; the per-subsystem detail lives in
the linked docs. **The code wins ties**; update these docs when it doesn't.

## Two inputs drive everything

Every social behavior is a function of the same two values:

- **SocialMode** — the lamp's own personality, `Introvert` / `Ambivert` /
  `Extrovert`. One per lamp, persisted in NVS. Ambivert is the default /
  neutral baseline.
- **Disposition** — how *this* lamp feels about *that* peer, `1=Salty`,
  `2=Wary`, `3=Neutral`, `4=Fond`, `5=Smitten`. Per peer, per lamp,
  asymmetric (A can be Smitten with B while B is Wary with A), persisted
  in NVS keyed by peer BD_ADDR. Unknown peers default to Neutral.

A peer's BLE presence (and RSSI) comes from the unified `NearbyLamps`
store. Disposition lookups come from `Config`. Both feed all three
subsystems below.

## Three subsystems

| Subsystem | What it does | Lives in | Detail doc |
|---|---|---|---|
| **Greetings** | A one-shot waveform on the shade when the lamp meets a peer — fade to their color, hold, fade back. Shape varies by SocialMode × Disposition. | `behaviors/social.cpp` (renderer), `core/personality_engine.cpp` (timing source) | [`personality-greetings.md`](personality-greetings.md) |
| **Crowd-dim** | Continuous brightness damping as the room fills with peers, weighted by disposition and floored per mode. Introverts hide harder; Extroverts never dim. | `core/personality_engine.cpp` | [`personality-signals.md`](personality-signals.md) |
| **Closest-Smitten pulse** | A recurring affection pulse on the lamp's single closest peer, but only while that peer is a Smitten favorite. | `core/personality_engine.cpp` | (below) |

### Greetings

When a peer is heard close by (short-range BLE) and isn't on cooldown,
`SocialBehavior` plays a greeting. `PersonalityEngine::greetingFor(bdAddr)`
maps `(SocialMode × Disposition)` to a `GreetingTuning` (ease-in / hold /
fade-out frames + dim parameters); the behavior renders it on the shade.
Warm relationships greet longer and pop in faster, and the warmest ones
*breathe* — one or more slow pulses during the hold (Fond/Smitten);
neutral greetings are a plain in/hold/out; cold ones snub (fade to
dark-in-their-color and back). Cooldowns + introvert fatigue throttle how
often greetings fire. Full waveform + cadence contract:
[`personality-greetings.md`](personality-greetings.md).

### Crowd-dim

`PersonalityEngine` samples BLE-reachable peers at 1 Hz, weights each by
disposition (mode-dependent — Ambivert ignores Fond/Smitten pressure,
Introvert weights almost everyone), medians over a short window + EMA-
smooths, then maps the weighted count through a log curve to a brightness
multiplier floored per mode (`kIntrovertFloor = 0.5`, `kAmbivertFloor =
0.7`, Extrovert disabled = 1.0). A 2/100 deadband stops jitter from
committing. The result is applied to the lamp's baseline brightness — a
busy room makes an introverted lamp recede. The read-only accessors a
custom lamp can react to (`crowdDimFactor()`, `smoothedCrowdWeight()`,
`crowdComposition()`) are in [`personality-signals.md`](personality-signals.md).

### Closest-Smitten pulse

Separate from greetings: `PersonalityEngine` watches the single closest
BLE peer (highest RSSI). If that peer is a **Smitten** favorite
(disposition 5), the lamp fires a short pulse expression in the peer's
color — once when they *become* the closest (guarded by a
`kRssiHysteresisDb = 3` dB margin so two near-equal peers don't strobe as
they flap), then every `kClosestPulsePeriodMs = 45 s` while they stay
closest. It's fired through `ExpressionManager` (not `SocialBehavior`), so
it composites independently of any greeting in flight. Losing the peer, or
the peer dropping below Smitten, resets the cadence clock so a later
re-fixation starts clean.

## How they interact

- **Shared inputs, independent outputs.** All three read the same
  presence + disposition state, but render on different paths — greetings
  and crowd-dim on the lamp's own surfaces via the behavior stack /
  brightness pipeline, the Smitten pulse via the expression system. They
  can all be active at once (e.g. dimmed for the crowd, mid-greeting on a
  new arrival, pulsing for a favorite in the corner).
- **Extrovert disengages crowd-dim.** In Extrovert mode `crowdDimFactor()`
  is hard 1.0 — an extrovert lamp greets eagerly (short cooldowns) and
  never recedes.
- **OTA outranks personality.** Firmware propagation is gated separately
  from the visual greeting; a lamp distributes/receives OTA regardless of
  social mode. The greeting's appearance is personality's department, not
  whether bytes move. (See the OTA notes in the firmware components.)

## Code map

- `core/personality_engine.{hpp,cpp}` — the engine: greeting tuning lookup,
  crowd-dim sampler/smoother, closest-Smitten cycle. Global singleton
  `personalityEngine`, wired + ticked by the framework.
- `behaviors/social.{hpp,cpp}` — the greeting renderer + discovery /
  cooldown / fatigue gating.
- `config/` — SocialMode + per-peer dispositions storage (NVS) and their
  BLE sync surfaces.

## Cross-references

- [`personality-greetings.md`](personality-greetings.md) — greeting
  waveforms, the profile table, cooldown + fatigue rules.
- [`personality-signals.md`](personality-signals.md) — the read-only
  signal API custom lamps react to, with worked examples.
- [`lamp-framework.md`](lamp-framework.md) — the runtime the social
  subsystems plug into.

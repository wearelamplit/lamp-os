# Personality greetings

Source of truth for the greeting waveform contract: what shape a lamp draws
on its shade when it meets another lamp, and how that shape varies by the
lamp's own social mode and its per-peer disposition.

The code wins ties; update this doc when it doesn't.

Implementation lives in:

- `software/lamp-os/src/core/personality_engine.{hpp,cpp}`, profile
  constants, `(SocialMode × Disposition) → Profile` mapping,
  `GreetingTuning` struct.
- `software/lamp-os/src/behaviors/social.{hpp,cpp}`, waveform renderer,
  cooldown gating, per-peer regreet tracking, fatigue.

## Concepts

**SocialMode** (per lamp, persisted in NVS), `Introvert` / `Ambivert` /
`Extrovert`. The lamp's own personality.

**Disposition** (per peer, per lamp, asymmetric, persisted in NVS keyed
by peer `lampId` / mesh mac), `1=Salty`, `2=Wary`, `3=Neutral`, `4=Fond`,
`5=Smitten`. How this lamp feels about that peer.

**Greeting profile**, a named waveform shape with five parameters:
total duration, ease-in duration, hold duration, fade-out duration, and
optional pulse-back (depth + count) during the hold phase.

**Pulse-back**, during the hold phase, the peer color breathes toward a
darker version of itself and back. Every warm profile pulses for the
*entire* hold (`pulseBackCount = kPulseCountContinuous`); disposition is
carried by the pulse depth (`pulseBackStrength`) and the per-profile
breath rate (`breathCycleFrames`, warmer = faster). `pulseBackCount = N`
would run exactly N cycles, but no shipped warm profile uses a finite
count.

**Snub**, the negative side of the disposition spectrum, on a short
profile. Unlike a warm greeting (which reaches full peer color and
optionally pulses during the hold), a snub *folds the dim into the
fades*: the ease-in fades shade → peer color while simultaneously
dropping brightness to the snub depth, the hold sits dark-in-their-color,
and the ease-out reverses (rising from dark back through their color to
the shade). Depth is set by `pulseBackStrength`, 255 = full darken to
black. A snub reaches a real cold floor: a full snub retains roughly a
quarter of the peer color, a partial snub a bit more (dims in
`personality_engine.cpp`). `pulseBackCount` is unused on snubs — the dim
is one continuous ramp, not a count of breaths.

The waveform renders on the **shade** surface. The base keeps drawing
whatever expression is configured.

## Profiles

The eight named profiles (the `kProfile*` constants in
`personality_engine.cpp`) are all variations on one shape: ease-in →
hold → fade-out, with an optional warm pulse-back during the hold. The
exact frame counts, dim strengths, and breath cadences are tuning
constants — read them from the code; what follows is the shape and how it
moves across the disposition axis.

`kProfileStandard` (Ambivert greeting a Neutral peer) is the anchor: a
brief ease-in, a long steady hold in the peer's color, a brief fade-out.
A real greeting runs on the order of twenty-odd seconds; the snubs are
much shorter. Every warm/cold variant is a subtle offset off the anchor,
and the personality tell is the asymmetry, not the length.

**Ease-in inverts with warmth.** The coldest profile eases in slowest
(hesitant recognition); the warmest snaps in fastest (eager lock-on).

**Fade-out grows with warmth.** The warm side lingers on the way out; the
cold side lets go quickly. So a warm greeting swells in and reluctantly
leaves; a cold one eases in and is gone quickly.

**Pulse depth deepens with warmth on the affectionate side.** Every warm
profile (Fond/Smitten) breathes a gentle glow — not a flash — for the
whole hold; the warmer the disposition, the deeper the dip and the faster
the cycle. Smitten Extrovert (Effusive) is the most visible greeting;
Smitten Introvert (Warm) is the most subtle.

## Per-mode greeting tables

The `(SocialMode × Disposition) → Profile` mapping is the contract (it's
the matrix `personality_engine.cpp` builds and `greetingFor()` exposes);
the timing behind each profile is in **Profiles** above.

### Introvert mode

Longest cooldown between greetings, a per-peer regreet window, and
**fatigue** — a burst of greetings in a short span triggers a rest window
during which it won't greet. Cadence constants in `social.cpp`.

| Disposition | Profile | Effect |
|---|---|---|
| Salty (1) | Snub | Snub in their color |
| Wary (2) | PartialSnub | Partial snub in their color |
| Neutral (3) | Minimal | Hold in their color |
| Fond (4) | Gentle | Hold in their color |
| Smitten (5) | Warm | Slow pulse in their color |

### Ambivert mode

Shorter cooldown than Introvert plus a per-peer regreet window. No
fatigue.

| Disposition | Profile | Effect |
|---|---|---|
| Salty (1) | Snub | Snub in their color |
| Wary (2) | PartialSnub | Partial snub in their color |
| Neutral (3) | Standard | Hold in their color |
| Fond (4) | Warm | Slow pulse in their color |
| Smitten (5) | Enthused | Deeper, faster pulse in their color |

### Extrovert mode

Shortest cooldown; no per-peer regreet window (eagerly re-greets
returning peers each cooldown cycle). No fatigue.

| Disposition | Profile | Effect |
|---|---|---|
| Salty (1) | Snub | Snub in their color |
| Wary (2) | PartialSnub | Partial snub in their color |
| Neutral (3) | Standard | Hold in their color |
| Fond (4) | Enthused | Deeper, faster pulse in their color |
| Smitten (5) | Effusive | Eager continuous pulsing in their color |

## Cooldown comparison

Personality drives both the base cadence and whether sustained activity
can drain the lamp.

| Aspect | Introvert | Ambivert | Extrovert |
|---|---|---|---|
| Base cooldown | longest | shorter | shortest |
| Per-peer regreet window | yes | yes | none |
| Fatigue (burst → rest) | yes | none | none |

Introvert is the personality that gets drained; Ambivert is balanced;
Extrovert is eager but bounded. Snub and PartialSnub greetings are
gated by the same cooldown as warm greetings, no bypass paths.

## Effect glossary

- **Snub**, ease from the shade toward the peer color while dimming to
  the snub depth (retains roughly a quarter of the peer color), hold at
  that cold floor, then reverse (rise back through the peer color to the
  shade). The observer sees the peer's hue recede to a dark floor and
  return, identifying who the snub is aimed at.
- **Partial snub**, same shape as Snub but the dim bottoms out higher.
  Reads as a hesitant pullback, gentler still than a Snub.
- **Hold**, ease into peer color, hold steady at peer color, fade
  out. No pulse. The duration distinguishes a quick Neutral
  acknowledgement from a lingering Fond settle.
- **Warm pulse**, a continuous slow breath dipping toward a darkened peer
  color and back, filling the whole hold. Depth + cycle speed carry the
  disposition: the warmer the profile, the deeper the dip and the faster
  the breath. Every affectionate profile (Fond/Smitten) pulses; the eager
  end (Smitten Extrovert, Effusive) reads as "fully lit up in affection."

## Waveform mechanics

Each greeting plays as a three-phase animation on the shade:

1. **Ease-in**, interpolate from whatever the shade was drawing →
   `foundLampColor` (the peer's base color). On a **snub**, the dim
   ramps 0 → `pulseBackStrength` across this phase too, so the ease-in
   lands dark-in-their-color.
2. **Hold**, stay at `foundLampColor`. On a **snub**, hold at
   `darken(foundLampColor, pulseBackStrength)` (the snub floor) instead —
   the pointed dark pause. Otherwise, if pulse-back is active, breathe
   slow pulse cycles for the entire hold (warm profiles are all
   `kPulseCountContinuous`).
3. **Fade-out**, interpolate `foundLampColor` → whatever the shade
   would now be drawing. On a **snub**, the dim ramps back
   `pulseBackStrength` → 0 over this phase, mirroring the ease-in.

`darken(c, s)` scales each channel by `(255 - s)/255` — `darken(c, 255)`
is black, `darken(c, 0)` is unchanged — preserving hue while dropping
brightness. Warm-side **pulse** cycles are half-down then half-up around
`darken(foundLampColor, pulseBackStrength)` at the profile's own
`breathCycleFrames` cadence (warmer breathes faster), a slow gentle
breath, not a flash. **Snubs** don't pulse — they use `darken`
once as a brightness envelope folded into the ease-in/hold/fade-out.

## What this doc doesn't cover

For how greetings fit alongside the other social subsystems, see the
overview in [`social.md`](social.md). This doc is greetings only:

- The crowd-aware brightness damping in `PersonalityEngine` (Introvert
  and Ambivert; Extrovert is unaffected), separate subsystem, weights
  peers by disposition.
- The dispositions storage + sync surfaces (NVS, `CHAR_SOCIAL_DISPOSITIONS`
  BLE characteristic), see `personality_engine.hpp` and
  `config.{hpp,cpp}`.

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
by peer BD_ADDR), `1=Salty`, `2=Wary`, `3=Neutral`, `4=Fond`,
`5=Smitten`. How this lamp feels about that peer.

**Greeting profile**, a named waveform shape with five parameters:
total duration, ease-in duration, hold duration, fade-out duration, and
optional pulse-back (depth + count) during the hold phase.

**Pulse-back**, during the hold phase, the peer color can dim toward a
darker version of itself in one or more cycles, then snap back. The
remainder of the hold is steady at the peer color. Pulse rate is fixed
at ~750 ms per breath (`kSlowPulseCycleFrames = 45` at 60 fps); explicit
counts (`pulseBackCount = 1, 2, …`) run exactly N cycles, and
`pulseBackCount = kPulseCountContinuous` fills the entire hold with
back-to-back cycles.

**Snub**, the negative side of the disposition spectrum, on a short
profile. Unlike a warm greeting (which reaches full peer color and
optionally pulses during the hold), a snub *folds the dim into the
fades*: the ease-in fades shade → peer color while simultaneously
dropping brightness to the snub depth, the hold sits dark-in-their-color,
and the ease-out reverses (rising from dark back through their color to
the shade). Depth is set by `pulseBackStrength`, 255 = full darken to
black (Snub), 128 = partial darken to ~50% retention (PartialSnub).
`pulseBackCount` is unused on snubs — the dim is one continuous ramp, not
a count of breaths.

The waveform renders on the **shade** surface. The base keeps drawing
whatever expression is configured.

## Profile constants

Frame counts at ~60 fps. Strength is the `darken()` input (0 = no dim,
255 = black). Count is the number of pulse cycles, or
`kPulseCountContinuous = 0xFF`.

| Profile | Total | Ease-in | Hold | Fade-out | Strength | Count |
|---|---|---|---|---|---|---|
| `kProfileMinimal` | 17.0s | 2.5s | 13.0s | 1.5s | 0 | 0 |
| `kProfileGentle` | 18.5s | 2.0s | 14.0s | 2.5s | 0 | 0 |
| `kProfileStandard` | 20.0s | 2.0s | 16.0s | 2.0s | 0 | 0 |
| `kProfileWarm` | 21.5s | 1.5s | 17.0s | 3.0s | 100 (~60% retention) | 1 |
| `kProfileEnthused` | 22.5s | 1.0s | 18.0s | 3.5s | 128 (~50% retention) | 2 |
| `kProfileEffusive` | 23.75s | 0.75s | 19.0s | 4.0s | 153 (~40% retention) | continuous |
| `kProfileSnub` | 4.5s | 1.0s | 2.5s | 1.0s | 255 (to black) | 1 |
| `kProfilePartialSnub` | 6.0s | 1.0s | 3.5s | 1.5s | 128 (~50% retention) | 1 |
| `kProfileSnubQuick` | 5.5s | 1.0s | 3.0s | 1.5s | 255 (to black) | 1 |
| `kProfilePartialSnubQuick` | 6.5s | 1.0s | 4.0s | 1.5s | 128 (~50% retention) | 1 |

`kProfileStandard` (Ambivert greeting a Neutral peer) is the anchor —
2s in / 16s hold / 2s out. Every other profile is a subtle variation off
it; a real greeting stays in the ~17-24s band, and the personality tell
is the asymmetry, not the length.

**Ease-in inverts with warmth.** Slow ease-in (Minimal, 2.5s) reads as
hesitant recognition; fast ease-in (Effusive, 0.75s) reads as eager
lock-on. Fade-out grows with warmth, extended lingering on the warm
side (Effusive, 4s), quick let-go on the cold side (Minimal, 1.5s). So a
warm greeting pops in and reluctantly leaves; a cold one eases in and is
gone quickly.

**Pulse depth deepens with warmth on the affectionate side.** Warm dips
gently to 60%, Enthused dips more visibly to 50%, Effusive dips
dramatically to 40%. Smitten Extrovert (Effusive) is the most visible
greeting; Smitten Introvert (Warm) is the most subtle.

## Per-mode greeting tables

### Introvert mode

**Cooldown**: 60 s base between greetings + per-peer 10-min regreet
window + **fatigue**: 3 greetings within 5 min triggers a 5-min rest
window (no greetings during rest).

| Disposition | Profile | Total | Ease-in | Hold | Fade-out | Slow Pulse | Effect |
|---|---|---|---|---|---|---|---|
| Salty (1) | Snub | 4.5s | 1.0s | 2.5s | 1.0s | — (dim → 0%) | Snub in their color |
| Wary (2) | PartialSnub | 6.0s | 1.0s | 3.5s | 1.5s | — (dim → 50%) | Partial snub in their color |
| Neutral (3) | Minimal | 17.0s | 2.5s | 13.0s | 1.5s | | Hold in their color |
| Fond (4) | Gentle | 18.5s | 2.0s | 14.0s | 2.5s | | Hold in their color |
| Smitten (5) | Warm | 21.5s | 1.5s | 17.0s | 3.0s | 1× → 60% | One subtle pulse in their color |

### Ambivert mode

**Cooldown**: 30 s base between greetings + per-peer 5-min regreet
window. No fatigue.

| Disposition | Profile | Total | Ease-in | Hold | Fade-out | Slow Pulse | Effect |
|---|---|---|---|---|---|---|---|
| Salty (1) | Snub | 4.5s | 1.0s | 2.5s | 1.0s | — (dim → 0%) | Snub in their color |
| Wary (2) | PartialSnub | 6.0s | 1.0s | 3.5s | 1.5s | — (dim → 50%) | Partial snub in their color |
| Neutral (3) | Standard | 20.0s | 2.0s | 16.0s | 2.0s | | Hold in their color |
| Fond (4) | Warm | 21.5s | 1.5s | 17.0s | 3.0s | 1× → 60% | One subtle pulse in their color |
| Smitten (5) | Enthused | 22.5s | 1.0s | 18.0s | 3.5s | 2× → 50% | Two pulses in their color |

### Extrovert mode

**Cooldown**: 26 s base between greetings; no per-peer regreet window
(eagerly re-greets returning peers each cooldown cycle). No fatigue.

| Disposition | Profile | Total | Ease-in | Hold | Fade-out | Slow Pulse | Effect |
|---|---|---|---|---|---|---|---|
| Salty (1) | Snub-Quick | 5.5s | 1.0s | 3.0s | 1.5s | — (dim → 0%) | Snub in their color |
| Wary (2) | PartialSnub-Quick | 6.5s | 1.0s | 4.0s | 1.5s | — (dim → 50%) | Partial snub in their color |
| Neutral (3) | Standard | 20.0s | 2.0s | 16.0s | 2.0s | | Hold in their color |
| Fond (4) | Enthused | 22.5s | 1.0s | 18.0s | 3.5s | 2× → 50% | Two pulses in their color |
| Smitten (5) | Effusive | 23.75s | 0.75s | 19.0s | 4.0s | continuous → 40% | Continuous pulsing in their color |

## Cooldown comparison

Personality drives both the base cadence and whether sustained activity
can drain the lamp.

| Aspect | Introvert | Ambivert | Extrovert |
|---|---|---|---|
| Base cooldown | 60 s | 30 s | 26 s |
| Per-peer regreet window | 10 min | 5 min | none |
| Fatigue | 3 greets / 5 min → 5 min rest | none | none |

Introvert is the personality that gets drained; Ambivert is balanced;
Extrovert is eager but bounded. Snub and PartialSnub greetings are
gated by the same cooldown as warm greetings, no bypass paths.

## Effect glossary

- **Snub**, ease from the shade toward the peer color while dimming all
  the way to black, hold dark, then reverse (rise from black back
  through the peer color to the shade). The observer sees the peer's hue
  swallowed into a blackout and spat back out, identifying who the snub
  is aimed at.
- **Partial snub**, same shape as Snub but the dim bottoms out at ~50%
  of the peer color instead of full black. Reads as a hesitant pullback
  rather than a hard blackout.
- **Hold**, ease into peer color, hold steady at peer color, fade
  out. No pulse. The duration distinguishes a quick Neutral
  acknowledgement from a lingering Fond settle.
- **Subtle pulse**, one slow breath dipping to ~60% retention then
  returning to peer color. Used by Smitten Introvert and Fond Ambivert.
- **Pulses (count > 1)**, two or more back-to-back breaths at the
  slow cycle rate. Dip depth deepens with warmth (50% for Enthused,
  40% for Effusive).
- **Continuous pulsing**, back-to-back breaths filling the entire
  hold phase. Reserved for Smitten Extrovert; reads as "fully lit up
  in affection."

## Waveform mechanics

Each greeting plays as a three-phase animation on the shade:

1. **Ease-in**, interpolate from whatever the shade was drawing →
   `foundLampColor` (the peer's base color). On a **snub**, the dim
   ramps 0 → `pulseBackStrength` across this phase too, so the ease-in
   lands dark-in-their-color.
2. **Hold**, stay at `foundLampColor`. On a **snub**, hold at
   `darken(foundLampColor, pulseBackStrength)` (black / ~50%) instead —
   the pointed dark pause. Otherwise, if pulse-back is active, run the
   specified number of slow pulse cycles back-to-back at the start of
   the hold; any remaining hold time stays steady.
3. **Fade-out**, interpolate `foundLampColor` → whatever the shade
   would now be drawing. On a **snub**, the dim ramps back
   `pulseBackStrength` → 0 over this phase, mirroring the ease-in.

`darken(c, s)` scales each channel by `(255 - s)/255` — `darken(c, 255)`
is black, `darken(c, 0)` is unchanged — preserving hue while dropping
brightness. Warm-side **pulse** cycles are half-down then half-up around
`darken(foundLampColor, pulseBackStrength)` at a fixed ~750 ms cadence
(`kSlowPulseCycleFrames = 45`), a slow breath, clearly visible, not
flicker-fast. **Snubs** don't pulse — they use `darken` once as a
brightness envelope folded into the ease-in/hold/fade-out.

## What this doc doesn't cover

For how greetings fit alongside the other social subsystems, see the
overview in [`social.md`](social.md). This doc is greetings only:

- The crowd-aware brightness damping in `PersonalityEngine` (Introvert
  and Ambivert; Extrovert is unaffected), separate subsystem, weights
  peers by disposition.
- The 45-second closest-Smitten recurring pulse, separate from the
  greeting waveform, fired through `ExpressionManager` not `SocialBehavior`.
- The dispositions storage + sync surfaces (NVS, `CHAR_SOCIAL_DISPOSITIONS`
  BLE characteristic), see `personality_engine.hpp` and
  `config.{hpp,cpp}`.

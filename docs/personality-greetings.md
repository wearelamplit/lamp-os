# Personality greetings

Source of truth for the greeting waveform contract: what shape a lamp draws
on its shade when it meets another lamp, and how that shape varies by the
lamp's own social mode and its per-peer disposition.

The code wins ties; update this doc when it doesn't.

Implementation lives in:

- `software/lamp-os/src/core/personality_engine.{hpp,cpp}` — profile
  constants, `(SocialMode × Disposition) → Profile` mapping,
  `GreetingTuning` struct.
- `software/lamp-os/src/behaviors/social.{hpp,cpp}` — waveform renderer,
  cooldown gating, per-peer regreet tracking, fatigue.

## Concepts

**SocialMode** (per lamp, persisted in NVS) — `Introvert` / `Ambivert` /
`Extrovert`. The lamp's own personality.

**Disposition** (per peer, per lamp, asymmetric, persisted in NVS keyed
by peer BD_ADDR) — `1=Salty`, `2=Wary`, `3=Neutral`, `4=Fond`,
`5=Smitten`. How this lamp feels about that peer.

**Greeting profile** — a named waveform shape with five parameters:
total duration, ease-in duration, hold duration, fade-out duration, and
optional pulse-back (depth + count) during the hold phase.

**Pulse-back** — during the hold phase, the peer color can dim toward a
darker version of itself in one or more cycles, then snap back. The
remainder of the hold is steady at the peer color. Pulse rate is fixed
at ~750 ms per breath (`kSlowPulseCycleFrames = 45` at 60 fps); explicit
counts (`pulseBackCount = 1, 2, …`) run exactly N cycles, and
`pulseBackCount = kPulseCountContinuous` fills the entire hold with
back-to-back cycles.

**Snub** — a high-strength pulse-back (255 = full darken to black, 128
= partial darken to ~50% retention) on a short profile. The negative
side of the disposition spectrum, structurally identical to the warm
side but with deeper dips and shorter durations.

The waveform renders on the **shade** surface. The base keeps drawing
whatever expression is configured.

## Profile constants

Frame counts at ~60 fps. Strength is the `darken()` input (0 = no dim,
255 = black). Count is the number of pulse cycles, or
`kPulseCountContinuous = 0xFF`.

| Profile | Total | Ease-in | Hold | Fade-out | Strength | Count |
|---|---|---|---|---|---|---|
| `kProfileMinimal` | 1.0s | 0.5s | 0.2s | 0.3s | 0 | 0 |
| `kProfileQuick` | 1.5s | 0.4s | 0.4s | 0.7s | 0 | 0 |
| `kProfileGentle` | 2.0s | 0.3s | 0.6s | 1.1s | 0 | 0 |
| `kProfileStandard` | 2.5s | 0.25s | 0.85s | 1.4s | 0 | 0 |
| `kProfileWarm` | 3.0s | 0.2s | 1.1s | 1.7s | 100 (~60% retention) | 1 |
| `kProfileEnthused` | 4.0s | 0.2s | 1.5s | 2.3s | 128 (~50% retention) | 2 |
| `kProfileEffusive` | 5.0s | 0.1s | 2.0s | 2.9s | 153 (~40% retention) | continuous (≈3 breaths) |
| `kProfileSnub` | 1.0s | 0.3s | 0.4s | 0.3s | 255 (to black) | 1 |
| `kProfilePartialSnub` | 1.0s | 0.3s | 0.4s | 0.3s | 128 (~50% retention) | 1 |
| `kProfileSnubQuick` | 1.5s | 0.4s | 0.6s | 0.5s | 255 (to black) | 1 |
| `kProfilePartialSnubQuick` | 1.5s | 0.4s | 0.6s | 0.5s | 128 (~50% retention) | 1 |

**Ease-in inverts with warmth.** Slow ease-in (Minimal, 500 ms) reads as
hesitant recognition; fast ease-in (Effusive, 100 ms) reads as eager
lock-on. Fade-out grows with warmth — extended lingering on the warm
side, quick let-go on the cold side.

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
| Salty (1) | Snub | 1.0s | 0.3s | 0.4s | 0.3s | 1× → 0% | Snub in their color |
| Wary (2) | PartialSnub | 1.0s | 0.3s | 0.4s | 0.3s | 1× → 50% | Partial snub in their color |
| Neutral (3) | Minimal | 1.0s | 0.5s | 0.2s | 0.3s | — | Hold in their color |
| Fond (4) | Gentle | 2.0s | 0.3s | 0.6s | 1.1s | — | Hold in their color |
| Smitten (5) | Warm | 3.0s | 0.2s | 1.1s | 1.7s | 1× → 60% | One subtle pulse in their color |

### Ambivert mode

**Cooldown**: 30 s base between greetings + per-peer 5-min regreet
window. No fatigue.

| Disposition | Profile | Total | Ease-in | Hold | Fade-out | Slow Pulse | Effect |
|---|---|---|---|---|---|---|---|
| Salty (1) | Snub | 1.0s | 0.3s | 0.4s | 0.3s | 1× → 0% | Snub in their color |
| Wary (2) | PartialSnub | 1.0s | 0.3s | 0.4s | 0.3s | 1× → 50% | Partial snub in their color |
| Neutral (3) | Standard | 2.5s | 0.25s | 0.85s | 1.4s | — | Hold in their color |
| Fond (4) | Warm | 3.0s | 0.2s | 1.1s | 1.7s | 1× → 60% | One subtle pulse in their color |
| Smitten (5) | Enthused | 4.0s | 0.2s | 1.5s | 2.3s | 2× → 50% | Two pulses in their color |

### Extrovert mode

**Cooldown**: 15 s base between greetings; no per-peer regreet window
(eagerly re-greets returning peers each cooldown cycle). No fatigue.

| Disposition | Profile | Total | Ease-in | Hold | Fade-out | Slow Pulse | Effect |
|---|---|---|---|---|---|---|---|
| Salty (1) | Snub-Quick | 1.5s | 0.4s | 0.6s | 0.5s | 1× → 0% | Snub in their color |
| Wary (2) | PartialSnub-Quick | 1.5s | 0.4s | 0.6s | 0.5s | 1× → 50% | Partial snub in their color |
| Neutral (3) | Standard | 2.5s | 0.25s | 0.85s | 1.4s | — | Hold in their color |
| Fond (4) | Enthused | 4.0s | 0.2s | 1.5s | 2.3s | 2× → 50% | Two pulses in their color |
| Smitten (5) | Effusive | 5.0s | 0.1s | 2.0s | 2.9s | continuous → 40% | Continuous pulsing in their color |

## Cooldown comparison

Personality drives both the base cadence and whether sustained activity
can drain the lamp.

| Aspect | Introvert | Ambivert | Extrovert |
|---|---|---|---|
| Base cooldown | 60 s | 30 s | 15 s |
| Per-peer regreet window | 10 min | 5 min | none |
| Fatigue | 3 greets / 5 min → 5 min rest | none | none |

Introvert is the personality that gets drained; Ambivert is balanced;
Extrovert is eager but bounded. Snub and PartialSnub greetings are
gated by the same cooldown as warm greetings — no bypass paths.

## Effect glossary

- **Snub** — ease into peer color, dim through that color all the way
  to black on one slow breath, return to peer color, fade out. The
  observer sees a brief flash of the peer's color sandwiching a
  blackout, identifying who the snub is aimed at.
- **Partial snub** — same shape as Snub but dims only to ~50% of the
  peer color. Reads as a hesitant pullback rather than a hard
  blackout.
- **Hold** — ease into peer color, hold steady at peer color, fade
  out. No pulse. The duration distinguishes a quick Neutral
  acknowledgement from a lingering Fond settle.
- **Subtle pulse** — one slow breath dipping to ~60% retention then
  returning to peer color. Used by Smitten Introvert and Fond Ambivert.
- **Pulses (count > 1)** — two or more back-to-back breaths at the
  slow cycle rate. Dip depth deepens with warmth (50% for Enthused,
  40% for Effusive).
- **Continuous pulsing** — back-to-back breaths filling the entire
  hold phase. Reserved for Smitten Extrovert; reads as "fully lit up
  in affection."

## Waveform mechanics

Each greeting plays as a four-phase animation on the shade:

1. **Ease-in** — interpolate from whatever the shade was drawing →
   `foundLampColor` (the peer's base color).
2. **Hold** — stay at `foundLampColor`. If pulse-back is active, run
   the specified number of slow pulse cycles back-to-back at the
   start of the hold; any remaining hold time stays steady.
3. **Fade-out** — interpolate `foundLampColor` → whatever the shade
   would now be drawing.
4. **OTA-hold** (optional) — if the lamp is mid-OTA-distribution to
   this same peer when the greeting fires, the animation lifetime is
   extended (~60 s ceiling) so the shade stays on the peer color
   while the OTA pulse modulates brightness on a meaningful hue.

Each pulse cycle is half-down then half-up around
`darken(foundLampColor, pulseBackStrength)`, where `darken(c, 255)` is
black and `darken(c, 0)` is unchanged. The cycle rate is fixed at
~750 ms (`kSlowPulseCycleFrames = 45`), a slow-breath cadence — clearly
visible, not flicker-fast.

## What this doc doesn't cover

- The crowd-aware brightness damping in `PersonalityEngine` (Introvert
  only) — separate subsystem, weights peers by disposition.
- The 45-second closest-Smitten recurring pulse — separate from the
  greeting waveform, fired through `ExpressionManager` not `SocialBehavior`.
- The dispositions storage + sync surfaces (NVS, `CHAR_SOCIAL_DISPOSITIONS`
  BLE characteristic) — see `personality_engine.hpp` and
  `config.{hpp,cpp}`.

# Personality signals (for custom lamp authors)

`PersonalityEngine` is the global singleton that tracks the social
environment around the lamp, who's nearby, how this lamp feels about
them, and how crowded the room is. Most of that machinery is private,
but a small read-only surface is exposed so custom lamp variants can
react to the same signals the framework's StandardLamp reacts to.

All accessors are pure reads. They don't mutate engine state and they
don't fire any effects. Safe to call from any Core 1 context, the
typical place is your variant's `loop()` or a custom behavior's
`update()`.

The engine is a global singleton declared in
`software/lamp-os/src/core/personality_engine.hpp`:

```cpp
extern PersonalityEngine personalityEngine;
```

Include the header, call the accessor, react. No init required from
the caller side, the framework already wires + ticks the engine.

## Available accessors

| Method | Returns | Represents | Use when |
|---|---|---|---|
| `crowdDimFactor()` | `float [floor, 1.0]` | Ready-to-use brightness multiplier, log-curved + per-mode floored | You want to dim your lamp the same way personality does |
| `smoothedCrowdWeight()` | `float [0, ∞)` | Raw weighted W, the signal that feeds `crowdDimFactor()` before curve/floor | You want to apply your own curve, floor, or target axis |
| `crowdComposition()` | `CrowdComposition` (5 `uint8_t` counts) | Currently-visible peer counts by disposition | You want to react to specific disposition patterns |
| `greetingFor(lampId)` | `GreetingTuning` struct | The greeting waveform the engine would play for that peer | You're driving your own greeting renderer on a non-Standard lamp |

`CrowdComposition`:

```cpp
struct CrowdComposition {
  uint8_t salty   = 0;  // disposition == 1
  uint8_t wary    = 0;  // disposition == 2
  uint8_t neutral = 0;  // disposition == 3 (default for unknown peers)
  uint8_t fond    = 0;  // disposition == 4
  uint8_t smitten = 0;  // disposition == 5
};
```

`GreetingTuning` is documented in `personality_engine.hpp` and the
shape it produces is documented in `docs/dev/personality-greetings.md`.

## Worked examples

### 1. Shy lamp: steeper crowd curve

Default `crowdDimFactor()` floors at 0.5 (Introvert) or 0.7 (Ambivert)
and rolls off as `1 - 0.5 * log10(1+W) / log10(11)`. A shy variant
might want a much lower floor and a sharper onset, read the raw W
yourself and curve it your own way:

```cpp
#include "core/personality_engine.hpp"

uint8_t ShyLamp::adjustBrightness(uint8_t base) const {
  const float w = lamp::personalityEngine.smoothedCrowdWeight();
  // Steeper: linear roll-off with a 0.2 floor, dim fast, hide hard.
  const float factor = std::max(0.2f, 1.0f - 0.1f * w);
  return static_cast<uint8_t>(base * factor + 0.5f);
}
```

### 2. Pattern-reactive: react to a specific disposition

`crowdComposition()` is the right tool when you care about *who's
there*, not just *how many*. A grumpy lamp that wakes up its base
animation any time a Salty peer enters range:

```cpp
void GrumpyLamp::loop() {
  const auto crowd = lamp::personalityEngine.crowdComposition();
  if (crowd.salty > 0 && !grumpyModeActive_) {
    activateGrumpyMode_();
  } else if (crowd.salty == 0 && grumpyModeActive_) {
    releaseGrumpyMode_();
  }
}
```

`crowdComposition()` snapshots live BLE-reachable peers each call, 
it's not pre-smoothed, so two consecutive calls can return different
counts as peers fade in and out at the edge of range. If you need
hysteresis, layer it yourself.

### 3. Custom greeting renderer

`greetingFor(lampId)` returns the same `GreetingTuning` the framework's
`SocialBehavior` would render for a peer. A variant lamp that wants
the same disposition-driven waveform but rendered on different
hardware (the snafu lamp does exactly this in
`software/lamp-os/src/lamps/snafu/greeting.cpp`) can read the tuning
and draw it however it wants:

```cpp
void SnafuGreeting::playFor(const RosterEntry& peer) {
  const auto tuning = lamp::personalityEngine.greetingFor(peer.macStr());
  // Drive your own ease-in / hold / fade-out using tuning's frame
  // counts; render onto your variant's pixel surfaces.
  renderEaseIn_(peer.baseColor, tuning.easeInFrames);
  renderHold_(peer.baseColor, tuning.holdFrames,
              tuning.pulseBackStrength, tuning.pulseBackCount);
  renderFadeOut_(peer.baseColor, tuning.fadeOutFrames);
}
```

You get the same `(SocialMode × Disposition) → Profile` matrix the
framework uses for free, including the Snub / PartialSnub / Effusive
waveforms, handy if you want a non-Standard lamp to still respond to
disposition the same way the rest of the fleet does.

## What's NOT exposed (and why)

- **Per-peer raw RSSI**, already on `LampRoster` directly. Iterate
  `lampRoster.getNear(...)` if you need distance.
- **The smoothing state buffer** (`sampleBuf_`, `sampleHead_`,
  `emaSeeded_`), internal to the smoother. Leaking it would freeze
  the smoother's internals as an external contract; don't want that.
- **The disposition weight table** (`weightForDisposition_`), private.
  If you have a use case for the mode-aware weight a single peer would
  contribute to W, ask and it can be exposed.

## Caveats

- `crowdDimFactor()` is mode-gated. In Extrovert it returns 1.0
  regardless of the smoother's internal state, by design.
- `smoothedCrowdWeight()` returns the underlying smoothed W *regardless
  of mode*, but the weighting itself is mode-dependent, Ambivert
  zeroes Fond and Smitten contributions, Introvert weights Fond at
  0.5. If you want a mode-independent peer count, use
  `crowdComposition()` and sum the fields you care about.
- `crowdComposition()` returns counts of peers visible *right now* via
  BLE. It is not smoothed; expect jitter at the edge of range. If you
  want a stable "is anyone Salty around" gate, debounce it caller-side
  (a few seconds of confirmation before flipping state is usually
  enough).
- All accessors are pure reads. They don't mutate engine state and
  they don't fire any effects.

## Cross-references

- `docs/dev/personality-greetings.md`, the behavioral / visual side of
  greetings: what profiles look like, the cooldown + fatigue rules.
- `docs/dev/lamp-framework.md`, how to author a custom lamp variant.
- `software/lamp-os/src/core/personality_engine.hpp`, canonical
  declaration; the code wins ties, update this doc when it doesn't.

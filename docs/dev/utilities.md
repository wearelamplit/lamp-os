# Utility modules (for expression + behavior authors)

The toolbox you reach for *inside* a `draw()` / `onUpdate()` / behavior
`update()`. These are the small, stable mechanical helpers, color math,
fades, randomness, peer lookups, that the framework's own behaviors are
built from. Nothing here mutates global state or fires effects; they're
pure helpers and read-only queries.

Two things this doc deliberately does **not** cover, because they have
their own homes:

- **Crowd / social signals** (disposition-weighted lamp count,
  `crowdDimFactor()`, `crowdComposition()`, per-peer greeting tuning),
  these live on `PersonalityEngine`. See
  [`personality-signals.md`](personality-signals.md). That's the
  "how many lamps are around, weighted by how I feel about them" API.
- **The `Expression` base class** (`onTrigger`/`draw`, palette, cadence,
  targets), see [`expressions.md`](expressions.md).

The code wins ties. Update this doc when a signature here drifts.

---

## Color & light math

### `Color` — `util/color.hpp`
RGBW pixel value. The unit every buffer and helper traffics in.

```cpp
Color(uint8_t r, uint8_t g, uint8_t b, uint8_t w);
bool operator==(const Color&) const;
std::string colorToHexString(Color);          // -> "#rrggbbww" (9 chars, leading '#')
Color       hexStringToColor(std::string);     // parse the above; requires the '#'
uint32_t    colorDistance(Color a, Color b);   // Euclidean (sqrt'd), for "are these close?"
```

### Fades & easing — `util/fade.hpp`
Interpolate a channel or a whole color across `steps`. Quadratic
(`ease`/`fade`) reads more organic; linear is cheaper and predictable.

```cpp
uint8_t ease(uint8_t start, uint8_t end, uint32_t duration, uint32_t step);
uint8_t easeLinear(uint8_t start, uint8_t end, uint32_t duration, uint32_t step);
Color   fade(Color start, Color end, uint32_t steps, uint32_t step);
Color   fadeLinear(Color start, Color end, uint32_t steps, uint32_t step);
```

For per-pixel inner loops where you don't want to recompute the factor
each pixel, split it: compute the factor once, then blend each pixel.

```cpp
uint32_t computeLinearFactor(uint32_t step, uint32_t duration); // once per frame
uint8_t  mixByteLinear(uint8_t start, uint8_t end, uint32_t factor);
Color    mixColorLinear(const Color& start, const Color& end, uint32_t factor);
```

### Gradients — `util/gradient.hpp`
Build a ramp of colors to lay across a strip.

```cpp
std::vector<Color> calculateGradient(Color start, Color end, uint8_t steps);
std::vector<Color> buildGradientWithStops(uint8_t numPixels,
                                          std::vector<Color> colorStops); // multi-stop
```

### Brightness — `util/levels.hpp`
Scale a channel or color down by a percentage (0–100).

```cpp
uint8_t calculateBrightnessLevel(uint8_t value, uint8_t percentage);
Color   setColorBrightness(Color, uint8_t percentage);
```

---

## Pixel buffer

### `FrameBuffer` — `core/frame_buffer.hpp`
What your `draw()` actually writes into. Every `Expression` already holds
the right one as `this->fb` (target-bound — don't capture `baseBuffer` /
`shadeBuffer` off the manager yourself). One `Color` per pixel.

| Field / method | Type | What |
|---|---|---|
| `buffer` | `std::vector<Color>` | Your write target — index `[0, pixelCount)`, one entry per pixel |
| `pixelCount` | `uint8_t` | Loop bound; never hardcode strip length |
| `defaultColors` | `std::vector<Color>` | The user's configured palette — your "resting" colors |
| `previousBuffer` / `previousBrightness` | `std::vector<Color>` / `uint8_t` | Last committed frame, for change detection (skip redraw when nothing moved) |
| `flush()` | `void` | Push `buffer` to the NeoPixel driver — the framework calls this; authors rarely do |

---

## Randomness

### `FastRng` — `util/fast_rng.hpp`
xorshift32 PRNG, ~8 bytes of state vs ~2.5 KB for `std::mt19937`. Every
`Expression` already owns one as the `rng` member, seeded from
`esp_random()` — just use `this->rng`. Construct your own only in a
non-Expression context.

```cpp
uint32_t next();                       // raw next value
uint32_t range(uint32_t lo, uint32_t hi); // unbiased, inclusive [lo, hi]
```

---

## Mesh / peer queries

### `NearbyLamps` — `components/network/mesh/nearby_lamps.hpp`
Live roster of lamps this one can currently see. Global singleton
`lamp::nearbyLamps`; also reachable as `behaviorContext()->nearbyLamps`
(null-check that pointer). Use this for "who's around" / "react when a
lamp shows up". For *weighted* crowd reactions, prefer
`PersonalityEngine` (see [`personality-signals.md`](personality-signals.md)).

```cpp
std::vector<NearbyLamp> getReachableViaBle(uint32_t maxAgeMs);    // short-range, sorted by RSSI (nearest first)
std::vector<NearbyLamp> getReachableViaEspNow(uint32_t maxAgeMs); // mesh-range
std::vector<NearbyLamp> getAll();
bool findByBdAddr(const std::string& bdAddr, NearbyLamp& out);
bool findByMac(const uint8_t mac[6], NearbyLamp& out);
```

Each `NearbyLamp` carries `name`, `baseColor` / `shadeColor`, `bdAddr`,
`mac`, `lastRssi`, `firstSeenMs` (use for arrival edge detection — see
[`lamp-framework.md`](lamp-framework.md)), `lastSeenViaBleMs` /
`lastSeenViaEspNowMs`, `firmwareVersion`, `protocolVersion`, `otaState`.

### Proximity — `util/proximity.hpp`
Bucket an RSSI into a coarse distance tier, so you don't hardcode dBm
thresholds in every expression.

```cpp
enum class Proximity : uint8_t { Near = 0, Around = 1, Far = 2 };
Proximity proximityFor(int8_t rssi);
uint8_t   proximityToInt(Proximity);
```

---

## Disposition / stored social state

### `Config` disposition API — `config/config.hpp`
How *this* lamp feels about a specific peer, persisted across reboots.
Disposition is the 1–5 scale the crowd weighting is built on.

```cpp
uint8_t getDisposition(const std::string& bdAddr) const; // 1=Salty 2=Wary 3=Neutral 4=Fond 5=Smitten (3 = unknown peer)
void    setDisposition(const std::string& bdAddr, uint8_t value); // debounced NVS flush
String  asDispositionsJson() const;
```

`getDisposition` answers "do I know this lamp, and how do I feel about
it?" for a single peer. For the aggregate weighted view across everyone
in range, that's `PersonalityEngine::crowdComposition()` /
`smoothedCrowdWeight()`.

---

## Small helpers

| Helper | File | What |
|---|---|---|
| `isValidBdAddr(const char*)` | `util/bd_addr.hpp` | Validate canonical `AA:BB:CC:DD:EE:FF` form before keying a disposition |
| `base64::encode(const uint8_t*, size_t)` | `util/base64.hpp` | Tiny header-only encoder (wisp palette payloads) |

---

## Cross-references

- [`personality-signals.md`](personality-signals.md) — disposition-weighted
  crowd signals (the "weighted social group number"), worked examples.
- [`expressions.md`](expressions.md) — the `Expression` base class and the
  spatial primitives that wrap some of the helpers above.
- [`lamp-framework.md`](lamp-framework.md) — `BehaviorContext` service
  pointers, arrival edge detection, the stable-vs-internal API matrix.

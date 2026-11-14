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
                                          const std::vector<Color>& colorStops); // multi-stop
void buildGradientWithStopsInto(uint8_t px, const std::vector<Color>& stops,
                                Color* dst);   // same bytes, caller's buffer
std::vector<Color> buildGradientLoop(uint8_t px, const std::vector<Color>& stops);
uint8_t gradientStopCount(const std::vector<Color>& stops);  // stops the builders use
```

`buildGradientWithStops` runs open, first stop to last. `buildGradientLoop`
is its closed form, with one more segment from the last stop back to the
first, so the last pixel leads into pixel 0 as any stop boundary does. Both
fill their segments the same way and use the first `gradientStopCount` stops,
at most 8. With fewer than 2 the loop builder returns what
`buildGradientWithStops` does. Code that renders every frame calls
`buildGradientWithStopsInto`, which writes exactly `px` colours into a buffer
the caller owns and allocates nothing.

### Colour rotation: `util/color_rotation.hpp`
A role's colours (one surface's scene, shade or base) flowing round a closed
loop, one loop every `periodS` seconds. Code that writes a role's scene builds
it with these; `ConfiguratorBehavior` draws it.

```cpp
enum class RotationDir : uint8_t { Up, Down };   // physical direction on the surface
struct ColorRotation { bool on; uint8_t periodS; RotationDir dir; };
constexpr uint8_t clampRotationPeriodS(int32_t s);  // into [kRotationMinPeriodS, kRotationMaxPeriodS]

struct RoleScene { std::vector<Color> colors; ColorRotation rotation; };
RoleScene buildRoleScene(uint8_t px, const std::vector<Color>& stops,
                         const ColorRotation& rotation);

class RotationClock {
  void     advance(uint32_t nowMs, uint8_t periodS, bool towardZero);
  void     reset();          // back to the settled position
  uint32_t phase() const;    // 2^32 is one loop
};
uint32_t loopOffset(uint32_t phase, size_t len);  // shift in 1/256 px
Color    sampleLoop(const std::vector<Color>& loop, uint32_t offset, size_t i);
```

- `buildRoleScene` is the one choice between an open gradient and a loop.
  Rotation on with a `gradientStopCount` of 2 or more builds the loop and keeps
  `on`; anything else builds the open gradient and clears `on`. Build every role scene
  through it, so `on` is set only on a loop.
- `periodS` runs from `kRotationMinPeriodS` (5) to `kRotationMaxPeriodS` (120),
  default `kRotationDefaultPeriodS` (40). `advance` clamps it with
  `clampRotationPeriodS`.
- `advance` steps by `lampos::frameDeltaMs`, the time since the last call
  capped at `lampos::kMaxFrameDeltaMs` (100 ms). The cap keeps a stall from
  jumping the pattern on resume; the cost is that advances spaced more than
  100 ms apart run slower than `periodS`. `towardZero` moves the colours
  toward buffer index 0. Turning a physical `RotationDir` into it is the
  caller's job, because which end is up depends on the surface.
- `loopOffset` takes the loop's pixel count as `len`. `sampleLoop` reads pixel
  `i` shifted by that offset, blending the two pattern pixels it falls
  between. `loop` must be non-empty.

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
| `defaultRotation` | `ColorRotation` | The saved scene's rotation, set with `defaultColors` from one `buildRoleScene`; on only when `defaultColors` is a loop |
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

### `LampRoster` — `components/network/mesh/lamp_roster.hpp`
Live roster of lamps this one can currently see, on two facets: **near**
(seen via BLE — physically close; gates greetings and crowd-dim) and
**mesh** (seen via ESP-NOW — network reachable; gates OTA and presence).
Global singleton `lamp::lampRoster`; also reachable as
`behaviorContext()->lampRoster` (null-check that pointer). Use this for
"who's around" / "react when a lamp shows up". For *weighted* crowd
reactions, prefer `PersonalityEngine` (see
[`personality-signals.md`](personality-signals.md)).

```cpp
const std::vector<NearbyCopy>& snapshotNear(uint32_t maxAgeMs);  // near, sorted by RSSI (nearest first)
std::vector<MeshPeer>& getMesh(uint32_t maxAgeMs);  // ESP-NOW-seen, OTA/cascade fields only
std::vector<ListedPeer>& getAll();  // nearby-JSON fields only; held until releaseAll()
bool findByMac(const uint8_t mac[6], RosterEntry& out);
// Nearest un-greeted arrival passing `accept`; in-place scan, no snapshot/sort.
template <typename Accept>
bool bestUngreetedArrival(uint32_t maxAgeMs, uint32_t now, Accept accept, RosterEntry& out);
```

Prefer the `forEach*` visitors over the snapshot getters for hot-path / arrival
work — they keep `RosterEntry` copies off the small heap.

`RosterEntry` is a trivially-copyable POD (112 B) held in a fixed
static array of `kCapacity` (50); snapshots never allocate per entry.
**Identity keys on `mac`** (the stable social identity used by the roster,
disposition store, and greeting dedup); `name` is a stored display field
(user-set), so a rename doesn't orphan an entry and two same-named lamps don't
collide. Each entry carries `name`, `baseColor` / `shadeColor`, `mac` bytes +
`hasMac` (canonical colon-hex `lampId` string via `macStr()`), `lastRssi`,
`lastSeenNearMs` / `lastSeenMeshMs`, `firmwareVersion`, `protocolVersion`,
`otaState`. For arrival edge detection see
[`lamp-framework.md`](lamp-framework.md).

---

## Disposition / stored social state

### `Config` disposition API — `config/config.hpp`
How *this* lamp feels about a specific peer, persisted across reboots.
Disposition is the 1–5 scale the crowd weighting is built on.

```cpp
uint8_t getDisposition(const std::string& lampId) const; // 1=Salty 2=Wary 3=Neutral 4=Fond 5=Smitten (3 = unknown peer)
void    setDisposition(const std::string& lampId, uint8_t value); // a user edit, debounced NVS flush
std::string asDispositionsJson() const;
DispositionStore& dispositions(); // the full rows: peer(), and the engine's writes
```

`getDisposition` answers "do I know this lamp, and how do I feel about
it?" for a single peer. For the aggregate weighted view across everyone
in range, that's `PersonalityEngine::crowdComposition()` /
`smoothedCrowdWeight()`.

`dispositions().peer(mac, uptimeMs())` returns the whole `PeerRecord`
(disposition, score, meetings, ages); the store's rules are in
[`social.md`](social.md#stored-social-rows).

---

## Small helpers

| Helper | File | What |
|---|---|---|
| `isValidBdAddr(const char*)` | `util/bd_addr.hpp` | Validate canonical `AA:BB:CC:DD:EE:FF` colon-hex form before keying a disposition on a `lampId` |
| `base64::encode(const uint8_t*, size_t)` | `util/base64.hpp` | Tiny header-only encoder (wisp palette payloads) |
| `lampos::timeReached(now, deadline)`, `lampos::isLater(a, b)`, `lampos::msSince(now, stamp)` | `lampos/clock.hpp` (`software/shared/clock`) | Compare 32-bit `millis()` stamps across the 49.7-day wrap. Every deadline, elapsed-time and most-recent comparison on a millis stamp goes through these or an unsigned `now - start`, never a raw `<` / `>` between stamps |
| `lampos::retireStamp(now, stamp)`, `lampos::kRetiredAgeMs` | `lampos/clock.hpp` | Hold a stamp no older than 24.8 days, so it stays outside every shorter window and never reads as recent again one wrap later. A stamp read against a window either gets retired this way (or by clearing its owner's valid flag) from a path that runs at least every 24.8 days, or is refreshed that often by design |
| `lampos::frameDeltaMs(now, last)`, `lampos::kMaxFrameDeltaMs` | `lampos/clock.hpp` | The time an animation steps by since its previous frame, capped at 100 ms, so the first frame after a stall or after weeks frozen in home mode steps like a normal one. Every per-frame animation delta on a millis stamp goes through it |
| `lamp::uptimeMs()`, `lamp::uptimeSince(stamp)` | `util/uptime.hpp` | 64-bit milliseconds since boot, the clock `millis()` truncates; it never wraps. Use it for a stamp read only on an event or from code home mode can skip for weeks, where no periodic path can retire it. `uptimeSince` gives an uptime stamp's age, saturated to 32 bits. Native tests drive the clock through `g_mock_millis_wraps` in the `esp_timer.h` stub |
| `wisp::uptimeMs()` | `software/wisp/src/util/uptime.hpp` | The wisp's 64-bit milliseconds since boot, for the same kind of stamp: the unicast peer table's last-use stamps (`PeerSlots`) and the `lastSeenMs` the wisp puts in its status |

---

## Cross-references

- [`personality-signals.md`](personality-signals.md) — disposition-weighted
  crowd signals (the "weighted social group number"), worked examples.
- [`expressions.md`](expressions.md) — the `Expression` base class and the
  spatial primitives that wrap some of the helpers above.
- [`lamp-framework.md`](lamp-framework.md) — `BehaviorContext` service
  pointers, arrival edge detection, the stable-vs-internal API matrix.

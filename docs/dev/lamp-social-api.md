# Lamp-to-lamp social API

The author-facing API a custom lamp uses to react to its peers: notice an
arrival, greet it, read how this lamp feels about it, ask the crowd to greet
back. Every entry point is a method on `BehaviorContext`, the per-behavior
service surface (`core/behavior_context.hpp`). This is the reference; for the
task narrative (build a variant, wire behaviors) read
[`building-custom-lamps.md`](building-custom-lamps.md), and for the runtime
underneath read [`lamp-framework.md`](lamp-framework.md). The code wins ties;
update this doc when it doesn't.

## BehaviorContext

Every `AnimatedBehavior` receives a `BehaviorContext*` before its first
`control()`. Inside a behavior, `behaviorContext()` returns it. The context
exposes framework services as raw pointers plus a few visitor/query helpers,
so a custom behavior never touches globals or snapshots the roster by hand.

```cpp
struct BehaviorContext {
  Compositor* compositor = nullptr;
  ExpressionManager* expressionManager = nullptr;
  std::vector<FrameBuffer*> expressionFrameBuffers;   // [shade, base]
  ConfiguratorBehavior* baseConfigurator = nullptr;
  ConfiguratorBehavior* shadeConfigurator = nullptr;
  LampRoster* lampRoster = nullptr;    // peer roster (near + mesh), RSSI
  Greetable* greeting = nullptr;       // active greeting behavior, or null
  MeshLink* meshLink = nullptr;        // mesh send surface

  void forEachArrival(uint32_t maxAgeMs,
                      const std::function<bool(const PeerView&)>& cb);
  void forEachNearby(const std::function<bool(const PeerView&)>& cb);
  void onArrival(std::function<void(const PeerView&)> cb);

  GreetingTuning   greetingFor(const std::string& peerLampId) const;
  uint8_t          dispositionOf(const std::string& peerLampId) const;
  CrowdComposition crowd() const;
  float            crowdWeight() const;
  // Physical facade (paint / brightness) — see building-custom-lamps.md, the author verbs.
};
```

**Every service pointer is nullable. Null-check before you dereference.** The
framework wires `lampRoster`, `greeting`, and `meshLink` at boot before any
`control()` runs, but a variant that masks a feature out can leave one null.

```cpp
void snafu::Greeting::control() {
  if (!context_ || !context_->lampRoster) return;
  // ... now safe to use context_->lampRoster
}
```

The paint/brightness verbs (`setSolidColor`, `setBrightness`,
`setSurfaceBrightness`, `setGradient`) live on the same struct but belong to the
physical author facade, documented in
[`building-custom-lamps.md`](building-custom-lamps.md#3-call-the-author-verbs--behaviorcontext).

## PeerView

The `forEach*` visitors and `onArrival` hand you a `PeerView`
(`core/behavior_context.hpp`), a non-owning read of one roster peer:

```cpp
struct PeerView {
  const char*    name = "";        // display name; valid only during the callback
  const uint8_t* mac  = nullptr;   // 6 bytes; valid only during the callback
  bool           hasMac = false;
  Color          baseColor;
  Color          shadeColor;
  int8_t         rssi = -127;
  char           lampId[18] = {0}; // canonical colon-hex mac, empty when !hasMac
};
```

`name` and `mac` alias the live roster entry, so a `PeerView` is valid **only
during the visitor call**. Copy `lampId` (or the colors) out if you need to
keep anything past the callback. `lampId` is the disposition/greeting key you
pass to `dispositionOf()` / `greetingFor()`.

## Noticing a peer: pull vs push

Two ways to react to an arrival, matching two lifetimes.

### Pull — `forEachArrival` (per-tick, from `control()`)

Ask, closest-first, and greet one ungreeted near arrival per call. **The
consumer owns the ack**: the visitor returns `true` to say "I greeted this
peer" (the framework acks it and stops) or `false` to leave it retriable next
tick. An un-acked arrival is the retry token — a peer that arrives while you're
busy simply comes back. `maxAgeMs` bounds how recent a near sighting still
counts as an arrival. snafu's whole greeting is this one call:

```cpp
context_->forEachArrival(/*maxAgeMs=*/5000, [this](const lamp::PeerView& p) {
  if (!context_->greeting) return false;   // nothing to greet with; keep p retriable
  context_->greeting->triggerGreeting(p);
  return true;                             // acks p AND stops (one greet per call)
});
```

The return value is a single signal: **true acks the peer and halts** (ack-and-
continue is not expressible — one greet per call); **false leaves the peer
un-acked and retriable**. Underneath, `forEachArrival` calls
`LampRoster::bestUngreetedArrival()`, an in-place scan under the roster mutex
(no snapshot, no sort — see [`embedded-heap.md`](embedded-heap.md)) that fills
`out` with the highest-RSSI ungreeted near arrival. Reach for that primitive
directly only when you need a custom `accept` predicate; otherwise prefer the
visitor, which keeps the ack coupled to the greet.

`forEachNearby` is the read-only sibling: it visits currently-near peers,
closest first, never acks, and stops early when the visitor returns `true`.

### Push — `onArrival` (attach-once, from `createBehaviors`)

Register a callback once at boot and the framework fires it exactly once per
genuinely-new near peer, deduped and re-armed in `ArrivalNotifier`
(`core/arrival_notifier.hpp`), decoupled from greeting entirely. The staff
lamp's friend-bloom:

```cpp
compositor.behaviorContext().onArrival([](const lamp::PeerView& peer) {
  constexpr uint8_t kDispositionFond = 4;
  if (config.getDisposition(peer.lampId) < kDispositionFond) return;
  lamp::ExpressionInvocation inv;
  inv.type = "bloom";
  inv.target = lamp::TARGET_BOTH;
  expressionManager.triggerInvocation(inv, peer.mac, /*broadcast=*/false);
});
```

`ArrivalNotifier` semantics:

- A fixed MAC set records who has fired — no `RosterEntry` flag (which resets on
  prune and taxes every snapshot copy) and no coupling to the greeting
  `acknowledged` bit (a greeted peer is still "arrived").
- **Departure is leaving the near window, not a roster prune.** A mesh-reachable
  peer that walks out of BLE range never prunes, so re-arm keys on an edge in
  `lastSeenNearMs` vs the near window. A peer re-fires only after it departs the
  near window and returns.
- A fresh arrival is bounded by a short window (`kArrivalMaxAgeMs`, 5 s), so a
  stale sighting doesn't read as an arrival.
- The near-set diff runs every tick over `snapshotNear`, whose freshness cache
  (`LampRoster::kSnapshotCacheMs`, 1 s) keeps the roster snapshot off the hot
  path, so an arrival surfaces within that window. Ticked on Core 1; a no-op
  when no callback is registered.

### Which to use

Pull when the reaction *is* the greeting and one-per-call ack-coupling is what
you want (a custom greeting renderer). Push for a side reaction that shouldn't
touch the greeting ack (a bloom, a chime, a log) and should fire once per real
arrival.

## Social reads

Per-tick, by value, allocate nothing. Each wraps a `PersonalityEngine` global so
a behavior routes through `context_` instead of the singleton, null-safe against
the engine not being wired yet.

| Global | `BehaviorContext` equivalent | Returns |
|---|---|---|
| `personalityEngine.greetingFor(lampId)` | `context_->greetingFor(lampId)` | per-peer `GreetingTuning` waveform |
| `config.getDisposition(lampId)` | `context_->dispositionOf(lampId)` | disposition 1..5 (3 = neutral) |
| `personalityEngine.crowdComposition()` | `context_->crowd()` | `CrowdComposition` (counts by disposition) |
| `personalityEngine.smoothedCrowdWeight()` | `context_->crowdWeight()` | smoothed weighted-crowd scalar |

The weighting curves, per-mode floors, and worked examples (a shy lamp's steeper
crowd curve, a disposition-reactive scene, a custom greeting renderer) live in
[`personality-signals.md`](personality-signals.md). The greeting waveform shapes
`greetingFor` returns are in
[`personality-greetings.md`](personality-greetings.md).

## Greeting state on the wire

A greeting behavior implements `Greetable` (`behaviors/greetable.hpp`):
`triggerGreeting(const PeerView&)` plays the greeting, `greetingState()` returns
the live `GreetingState`. The framework routes both through
`BehaviorContext::greeting`, so dispatch never switches on lamp type.

```cpp
struct GreetingState {
  bool        active = false;
  std::string peerLampId;   // greeted peer's colon-hex mac, empty when idle
  std::string kind;         // "warm" | "reserved" | "snub" | "glitch" | "pulse"
};
```

The state is pushed on `CHAR_STATE_NOTIFY` (field `greeting`) on start and stop.
`kind` is a stable short label: `"warm"` (pulsed hold), `"reserved"` (plain
hold), `"snub"` (dark-in-peer-color) from `SocialBehavior`, `"glitch"` from
the snafu variant, or `"pulse"` from the Lioness variant. `peer` carries
`peerLampId`; empty when idle.

## Cross-references

- [`building-custom-lamps.md`](building-custom-lamps.md) — the author task guide;
  the paint / brightness verbs, the input-hardware seam, the register-a-variant
  recipe.
- [`social.md`](social.md) — how greetings and crowd-dim fit together
  conceptually.
- [`personality-signals.md`](personality-signals.md) — the crowd/disposition
  signal semantics behind the social reads.
- [`personality-greetings.md`](personality-greetings.md) — the greeting waveform
  contract.
- [`networking.md`](networking.md) — the `CHAR_STATE_NOTIFY` wire format.

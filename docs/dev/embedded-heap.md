# Embedded heap discipline

The lamp's real runtime free heap is ~58 KB, fragmented in practice to a ~23 KB
largest contiguous block (BLE, ESP-NOW, the webapp, and wifi are all resident at
once). `malloc(N)` needs N *contiguous* bytes, so the number that bites is the
largest free block, not total free. The 1.1.1 audit found a whole column of
independent bugs that were one anti-pattern hitting this ceiling. Read this before
adding any feature that snapshots the roster, tracks per-peer state, or answers
"who's nearby."

## The recurring anti-pattern

Fleet-awareness features (crowd-dim, claims, nearby lists, greetings, personality)
all answer "who's around me." The convenient implementation snapshots the roster
into a fresh `std::vector`, one `std::string` per entry, then sorts it. That is
fine on a laptop and lethal here, and each feature reinvented it. `personality_engine`
ran it on every main-loop tick (kHz); the wisp ran it at ~200 Hz. They were the top
fragmentation drivers on the heap.

Two properties make it lethal:

- **kHz alloc/free churn of variable-size blocks fragments a no-compaction heap.**
  The vector and its N strings allocate and free every pass. Nothing moves once
  placed, so a long-lived allocation that lands in a freed gap keeps that gap from
  merging back. The largest contiguous block erodes and stays eroded. It does not
  even recover when the triggering BLE client disconnects, because the churn never
  stops.
- **A 17-char lampId (`"AA:BB:CC:DD:EE:FF"`) defeats std::string SSO** (the inline
  buffer holds ~15 chars). Every address becomes a separate heap allocation.

## Rules

1. **Watch `largest`, not just `free`.** `heap_caps_get_largest_free_block()` is the
   real budget. `util/heap_probe.hpp` (`lamp::logHeap`, LAMP_DEBUG) logs both at
   boot / mesh / webapp / ble-connect / ble-disconnect / ota-stream / web-save. Tap
   the serial, grep `[heap]`, find the checkpoint that craters the block. Measure the
   fragmenter; do not theorize it.
2. **No heap churn on the hot path.** Never rebuild-and-sort a fresh allocation per
   loop tick. Throttle it, dirty-gate it (rebuild only when the roster changed), or
   reuse a member buffer `reserve()`d once to its max.
3. **No per-entry `std::string` for addresses or short keys.** Use a fixed
   `char[18]` or the raw 6-byte address. Anything past ~15 chars heap-allocates.
4. **Scan in place, do not snapshot.** "Closest Smitten peer", "nearest by RSSI",
   "count of X": iterate the roster where it lives, allocation-free. Do not
   materialize a sorted copy to read one value off it.
5. **Front-load permanents; preallocate.** Long-lived allocations belong at boot,
   while the heap is still one contiguous slab, `reserve()`d to their max, so they
   pack low instead of bisecting the runtime free region.
6. **No heavy work under a spinlock or on the BLE task.** A JSON build inside
   `portENTER_CRITICAL` masks interrupts for milliseconds; a 10 KB JSON built on the
   NimBLE host task competes with the largest free block. Pre-build on a Core-1
   cache and let the mux cover only the pointer swap. Every section payload but one
   already works this way; match it.
7. **Prefer a shared roster view over a new snapshot.** "Who's around" gets asked
   from many features, so a single allocation-free roster iterator earns its keep.
   Do not let the next feature roll its own vector-of-strings copy.

## Why it recurs

The features that make the fleet feel alive constantly re-answer "who's near me,"
so the snapshot pattern gets reinvented per feature. It compiles clean, passes the
native tests, and only surfaces as a functional failure (the web config UI will not
serve, the wisp goes invisible) under real fleet and BLE load on the bench, never on
a quiet desk. Assume heap pressure, measure with the probe, and reach for
allocation-free access to shared fleet state.

Related: [`code-smells.md`](code-smells.md) (the reinvention smell) and
[`networking.md`](networking.md) (roster and mesh).

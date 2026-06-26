# ADR 0003 — Dual-core concurrency model (Core 0 radio / Core 1 loop)

**Status:** Accepted (current system)

## Context

The lamp runs on an ESP32-WROOM, which is **dual-core** (PRO_CPU = Core 0,
APP_CPU = Core 1). The firmware has two very different kinds of work:

- **Radio / receive work** — ESP-NOW receive callbacks, frame parsing, and
  writing OTA chunks to flash. This is driven by the WiFi stack and is
  latency-sensitive: the ESP-NOW RX ring is small, so if the receive path
  stalls, frames are dropped silently.
- **Application work** — the render loop (frame buffers, fades, the LED strip at
  ~60 Hz), the OTA state machines (distributor + receiver), and the
  social/expression/personality engines.

These touch **shared state**: the inactive OTA partition, the received-chunk
bitmap, the distributor session fields, and the nearby-lamps store. Two cores
plus shared state means race discipline is mandatory, and the ESP32's per-core
caches have weak ordering with no per-byte atomicity guarantee.

A hard physical constraint shapes everything here: **`esp_partition_erase_range`
disables the cache + interrupts on _both_ cores** for the duration of the erase
(50-750 ms). During an erase, neither core's tasks run; only IRAM ISRs DMA into
the RX ring, which overflows in tens of ms. This is not a "use the other core"
problem — there is no other core during a flash erase.

## Decision

**Pin work by core, keep the receive path non-blocking, and guard shared state
with tight spinlocks + a published-handle gate.**

- **Core 0 (WiFi recv task):** ESP-NOW receive callbacks, frame parse/dispatch,
  and the OTA chunk write (`handleChunkOnRecvTask` → `esp_partition_write`).
  Rule: **never block Core 0.** It never sends mesh frames, never does long work,
  never holds a lock across I/O. Control frames it can't handle inline are handed
  to Core 1 via **typed-slot forwarders** (lock-free single-slot handoff).
- **Core 1 (Arduino loop):** the render loop + both OTA state machines (driven
  from `tick()` / `handleControlOnLoop`) + social/expression. All multi-step
  state-machine logic lives here, single-threaded with respect to itself.
- **Shared OTA partition — published-handle gate.** Core 1 prepares the receive
  (pick partition, erase, size the bitmap) and then arms a
  `publishedOtaHandle_` flag with a **release-store**; Core 0 checks it with an
  **acquire-load** before touching the partition pointer. On teardown Core 1
  disarms (store 0) first, then nulls the pointer. Core 0 only ever sees a
  fully-prepared partition or a disarmed gate — never an intermediate state.
- **Bitmap RMW — `eraseMux_` spinlock.** Core 0's `markChunkReceived` does a
  non-atomic byte RMW on the bitmap; Core 1's `isBitmapFull`/`firstMissingChunk`
  read it. A `portMUX` spinlock brackets *only* the byte op (interrupts off for a
  few instructions), preventing torn bytes / lost bits across cores.
- **Verify-vs-write barrier — `writesInFlight_`.** Before Core 1 reads the whole
  partition to verify the signature, it drains in-flight Core 0 writes, so a
  late chunk write can't land mid-read and produce a "sigverify-failed-with-a-
  full-bitmap" result.

## Alternatives considered

- **Single-core (treat it like the unicore wisp C6).** Rejected: wastes a whole
  core; the render loop and the radio receive would contend on one core, and a
  ~1.5 MB OTA would starve rendering (or vice-versa).
- **One big mutex over all shared state.** Rejected: contention + the temptation
  to hold it across I/O (flash writes, mesh sends), which pins ISRs and stalls
  the radio. The discipline is *many tight critical sections*, not one coarse one.
- **A FreeRTOS task per subsystem with queues everywhere.** Rejected as
  over-built for this scope; the two-core split + typed slots + a couple of
  spinlocks covers the real sharing without a message-passing framework.
- **Erase on "the other core" to avoid stalling receive (pipelined pre-erase).**
  Tried and reverted — see ADR 0002. The both-cores-stall fact means there *is*
  no other core during an erase; the cure was to move erase out of the receive
  window entirely (full-upfront erase), not to parallelise it.

## Consequences

**Good:**

- Receive and render run truly concurrently; a long OTA doesn't freeze the strip
  (and the strip's render doesn't drop ESP-NOW frames).
- The published-handle gate gives a clean, auditable cross-core ownership
  transfer for the OTA partition.

**Costs / rules we live with:**

- **"Don't block Core 0" is load-bearing.** Any blocking call added to the
  receive path (a lock held across flash I/O, a mesh send, a long parse) silently
  drops frames. The OTA chunk write must stay sub-millisecond — which is *why*
  the JIT per-sector erase had to leave the receive path (ADR 0002).
- **Flash erase is a both-core event.** It must happen outside the receive window
  (upfront), and watchdogs (TWDT on IDLE0, IWDT on interrupts-off) must be
  widened around it or the chip resets mid-erase (`rst:0x8 TG1WDT`).
- **Weak memory ordering is real.** Cross-core sharing needs explicit
  acquire/release (the gate) or a spinlock (the bitmap); a plain shared `bool`
  is not safe. Torn-read hazards (the `writesInFlight_` barrier) are subtle and
  were the likely root of a historical OTA regression.
- **Cross-core bugs are timing-dependent** and rarely reproduce in a few runs —
  changes to this layer warrant adversarial review + targeted timing tests, not
  just "it worked N times."

## References

- `software/lamp-os/src/components/firmware/firmware_receiver.{hpp,cpp}` —
  the published-handle gate, `eraseMux_`, `writesInFlight_`, the typed-slot intake.
- `software/lamp-os/src/components/firmware/firmware_distributor.{hpp,cpp}` —
  Core 0 recv-task parse vs Core 1 `tick()`/streaming-task split.
- ADR 0002 — why flash erase moved off the receive path.

# ADR 0002 — Firmware OTA over the ESP-NOW mesh

**Status:** Accepted. Upfront-erase landed; cadence locked at 30 ms (see
`EXPERIMENTS.md`). Gossip + signing + chunking decisions are settled.

## Context

The fleet must update its own firmware without USB or physical access, over the
ESP-NOW mesh (ADR 0001), against ~22 lamps that come and go. Constraints:

- **No reliable server.** A venue may have no WiFi, so HTTP-from-a-server OTA
  isn't dependable; the fleet must distribute firmware *to itself*.
- **Security.** A firmware image is the highest-value attack surface; an
  unsigned/unauthenticated image must never boot.
- **Lossy broadcast.** ESP-NOW is unacknowledged and drops frames scattered;
  a ~1.5 MB image is ~7750 chunks at the 200-byte chunk size.
- **Flash physics.** On ESP32, `esp_partition_erase_range` disables the cache +
  **both cores'** interrupts for 50-750 ms — during which the ESP-NOW RX queue
  cannot drain. You cannot erase and receive at the same time, on any core.
- **Mixed-version fleet during a wave.** Lamps update at different times, so the
  protocol must tolerate version skew mid-rollout.

## Decision

**Lamp-to-lamp gossip OTA**: each lamp can distribute *its own running image* to
any nearby peer running an older version. No central scheduler.

### Distribution model
- **Event-driven targeting** (not scan-based): `SocialBehavior` already iterates
  nearby peers each tick; when the distributor is `Idle` and a peer's version <
  ours, it calls `considerPeerForOta(peerMac, peerVersion, ...)`. Single peer per
  session; the fleet propagates by gossip (whoever-is-higher offers
  whoever-is-lower, in range).
- **Sender-side type/channel gate**: a distributor skips offering to a peer whose
  `{type}-{channel}` (carried in a HELLO TLV) differs from its own — a `snafu`
  lamp is never offered a `standard` image, channels never cross. The receiver's
  silent-drop is the backstop.

### Transfer + recovery
- **Chunked** at 200 bytes over `MSG_FW_OFFER` → `ACCEPT` → `CHUNK`× → `DONE` →
  `RESULT`. The receiver bitmaps received chunks.
- **REQ-based recovery** over the lossy broadcast: the receiver re-requests
  missing chunks; the sender rewinds and re-serves. (The recovery is currently a
  *contiguous-range* REQ, which over-asks on scattered loss — see "Known limits".)
- **Receive-range versioning + TLV-first** so a wave across mixed versions
  doesn't split the mesh.

### Security
- **ed25519-signed images.** A 96-byte `LSIG` footer holds `{type}-{channel}` +
  the signature over the signed region. `verifyAndApply` verifies the signature
  (streaming SHA-256 from flash — the image doesn't fit in RAM) **before**
  `esp_ota_set_boot_partition`. A bad signature never boots.
- **Type/channel gating** via the footer's `{type}-{channel}` slot, enforced at
  both OFFER time and post-download.

### Flash handling — full-upfront erase (bench-validated this cycle)
The receiver erases the **entire image region once**, synchronously, **before**
arming the receive gate and sending ACCEPT — then the chunk-receive path is a
**pure write** (no erase). This removes the JIT-per-sector erase that used to
stall the RX queue mid-stream (the cause of ~60% forward-pass loss at fast
cadences). Erase is done in 64 KB blocks (~2-5 s, varies 2-3× by lamp's flash),
which fits inside a widened `kAcceptTimeoutMs` (15 s).

This replaces an earlier **pipelined pre-erase** that erased *during* the stream
and was reverted over an unlocalized sigverify regression. Full-upfront is a
strictly simpler shape (one erase site, one thread, no cross-core CAS), which is
why it doesn't reintroduce that failure class. (See ADR 0003 for the cross-core
detail; a `writesInFlight_` barrier — orthogonal to the erase shape — is the
likely real fix for the old "sigverify-failed-with-full-bitmap" bug.)

### Sender pacing (cadence) — locked at 30 ms
Inter-chunk spacing (`kStreamingChunkSpacingMs`) is a **sender-only** knob — the
receiver is rate-agnostic, so cadence is retunable later via a sender OTA with no
fleet lockstep. **Locked at 30 ms.** Upfront-erase lets faster cadences
(15-25 ms) complete too, but a follow-up campaign found the speed gain marginal
and noisy, while higher on-air burst density tracks with visible LED flicker
during OTA; below ~15 ms the RX queue overruns independent of erase. 30 ms keeps
the most RX-queue margin and runs flicker-free. See `EXPERIMENTS.md` for the data
and the locked decision.

## Alternatives considered

- **HTTP/WiFi OTA from a server** (`esp_https_ota`). Rejected as primary: depends
  on venue WiFi + a reachable server the fleet can't guarantee. Mesh OTA is
  infra-free.
- **Wisp as sole distributor.** Rejected: single point; the wisp is USB-flash-only
  and goes silent on a protocol bump (it's an "OTA island"). Gossip is more robust.
- **Pipelined / incremental pre-erase.** Tried, reverted (sigverify regression);
  superseded by full-upfront erase.
- **Fountain / erasure coding for recovery.** Strong fit for lossy broadcast +
  multi-receiver, but a real decoder + RAM/protocol change. Deferred — see Known
  limits. The current coarse REQ is "wasteful but rock-solid."

## Consequences

**Good:** no infrastructure, self-healing fleet propagation, signed + type-gated,
and (with upfront-erase) reliable at speed without the erase-stall failure class.

**Costs / known limits:**

- **Recovery is coarse.** The contiguous-range REQ over-asks on scattered loss
  (~12-15% duplicate chunks at 30 ms; thrashes badly below ~15 ms). A precise
  **NAK-bitmap** (request exact holes) or **fountain coding** would fix it — both
  deferred, because at the committed 30 ms it's a non-issue for a passive
  background process and the recovery path is freshly converged (don't perf it
  without need).
- **Slow-flash erase vs accept-timeout** is the gating fleet risk: a worst-case
  W25Q erase must stay under `kAcceptTimeoutMs`. Characterize across fleet flash
  parts before tightening it.
- **Multi-receiver waves are sequential** per distributor (single peer per
  session); fleet-scale waves rely on gossip parallelism, not one sender fanning
  out. Fountain coding is the only thing that fixes the per-receiver cost.
- **The wisp is an OTA island** (USB-flash only) — first suspect when it vanishes
  after a protocol bump.

## Must-pass gate before fleet rollout (upfront-erase)

1. Worst-case erase time across fleet flash variants (cold+hot) vs `kAcceptTimeoutMs`.
2. Power-cut during erase → boots running partition, re-OFFER re-erases.
3. Abort-then-reoffer at a different image length → hits `erasedForLen_` fail-loud.
4. Re-OFFER during the erase-blocked window → busy/idempotent correctness.
5. `snafu` variant: build + full OTA + cross-variant silent-drop holds.
6. Receiver-then-redistribute chain (A→B→C) on hardware.
7. ~20+ completion runs at the chosen cadence (real failure-rate denominator).

## References

- `software/lamp-os/src/components/firmware/firmware_distributor.{hpp,cpp}`,
  `firmware_receiver.{hpp,cpp}`, `firmware_signature.{hpp,cpp}`.
- `scripts/sign_firmware.py`, `scripts/inject_firmware_channel.py`.
- ADR 0001 (mesh), ADR 0003 (dual-core concurrency).

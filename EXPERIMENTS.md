# OTA Strategy Experiments

Goal: find the OTA strategy with the best **stability** (completion rate) **and speed**
(total transfer time), starting from the committed 30ms-pacing baseline.

## Bench
- jacko `c4:dd:57:eb:64:60` → `/dev/cu.SLAB_USBtoUART` (sender)
- flora `24:dc:c3:a6:c1:8c` → `/dev/cu.SLAB_USBtoUART9` (receiver)
- lamp3 `b8:d6:1a:44:a3:5c` → `/dev/cu.SLAB_USBtoUART7` (receiver #2 — more stability runs)
- Flash baud 921600. Image ≈ 7751 chunks (~1.55 MB).

## Metrics per run
- **completed?** (ed25519 rc=0 + reboot to new version)
- **forward yield** (unique chunks received when sender finishes forward pass)
- **dups** (recvChunksCount / unique)
- **recovery thrash** (DONE sends / REQ-serves)
- **total time** (OFFER → reboot)

## Baseline (from prior session, branch dev-cleanup)
| spacing | runs | completed | forward yield | recovery | time |
|---|---|---|---|---|---|
| 10ms | 1 | 0/1 FAIL | ~41% | 439 DONE / 883 REQ | timed out |
| 30ms | 2 | 2/2 ✅ | ~98% | ~102 DONE / ~230 REQ | ~4 min |

## Strategies under test
1. **upfront-erase + 30ms** — erase whole partition before ACCEPT, recv path = pure write. (decouple erase from cadence first)
2. **upfront-erase + 10ms** — once #1 works, drop cadence for speed (the prize: fast AND reliable)
3. **pacing 20ms**, **pacing 15ms** — find the safe-fast pacing point without the erase change
4. (stretch) upfront-erase + faster cadences

## Results
_(filled in as runs complete)_

### Run: upfront-erase + 30ms (decouple erase from cadence)
- jacko v124 → flora. **COMPLETED**, ed25519 rc=0, flora→v124.
- Erase: 379 sectors / 1.55MB, **no watchdog reset**, fit under 15s accept-timeout. ✅ implementation safe (no regression).
- forward yield 7749/7751; recovery 125 DONE / 314 REQ; dups ~15% (8915/7750).
- Verdict: upfront-erase is correct + safe. At 30ms it ≈ 30ms-only (erase wasn't the bottleneck at 30ms). Need the fast-cadence test to show its value.

### Run: upfront-erase + 10ms (the "prize" — fast cadence)
- jacko v125 → flora. **COMPLETED**, ed25519 rc=0, flora→v125. No watchdog.
- dups ~15% (8932/7750) — vs **60% at 10ms-only**: upfront-erase cut forward loss ~4×.
- BUT recovery still heavy (~523 DONE / 550 REQ): a residual ~15% non-erase loss (RX queue under raw 10ms rate) + inefficient REQ recovery (#18) thrashing.
- Verdict: upfront-erase lets 10ms COMPLETE (vs fail), but 10ms is still too fast for the RX queue. Sweet spot is a moderate cadence WITH upfront-erase.
- NOTE: dual-watch log garbles at 10ms dual-stream throughput; using tolerant greps + flash→complete wall-clock for time.

### Run: upfront-erase + 20ms (clean single-port)
- jacko v126 → flora. **COMPLETED**, ed25519 rc=0, flora→v126.
- **Erase: 379 sectors in 2264 ms (~2.3s)** — well under 15s timeout (5s would do).
- dups ~14% (8815/7750); OTA time (erase-done→rc=0) **212s** (~3.5 min). forward done MAX 7747/7751.
- Verdict: clean completion, ~2.3s erase. Modestly faster than 30ms (~240s). Recovery overhead (#18) still ~14% dups.

### Run: upfront-erase + 15ms (single-port) — SWEET SPOT
- jacko v127 → flora. **COMPLETED**, ed25519 rc=0, flora→v127.
- Erase 2186 ms (~2.2s); recovery **17 rounds** (low — RX keeps up); dups ~10% (8511/7750).
- **OTA time 152s (~2.5 min)** — fastest clean run; ~37% faster than 30ms.

## Cadence sweep summary (upfront-erase, single sender→flora, ~7750 chunks)
| cadence | completed | recovery rounds | erase | OTA time | notes |
|---|---|---|---|---|---|
| 10ms-only (NO erase) | ✗ FAIL | — | — | timed out | 60% forward loss |
| upfront + 10ms | ✓ | ~550 (HEAVY) | ~2.3s | ~slow (recovery-bound) | RX queue overruns at 10ms |
| **upfront + 15ms** | ✓ | **17 (low)** | 2.19s | **152s** | **sweet spot: fast + clean** |
| upfront + 20ms | ✓ | 14 (low) | 2.26s | 212s | clean, slower |
| upfront + 30ms | ✓ | low | ~2.3s | ~240s | clean, slowest |
| 30ms pacing (no erase) | ✓ | low | n/a (JIT) | ~240s | the committed baseline |

Key findings:
1. **Upfront-erase is SAFE** — no watchdog, no sigverify regression, completes every run. The simpler full-upfront shape did NOT reproduce the prior pipelined-pre-erase bug.
2. Erase cost is **~2.2s** (64KB block erase, 379 sectors) — fits easily under accept-timeout (could drop it 15s→5s).
3. **Upfront-erase removes the erase-stall loss**, which lets the cadence drop from 30ms→15ms (RX-queue floor) — **~37% faster** at the same reliability.
4. Below ~15ms the RX queue itself overruns (independent of erase) → heavy recovery thrash → no speed gain. **15ms is the floor.**
5. The recovery path (#18 REQ-bitmap) is the next lever — it's what bounds going faster than 15ms.

### Stability + multi-receiver (upfront-erase + 15ms)
| run | lamp | completed | erase | recovery | OTA time |
|---|---|---|---|---|---|
| 1 | flora | ✓ rc=0 | 2186ms | 17 | 152s |
| 2 | flora | ✓ rc=0 | 2259ms | 10 | 159s |
| 3 | flora | ✓ rc=0 | 2218ms | 26 | 160s |
| 1 | daisy | ✓ rc=0 | 5414ms | 21 | 109s |

- **4/4 completions across 2 physical lamps.** Recovery low (10-26 rounds). OTA 109-160s.
- Multi-receiver gossip: jacko v129 fanned out to BOTH flora + daisy sequentially, both → v129. ✓
- **Flash-erase time varies 2-3× across lamps** (flora ~2.2s, daisy 5.4s) → the 15s accept-timeout margin is justified (5s would be too tight for slow flash).

---

## FINAL ASSESSMENT & RECOMMENDATION (expert-reviewed)

### What we proved
- **Upfront-erase runs clean on the bench**: 8/8 completions (cadence sweep + 4 stability runs across 2 lamps, flora + daisy), ed25519 rc=0 every time, **no watchdog reset, no sigverify regression**. The simpler full-upfront shape (one synchronous erase site, no CAS/tristate) did NOT reproduce the prior pipelined-pre-erase bug.
- It **decouples completion from cadence**: 10ms-only FAILED (60% loss) → upfront+10ms COMPLETES (forward loss ~4× lower). Below ~15ms the RX queue itself is the floor (independent of erase).
- Erase cost **2.2-5.4s** (varies 2-3× across lamps), fits the 15s accept-timeout.
- Multi-receiver gossip fan-out works (one distributor → flora + daisy sequentially).

### What the data does NOT earn (honest limits)
- **"~37% faster / 15ms sweet spot" is n=1-per-cadence.** Inter-lamp variance is huge: daisy did 15ms in 109s vs flora's 152-160s at the *same* cadence (40% spread) — larger than the 15ms-vs-20ms gap used to crown 15ms. Direction is right; the exact numbers are not.
- **"Safe" from 8 completions can't bound a fleet failure rate** (rule of three: 0/8 still admits up to ~37% failure at 95% CI). Need ~20-30 runs at the chosen cadence.

### The real value (reframed)
The win isn't "37% faster" — it's **removing the entire JIT-erase-stalls-RX-queue failure class** that currently caps BOTH the streaming cadence AND how aggressive recovery (`kMaxReqRunChunks`) can be. **Upfront-erase is the *enabler* for the #18 REQ-bitmap recovery fix, not an alternative to it.** With no erase on the recv path, the REQ window can finally widen safely.

### The regression ghost
The prior pre-erase bug was never localized — BUT the `writesInFlight_` drain barrier in `verifyAndApply` (a named Core0-write-vs-Core1-read race fix) is the likely actual culprit/fix, **orthogonal to the erase shape**. Upfront-erase's structural simplicity removes the *other* fragility (3 erase sites + cross-core CAS) regardless.

### RECOMMENDATION: GO on upfront-erase, phased. NO-GO on 15ms-as-presented.
- **Phase A (merge first, low risk):** upfront-erase at **30ms** cadence. Same speed/reliability as today's committed baseline, but removes JIT erase from the recv path — banks the structural win + the #18 enabler with **zero speed bet**.
- **Phase B (earn it):** drop cadence to 20ms, then 15ms — each only after a real completion-rate sample (~20+ runs) + slow-flash characterization. Land on **20ms** until the daisy-vs-flora variance is explained (20ms has more RX-queue margin; the 15ms-beats-20ms claim is the thinnest result).
- **Do NOT** freeze at "30ms pacing forever" — that keeps JIT per-sector erase (the core hazard) live and blocks #18.

### Must-pass gate before any merge to `dev`
1. Worst-case 64KB block-erase time across fleet flash parts (cold+hot) → size `kAcceptTimeoutMs` to (worst-block × count)+margin. *(blocks everything; 5.4s observed is NOT the worst case)*
2. Power-cut during the erase → boots running partition, re-OFFER re-erases + completes.
3. Abort-then-reoffer at a DIFFERENT image length → must hit the `erasedForLen_ != offerTotalLen_` fail-loud path.
4. Re-OFFER (same + different version) DURING the erase-blocked window (Core 1 stalled 2-5.4s) → busy/idempotent correctness.
5. snafu variant: build + one full OTA + cross-variant silent-drop still holds.
6. Receiver-then-redistribute chain (A→B→C) on hardware.
7. ~20+ completion runs at the chosen cadence.
8. Native suite green (379/379).

### Deliverable
Upfront-erase implemented + comment-swept + 379/379 native + 3 firmware targets build. Cadence **locked at 30ms** (see decision below).

---

## DECISION (FINAL): cadence locked at 30ms

A follow-up campaign (multi-sender gossip bench, one monitored receiver over USB)
ran **15ms ×11, 20ms ×6, 25ms ×4 — 21/21 completions, zero failures** at every
cadence. This **supersedes the phased "drop to 20/15ms later" recommendation
above.** 30ms is locked. Reasoning:

- **Reliability is not the differentiator.** Every cadence 15-30ms completes
  reliably with upfront-erase — the erase-stall failure class is gone. So speed
  is the only axis in play, and it's weak (below).
- **Speed below 30ms is marginal and did not reliably materialize.** Forward-pass
  floors are 116/155/194/232s for 15/20/25/30ms, but measured times were
  dominated by recovery + multi-sender queue noise — 20ms's median (328s) landed
  *above* 25ms's (262s), which is physically backwards. The bench can't cleanly
  rank cadence speed; the theoretical gain is real but small and unobserved.
- **Flicker tracks cadence.** Visible LED flicker during OTA was clear at 15ms,
  less at 20ms, **none at 25ms or 30ms.** Cause is electrical/radio (on-air burst
  density), not rendering — the WS281x "reset-gap" source comment that claimed
  otherwise was audited and **debunked** (FrameBuffer dedup + Adafruit `canShow()`
  make that mechanism impossible). Slower cadence = lower burst density = no
  flicker.
- **30ms has the most RX-queue margin** for degraded fleet conditions (battery
  sag, RF contention, slow flash). Below ~15ms the RX queue overruns independent
  of erase.
- **Run counts are honestly thin at every cadence** (30ms=2 historical, 25ms=4,
  20ms=6, 15ms=11) — none bounds a fleet failure rate (rule of three needs
  ~20-30). 30ms is chosen for **margin + flicker-free**, *not* for being better
  proven. The only thing that would earn a faster cadence is a clean
  1-sender→1-receiver study of ~20+ runs.

**Cadence is a sender-side knob** (`kStreamingChunkSpacingMs`) — the receiver is
rate-agnostic, so this is retunable later via a normal OTA with no fleet
lockstep. Locking at 30ms forfeits no future flexibility.

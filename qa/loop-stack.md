# Pass: loop-task stack ceiling

Puts a number on the Arduino `loopTask` peak stack usage so the loop stack can
be shrunk from the 8192 B default (`getArduinoLoopTaskStackSize()`) to a
measured ceiling without risking a field overflow. `loopTask` (Core 1) runs
EVERYTHING synchronous: `Lamp::setup()` once, then `Lamp::tick()` forever —
the compositor + expression render pipeline, every BLE/mesh drain, the OTA SHA
prefix hash at boot, and the web-server start. A single-threaded task, so its
true peak is the DEEPEST NESTED call chain in any one entry, not the sum of the
sequential drains (the stack unwinds between them).

Run this before changing `LAMP_LOOP_STACK_SIZE`, before/after the OTA SHA
buffer shrink, or after adding any new drain, expression, behavior, or
web-server work that runs on the loop task.

## Hardware

- **1 lamp on USB serial** — dev build for the runtime-path measurement
  (`LAMP_DEBUG` serial + the probe below are dev-only).
- For the boot-SHA path (signed builds only), a **key-holder's local signed
  build** (`LAMP_FIRMWARE_CHANNEL=beta`) with the temporary probe. Both
  variants: `upesy_wroom_standard` AND `upesy_wroom_snafu`.
- Optional: a **2nd powered lamp** (no serial) + the app, to drive real
  greeting / cascade / webui-join nesting instead of injected stimuli.

## Part 1 — worst-case chain (both variants)

Per-function frame sizes below are from a throwaway `-fstack-usage` build
(reproduce in the re-evaluation step). Numbers are bytes, gcc 14.2.0 /
platform-espressif32 55.03.39. **Standard and snafu produce identical frames
for every shared chain** — the only variant-specific frames are the render-leaf
behaviors (snafu `Greeting::tickStages` 192 B, `DotsBehavior::*` ≤ 80 B;
standard's expressions 32–80 B), all far below the drain and SHA frames. The
peak is driven by shared framework/firmware code, so the two variants converge
on the same dominant chain.

### Frame sizes that matter (both variants)

| Function | Frame (B) | Notes |
|---|---:|---|
| `FirmwareDistributor::computeShaPrefixOnce` | **4320** | 4096 B stack block buffer. Boot only, signed builds. Pending shrink to a 512 B block → ~**736 B**. |
| `Lamp::setup` | 528 | live under the whole boot chain |
| `drainTestAction` | 704 | 512 B `kPendingJsonOp` stack buf + locals |
| `drainExpressionOp` / `drainWifiOp` | 640 | " |
| `drainInboundOp` / `drainRemoteOp` / `drainWispOp` / `drainSocialDispositions` | 560 | " |
| `drainWispStatus` | 576 | |
| `discoverSignedImageLength` | 352 | 259 B scan window; boot only, signed |
| `verifySignedFirmware` | 304 | read buffer is HEAP (`unique_ptr`), off-stack |
| `webapp::begin` | 256 | calls into SPIFFS / AsyncWebServer / MDNS (prebuilt, see below) |
| `drainSettingsBlob` | 416 | 2048 B settings buf is `static`, OFF-stack |
| ArduinoJson `deserialize` entry | 144–160 | `JsonDocument` pool is HEAP |
| ArduinoJson `parseVariant` (recursive) | 64–96 / level | bounded by `NestingLimit` = 10 |
| `Lamp::tick` | 32 | drains are separate sequential frames, NOT summed |
| `Compositor::tick` | 48 | render leaf `control()`/`draw()` 32–80 B |

`esp_partition_read`, the mbedTLS SHA-256 transform, SPIFFS mount, and mDNS
init live in prebuilt IDF/arduino `.a` libs (no `.su`); their leaf frames are
NOT statically resolvable and are covered by measurement + margin, not
analysis.

### Candidate chains, deepest first

**A. Boot OTA SHA prefix (signed builds only) — CURRENT worst case.**
```
loopTask → Lamp::setup [528] → firmwareDistributor.begin [32]
         → computeShaPrefixOnce [4320] → mbedtls_sha256_update (prebuilt ~300)
```
≈ **5.4 KB** with the 4096 B buffer. The distributor self-disables on the
`-dev` channel (`firmware_distributor.cpp:64`), so this chain runs ONLY on
signed (`beta`/`stable`) builds, at boot, before the first `loop()`. With the
pending 512 B buffer it drops to ≈ **1.9 KB** and stops being the peak.

**B. Runtime drain + JSON parse (all builds).**
```
loopTask → Lamp::tick [32] → drainTestAction [704] (or drainExpressionOp [640])
         → deserializeJson [160] → parseVariant × ~4 [~384] → runExpressionOp / triggerInvocation → onTrigger → gradient (heap)
```
≈ **2.0 KB**. The `kPendingJsonOp` = 512 B stack buffer in each op-drain is the
floor; ArduinoJson's recursion is heap-backed for the pool and shallow for
real payloads (2–4 levels).

**C. Web-server start on the loop task.**
```
loopTask → Lamp::setup [528] (today) / Lamp::tick → webapp::tick [32] (post-redesign)
         → webapp::begin [256] → { SPIFFS.begin → SPIFFS_mount (prebuilt, deep) ;
                                    new AsyncWebServer [80] ; MDNS.begin → mdns init (prebuilt) }
```
≈ **2.6 KB** analytically, but the SPIFFS-mount + mdns-init prebuilt tail is
unquantified and has real appetite — **treat as MEASURE, not compute.** The
pending webui redesign moves `webapp::begin` from `setup()` into `webapp::tick()`;
either way it runs on `loopTask`, so the ceiling must cover it.

**D. Render pipeline / greeting / personality.** `Compositor::tick` → virtual
`control()`/`draw()` → color/gradient math. All frames 32–192 B; heap-backed
vectors. Never the peak. Listed to rule out.

### Peak summary

| Configuration | Peak chain | Estimate | Observable on |
|---|---|---:|---|
| Current (4096 B SHA buffer) | A (boot SHA) | ~5.4 KB | signed build only |
| Current, dev build | max(B, C) | ~2.6 KB | dev serial |
| After 512 B SHA shrink | C (webui start), then B | ~2.6 KB | dev serial |

**Finding that drives the plan:** with the 4096 B SHA buffer the true peak
(~5.4 KB) leaves little headroom under 8192, so shrinking the loop stack is not
worth it until the SHA buffer shrinks. After the 512 B shrink the peak falls to
the web-server-start path (~2.6 KB, measurable on a dev build), and the loop
stack can come down materially. **Land the SHA-buffer shrink first, then set
the ceiling against the shrunk build.**

## Step 0 — RE-EVALUATE before every run (mandatory)

Code evolves; a new expression, drain, or a widened stack buffer can introduce
a deeper chain and silently invalidate the table above. Before ANY measurement
run, re-derive the worst-case chain from the current tree:

1. Throwaway `-fstack-usage` build into an isolated build dir (does not touch
   tracked files or the shared `.pio`):
   ```sh
   cd software/lamp-os
   PLATFORMIO_BUILD_DIR=/tmp/su_lamp PLATFORMIO_BUILD_FLAGS="-fstack-usage" \
     LAMP_FIRMWARE_CHANNEL=dev pio run -e upesy_wroom_standard
   PLATFORMIO_BUILD_DIR=/tmp/su_snafu PLATFORMIO_BUILD_FLAGS="-fstack-usage" \
     LAMP_FIRMWARE_CHANNEL=dev pio run -e upesy_wroom_snafu
   ```
2. Rank frames, both variants:
   ```sh
   cat /tmp/su_lamp/upesy_wroom_standard/src/**/*.su | grep -vE 'std_function|_M_manager' \
     | awk -F'\t' '{print $2"\t"$1}' | sort -n | tail -25
   ```
3. Confirm the top frame is still `computeShaPrefixOnce` (or its shrunk
   successor) and that no NEW function on a `loopTask` entry (walk from
   `Lamp::tick` drains, `Compositor::tick`, `webapp::tick`, and the `setup()`
   OTA-init path) exceeds the drain frames. A prebuilt-lib chain (SPIFFS/mDNS/
   mbedTLS) can't be walked statically — it's covered by the measured HWM.
4. If a deeper chain appeared, update Part 1 and the ceiling BEFORE trusting the
   pass. A runbook whose documented chain no longer matches the code is a bug.

## Step 1 — SAFETY PREREQUISITE: stack canary (do before measuring)

An overflow during a shrink test must fail LOUD and NAMED, not corrupt an
adjacent allocation silently. The FreeRTOS canary prints
`***ERROR*** A stack overflow in task "loopTask" has been detected` and panics.

The prebuilt arduino-libs sdkconfig already ships both:
`CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=1` and
`CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=1`. This project regenerates the IDF
sdkconfig via `custom_sdkconfig`, so **pin them explicitly** for the
measurement so the guard can't depend on an unpinned default. Add to
`custom_sdkconfig` in `platformio.ini` (spec only — do NOT apply here;
`platformio.ini` is being edited elsewhere):

```
# Stack-overflow guard: fail loud + named instead of silent corruption.
CONFIG_FREERTOS_CHECK_STACKOVERFLOW_CANARY=y
# Exact-instruction trap on the overflowing write. Consumes one CPU debug
# watchpoint (fine on a USB-serial bench with no JTAG single-stepping).
CONFIG_FREERTOS_WATCHPOINT_END_OF_STACK=y
```

`WATCHPOINT_END_OF_STACK` traps at the exact PC that overruns the stack —
worth it for a shrink test because it names the offending frame, not just the
task. Leave it on for the whole measure-and-tighten cycle; drop to canary-only
for the shipped build if the watchpoint is needed elsewhere.

## Step 2 — MEASUREMENT

There is no working loop-task HWM readout today: the print at
`firmware_distributor.cpp:185` is `FWDIST_LOG`-gated (`LAMP_DEBUG` ≡ dev) AND
sits after the distributor's `-dev` early-return, so it never executes in any
shipping config. Add a temporary probe.

**Probe (temporary, measure-only — revert before commit).** `loopTask` is the
task that runs `Lamp::tick`, so `uxTaskGetStackHighWaterMark(nullptr)` read
from inside `tick()` reports the loop task's all-time minimum free stack. Add a
`stack.get` serial verb mirroring `heap.get`, or a periodic print in `tick()`:
```cpp
Serial.printf("[stack] loopTask minfree=%u bytes\n",
              (unsigned)uxTaskGetStackHighWaterMark(nullptr) * 4);
```
The HWM is a lifetime low-water mark that only ratchets down, so one late read
captures the deepest moment reached so far — including the boot-SHA peak, but
ONLY on a build where that path ran.

**Dev build (both variants) — runtime paths (B, C, D).** Flash dev, drive the
worst-case recipe below, read the probe. This misses chain A (distributor
disabled on dev).

**Signed build (key-holder, both variants) — the true all-time peak.** A local
`beta` build with the probe made UNCONDITIONAL (not `LAMP_DEBUG`-gated) is the
gold standard: one late HWM read captures chain A (boot SHA) AND the runtime
paths on the same task/stack. If a signed bench build isn't available, take the
boot-SHA contribution analytically from Part 1 (`computeShaPrefixOnce` frame +
`setup` chain) and combine: `task peak = max(analytic boot-SHA, measured dev
runtime)`.

**Worst-case recipe (drive all deep paths in one boot, then read once).**
Because the task is single-threaded and the HWM ratchets, the stimuli need NOT
overlap — walk them in sequence and the low-water mark records the deepest.
Snapshot first (`expr.get`), restore last, dim colors, per
[README.md](README.md).

1. Boot the lamp (captures chain A on signed, chain C if the server starts at
   boot). `--wait-ready '\[show\] ready'`, then `sleep:30` for roster settle.
2. Deepest render + cascade: fire a continuous expression at `target:3`
   (`{"a":"test_expression","type":"shifty","target":3}`; repeat for
   `breathing`) with a powered neighbor present so the cascade send path runs.
3. Op-drain + JSON depth: `expr.set` an upsert (chain B via `drainExpressionOp`)
   and a `test_expression` with a `colors` array (chain B via `drainTestAction`).
4. Settings-blob save: push a full settings blob over BLE/webui
   (`drainSettingsBlob` → `settingsBlobLocal` → `persistConfig`).
5. Web-server start: join the lamp's SoftAP / trigger the webui station-join so
   `webapp::begin` runs on the loop task (chain C). On the current tree this is
   a boot event; post-redesign, trigger whatever config toggle starts the
   server in `tick()`.
6. Greeting + personality: inject a peer + `triggerGreet`, let a full greet run.
7. Read the probe. Record `minfree` (peak usage = task stack size − minfree).

Include crash signatures in the log grep so a canary trip can't hide:
```sh
grep -E '\[stack\]|BOD|Guru|abort|stack overflow' /tmp/loop-stack.log
```

## Step 3 — CEILING selection

`LAMP_LOOP_STACK_SIZE` = worst measured peak-usage across BOTH variants + a
safety margin, applied via a weak `getArduinoLoopTaskStackSize()` override.

- **Margin ≥ 2 KB.** The worst-case chain can't be proven exhaustively: virtual
  dispatch (expressions/behaviors are polymorphic) defeats static resolution,
  drain ordering is combinatorial, and the SPIFFS/mDNS/mbedTLS leaves live in
  prebuilt libs. The margin covers that unmeasured tail plus the port's ISR
  frame that can nest on top of the deepest sync frame. 1.5 KB is the floor;
  2 KB is the recommendation.
- **After the 512 B SHA shrink:** measured peak ≈ 2.6 KB (confirm chain C
  empirically — it's the new driver) + 2 KB → **`LAMP_LOOP_STACK_SIZE = 5120`**
  (recovers ~3 KB RAM vs 8192; meaningful given the tight fragmented heap). If
  chain C measures higher than the analytic estimate, raise to `measured + 2048`
  rounded up to a KB.
- **Before the shrink (4096 B buffer):** peak ≈ 5.4 KB + 2 KB ≈ 7.4 KB — under
  8192 but not worth the change. Do the SHA shrink first.
- **Single default, not per-variant.** Standard and snafu share the dominant
  chain and differ by < 200 B at the render leaf, so one
  `LAMP_LOOP_STACK_SIZE` covers both. Only split into a per-variant override
  (in the variant header) if a future variant's measured peak diverges from the
  other by more than the chosen margin.

## Pass criteria

- Measured peak usage (both variants) + chosen margin ≤ `LAMP_LOOP_STACK_SIZE`.
- The canary is enabled and prints NO `stack overflow in task "loopTask"` line
  across the full worst-case recipe, on both variants, on the shipped
  configuration (signed build if that's what ships the SHA path).
- Zero `BOD` / `Guru` / `abort` across the run.
- Step 0 re-evaluation ran first and the documented chain still matches the
  current `-fstack-usage` output.

## Baseline log

Append one row per run. A green run meaningfully worse than the last green run
is a finding even though it passes.

| Date | Build | Variant | SHA buf | Measured minfree | Peak usage | Ceiling | Verdict |
|---|---|---|---:|---:|---:|---:|---|
| _tbd_ | | | | | | | analysis only — no hardware run yet |

Analytic baseline (this branch, `-fstack-usage`, no hardware): current peak
≈ 5.4 KB (signed, boot SHA) / ≈ 2.6 KB (dev runtime). Post-512-shrink peak
≈ 2.6 KB. Recommended ceiling post-shrink: 5120 B.

## Not covered here

- The prebuilt-lib leaf frames (SPIFFS mount, mDNS init, mbedTLS transform,
  esp_partition_read) — folded into the measured HWM + margin, not analyzed.
- Other tasks' stacks (NimBLE host 8192, WiFi, `fwdist` streaming task,
  async_tcp). This pass is `loopTask` only.
- The `fwdist` streaming task's own `streamOneChunk` (496 B) — runs on the
  distributor task, not `loopTask`.
- Compiler/toolchain frame drift: a platform-espressif32 bump can change frame
  sizes; re-run Step 0 after any platform upgrade.
</content>
</invoke>

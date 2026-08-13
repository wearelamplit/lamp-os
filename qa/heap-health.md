# Pass: heap health

Puts numbers on heap leaks and fragmentation: samples the allocator across
identical return-to-idle cycles and across a long soak. Run after changes to
allocation-heavy paths (expression manager, mesh roster, BLE control, config
JSON/persist), or whenever free-heap symptoms show up (alloc failures,
largest-block collapse, unexplained resets under load).

## Hardware

- **1 lamp on USB serial** (beta/dev build — `heap.get` is LAMP_DEBUG-gated).
- Optional: a **2nd powered lamp** (no serial) — adds real greet/roster churn
  to the cycles instead of only injected peers.

## Instrumentation

- **`heap.get` serial verb** — one parse-friendly line:
  `[cmd] ok heap free=<B> largest=<B> minever=<B>`
  where `free` = `esp_get_free_heap_size()`, `largest` = biggest contiguous
  8-bit-capable block, `minever` = boot-lifetime minimum free (ratchets down,
  never recovers).
- **`[heap] at=<tag> free=<B> largest=<B>`** lifecycle checkpoints already in
  the firmware, printed at: `boot`, `mesh`, `webapp`, `ble-connect`,
  `ble-disconnect`, `ota-stream`, `web-save`.
- A heap sample = send `heap.get` via `bench_cmd.py` (usage per
  [README.md](README.md)), grep the `[cmd] ok heap` line from the log.

## Methodology

- **Leak** — monotonic decline of `free` across identical return-to-idle
  cycles.
- **Fragmentation** — `free` flat while `largest` declines: the heap has the
  bytes but not in one piece.
- **`minever`** — high-water stress marker; how close the worst moment so far
  came to exhaustion.
- Baseline datum 2026-07-15, this branch: `free=19776 largest=11764` at a
  `ble-connect` checkpoint. The branch is SUSPECTED to have live
  fragmentation; this pass exists to surface it with numbers.

## Steps — quick mode (~15 min)

Snapshot first (`expr.get`), restore last, per README conventions.

Each cycle: sample → store a marker expression → trigger it → remove it →
inject 10 peers → clear them → fire a transient → sleep to reap → sample.
`bench_cmd.py` has no loop construct, so generate the repeated `--cmd` list
with a shell loop:

```sh
PORT=/dev/cu.usbserial-XXXX
PEERS=$(python3 -c 'import json;print(json.dumps({"a":"inject_nearby","peers":[{"name":f"p{i}","baseColor":"#10000000","disposition":3} for i in range(10)]},separators=(",",":")))')
CYCLE=(
  --cmd 'heap.get'
  --cmd 'expr.set {"op":"upsert","entry":{"type":"pulse","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"colors":["#00100000"],"pulseSpeed":3}}'
  --cmd '{"a":"test_expression","type":"pulse","target":3}'
  --cmd 'expr.set {"op":"remove","type":"pulse","target":3}'
  --cmd "$PEERS"
  --cmd '{"a":"clear_nearby"}'
  --cmd '{"a":"test_expression","type":"pulse","target":3,"colors":["#00100000"]}'
  --cmd 'sleep:12'
  --cmd 'heap.get'
)
ARGS=(); for i in {1..20}; do ARGS+=("${CYCLE[@]}"); done
python3 scripts/bench_cmd.py "$PORT" --wait-ready '\[show\] ready' \
  "${ARGS[@]}" -o /tmp/heap-quick.log --duration 600
```

(~14.5 s per cycle: 9 commands at bench_cmd's 0.25 s pacing + the 12 s
reap sleep. 20 cycles ≈ 5 min.)

Read the series, crash signatures included:

```sh
grep -E '\[cmd\] ok heap|\[heap\]|BOD|Guru|abort' /tmp/heap-quick.log
```

**Optional BLE-churn variant.** While the loop runs, drive host bleak
connect/disconnect cycles from a second terminal (trim the
[ble-app.md](ble-app.md) H1 snippet to connect → read `CHAR_SCHEMA_VERSION` →
disconnect, repeated). Each cycle exercises the NimBLE allocator and prints
the `ble-connect` / `ble-disconnect` checkpoints into the same log. Note the
lamp pauses its mesh scan while connected (greet suppression), so roster
churn from a 2nd lamp goes quiet during the connected windows — expected.

## Steps — soak mode (hours)

**One boot, no resets.** The whole point is accumulation, so let it run from a
single boot and treat any mid-run reboot as the lamp giving up, not a re-baseline.
The finding is the *shape* of `largest` over hours: a flat/noisy line = healthy;
a monotonic ratchet-down that never recovers = real fragmentation, and that's the
fleet-killer (a lamp at a multi-day event never reboots).

**Sample at every checkpoint, not just at the ends.** The point of a soak is to
see *where* the heap goes off, not just *that* it did. The firmware already stamps
`[heap] at=<tag> free= largest=` at seven lifecycle points; the drive cycle adds a
`heap.get` between each phase. Grep both after the run and read each tag's `largest`
as its own series — the tag whose slope trends down is the culprit path.

### What fragments this heap — exercise all of it

Every path below allocates on the shared heap; a soak that skips one can't clear it.
Roughly ordered by allocation pressure — the churny ones first. (OTA sits at the
bottom on purpose: it's low-risk, see the note under the table.)

| Path | Checkpoint / probe | What allocates | How to drive it |
|---|---|---|---|
| **BLE connect/disconnect** | `at=ble-connect` / `ble-disconnect` | NimBLE conn/GATT churn + section-read JSON (settings_blob, exprcat, page-data → `std::string`/`JsonDocument`) | host `bleak`: connect → read `CHAR_SCHEMA_VERSION` + a section page → disconnect, looped |
| **Config persist (NVS)** | `at=web-save`; expr.set drains | each write builds a `JsonDocument` + `std::string` then NVS write | `expr.set` upsert/remove; web config POST |
| **Web serve** | `at=webapp` | AsyncTCP per-conn ~5.7 KB *contiguous* buffers; `/api/settings` + `/api/expressions` String responses | `curl` the softAP GETs + a POST, looped |
| **Mesh message drains** | `at=mesh` | ~10 `JsonDocument` deserialize sites per inbound op (`lamp_drains.cpp`); `getWispStatusReadJson` `std::string`; `buildClaimsBlob` | inject peers + a live 2nd peer / wisp |
| **Roster growth to cap** | serial `heap.get` | snapshot vector + 17-char bdAddr strings (defeat SSO) at up to 50 peers | `inject_nearby` 50 peers then `clear_nearby` |
| **Expression create/destroy** | serial `heap.get` | `new`/`delete` of varying-size Expression objects | `expr.set` upsert/remove + `test_expression` |
| **Greeting / gradient** | serial `heap.get` | peer color query/info → gradient build | `triggerGreet` |
| **OTA (low risk)** | `at=ota-stream` | *sender allocates nothing big per offer.* Receiver's chunk `bitmap_` is ~1.2 KB and only on an accepted stream — which reboots on apply, so it can't accumulate. Never measured against the heap. | signed peer with a real same-variant target (**needs staging**) |

Serial verbs (`bench_cmd.py`) cover most of this unattended; BLE, web, and live
mesh need external drivers running *concurrently* against the same lamp. OTA is
listed for completeness, not because it's suspected — a dev bench lamp can't
*source* OTA (distributor self-disables on dev), so measuring it at all needs a
signed+`LAMP_DEBUG` build; skip it unless a heap symptom ever points there. Note in
the run which drivers were live; an unexercised path is untested, not passed.

### Drive cycle

Serial core, one `heap.get` per phase boundary so each phase's delta is visible:

```sh
CYCLE=(
  --cmd 'heap.get'                                                                    # cycle top
  --cmd "$PEERS10"        --cmd 'heap.get'                                            # roster fill
  --cmd 'expr.set {"op":"upsert","entry":{"type":"pulse","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"colors":["#00100000"],"pulseSpeed":3}}'
  --cmd '{"a":"test_expression","type":"pulse","target":3}'
  --cmd 'expr.set {"op":"remove","type":"pulse","target":3}'  --cmd 'heap.get'        # expr create/destroy + NVS
  --cmd '{"a":"triggerGreet"}'  --cmd 'heap.get'                                      # greeting/gradient
  --cmd "$PEERS50"  --cmd '{"a":"clear_nearby"}'  --cmd 'heap.get'                    # roster to cap + clear
  --cmd 'sleep:52'  --cmd 'heap.get'                                                  # reap idle
)
N=240   # cycles ≈ minutes; 240 ≈ 4 h
ARGS=(); for i in $(seq 1 $N); do ARGS+=("${CYCLE[@]}"); done
python3 scripts/bench_cmd.py "$PORT" --wait-ready '\[show\] ready' \
  "${ARGS[@]}" -o /tmp/heap-soak.log --duration $((N * 90 + 300))
```

`$PEERS10` / `$PEERS50` = `inject_nearby` payloads (10 / 50 peers, mixed
disposition) built as in quick mode. Run the BLE / web / OTA drivers from other
terminals against the same `$PORT` lamp for the same window.

### Read where it went off

```sh
grep '\[heap\] at=' /tmp/heap-soak.log        # per-tag lifecycle series
grep '\[cmd\] ok heap' /tmp/heap-soak.log     # per-phase serial samples
# one tag's largest over time — swap the tag to localize:
grep 'at=ota-stream' /tmp/heap-soak.log | grep -o 'largest=[0-9]*' | cut -d= -f2
```

A flat/noisy `largest` per tag = that path is clean. A monotonic ratchet-down that
never recovers on that tag = the fragmenter, and the tag names it.

## Pass criteria

**Settle before you read — this is not optional (it bit us 2026-07-16).**
Heap numbers are only meaningful at *steady state*. Two traps, both of which
produce confidently-wrong readings:
- **Boot is inflated.** A fresh boot shows the highest `largest` the lamp will
  ever have (~55–82 KB) before WiFi/BLE/mesh finish allocating. Never read
  pass/fail off boot or the first few cycles — **skip cycles 1–4 as warmup**
  and evaluate the settled value.
- **A long-uptime lamp can be deflated.** A lamp that's churned for a long time
  with no reboot can drift into a fragmented state below its fresh-boot steady
  state. For the **quick/regression** pass, start from a clean `--reset` boot so
  build-to-build numbers are comparable, and treat a mid-run reboot (a 2nd
  `[show] ready`) as an invalid run — the pre-reboot samples are a different
  lamp-state; re-run clean.
  - **Caveat — clean-boot is for comparability, not a clean bill of health.**
    Rebooting before every test hides the one failure that actually kills a
    field lamp: slow fragmentation that accumulates over days of real use and
    never self-recovers (no compaction on this heap). A lamp at an event runs
    for days without a reboot. That drift is what **soak mode** exists to catch
    — and soak deliberately does NOT reboot.

Thresholds — evaluate at **settled steady state**, not boot:
- **Fragmentation** — settled `largest`:
  - **≥ 24000 B = healthy** (target). A current-tip clean run holds ~40–43 KB
    under 10-peer churn.
  - **~15000–24000 B = watch** — degraded but functional.
  - **< ~15000 B = issue** — this is where the web config UI stops serving
    (AsyncTCP can't get its ~5.7 KB *contiguous* send buffers) and large
    allocations start failing. Confirm it's steady-state and clean-boot before
    calling it — a stale/pre-reboot sample here is the classic false alarm.
- **Leak** — skip cycles 1–4; `free` at the last cycle vs cycle 5 declines
  < 200 B per 10 cycles. Steady `free` = no leak.
- **Stress floor** — `minever` never below 12000 B.
- **Soak shape (per checkpoint)** — each `[heap] at=<tag>` series and each per-phase
  `heap.get` series is flat/noisy over the whole run, not a monotonic ratchet-down.
  A tag that trends down and never recovers fails the soak and names the culprit
  path — even if the run never crosses the 24 KB bar within the window.
- Zero `BOD` / `Guru` / `abort` across the run.

## Baseline log

Append one row per run; a green run that's meaningfully worse than the last
green run is a finding even though it passes.

| Date | Build | free@boot | largest@boot | largest@end | free@end | minever | Verdict |
|---|---|---|---|---|---|---|---|
| 2026-07-16 | release-1.1.1 tip `1732c1e5` (dev) | 85068 | 81908 | **40948** | ~49000 | ~44000 | ✅ **PASS** — clean `--reset` boot, 14-cycle 10-peer churn; `largest` holds 40–43 KB, no leak, single boot, 0 crashes |
| 2026-07-17 | release-1.1.1 tip (dev) — 19-cycle comprehensive soak | 60744 | 55284 | **38900** | 47752 | 41284 | ✅ **PASS** — checkpointed multi-path (mesh/expr/NVS/greet/roster); all 6 phase series flat in a 36.8–43 KB band, no per-phase drift, 1 boot, 0 crashes/errors. Per-checkpoint sampling localizes any leak within one cycle, so reps aren't needed. **OTA leg not covered** (dev bench can't source OTA). |

## Not covered here

- The 49.7-day `millis()` wrap.
- NVS wear — each cycle writes NVS twice via `expr.set`; the flash budget is
  a separate concern ([config-nvs.md](config-nvs.md) parks it too).
- PSRAM (none on the WROOM).
- Multi-day drift beyond the soak window.

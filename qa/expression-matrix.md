# Pass: expression matrix

Verifies every expression type triggers, times, and cascades correctly on
real hardware. Run after any change to `src/expressions/`, expression timing,
or the cascade path. Last full pass: 2026-07-15 (release-1.1.1 baseline).

## Hardware

- **1 lamp on USB serial** (beta/dev-channel build — LAMP_DEBUG required).
- **≥1 powered mesh lamp nearby, no serial needed** — gives the cascade rows a
  real peer (`peers=1`). Without it, expect `skip no-peers` instead of `sent`.
- **No wisp.** If a wisp is powered and painting, wisp-gated types
  (breathing/spotty/shifty) will suppress — power it off or expect rejects.

## Steps

1. Flash the build under test: `PORT=<port> npm run lamp:flash`.
2. Snapshot + set up (one session). Note `expr.get`'s output — it is the
   restore payload for step 6.

   ```sh
   python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
     --cmd 'expr.get' \
     --cmd 'expr.set [{"type":"glitchy","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"cascadeEnabled":1,"cascadeStaggerMs":0,"colors":["#10000000","#00100000"],"durationMin":200,"durationMax":400},{"type":"pulse","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"cascadeEnabled":0,"colors":["#00100000"],"pulseSpeed":3},{"type":"shifty","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#10101000"],"fadeDuration":5,"shiftDurationMin":20,"shiftDurationMax":20,"fillMode":0},{"type":"spotty","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"cascadeEnabled":1,"colors":["#10000000","#00100000"],"count":3,"size":3,"spotSpeed":3},{"type":"breathing","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#08000800"],"breathSpeed":5,"count":1,"scatter":0}]' \
     --cmd 'sleep:35' \
     --cmd '{"a":"test_expression","type":"glitchy","target":3}' \
     --cmd 'sleep:2' \
     --cmd '{"a":"test_expression","type":"glitchy","target":3}' \
     --cmd 'sleep:2' \
     --cmd '{"a":"test_expression","type":"spotty","target":2}' \
     --cmd '{"a":"test_expression","type":"pulse","target":3}' \
     --cmd '{"a":"test_expression","type":"breathing","target":2}' \
     --cmd 'sleep:2' \
     --cmd '{"a":"test_expression","type":"shifty","target":2}' \
     --cmd 'sleep:38' \
     -o /tmp/qa_exprmatrix.log --duration 150 2>&1 | \
     grep -E "\[cmd\]|\[trigger\]|\[cascade\]|\[shifty\]|\[expr\] fired|BOD|Guru|abort"
   ```

3. Payload-cap row — NOT bench-triggerable. The cascade command payload cap
   is `COMMAND_MAX_PAYLOAD = 1444` (`command.hpp`, the ESP-NOW v2 frame
   ceiling minus header + tag). A maxed 8-color zoned glitchy config packs to
   a few hundred bytes, so no slider config a user can build reaches 1444 B —
   the `[cascade] ERR payload …B over …B cap` guard cannot be exercised from
   the app or `expr.set`. Coverage for the cap lives in the native cascade
   dedup / command tests; skip this on hardware.

4. Spotty/breathing steady-state: in the step-2 log, count
   `[expr] fired type=spotty` / `type=breathing` lines. Expected: exactly one
   per enabled instance (target=3 configs fire twice — one per surface), all
   at enable/boot; ZERO additional fires afterward.
5. Shifty timing: from the step-2 log, subtract the `t=` values across
   `[shifty] fade-start → hold-start → fade-back-start → complete`.
6. Restore: `expr.set` the exact JSON captured in step 2, remove any test
   entries the snapshot didn't contain
   (`expr.set {"op":"remove","type":"spotty","target":2}` etc.), then
   `expr.get` and diff against the snapshot — must be identical.

## Pass criteria

- Every trigger ACKs `[cmd] ok` and logs `[trigger] '<type>' … fired=1`.
- Glitchy triggers (cascadeEnabled=1, roster filled): `[cascade] sent
  type=glitchy peers=<n≥1>`, on BOTH triggers (repeat taps must each cascade;
  only <250 ms repeats dedup).
- Spotty trigger: `[cascade] skip continuous`. Pulse (cascadeEnabled=0):
  `[cascade] skip not-enabled`.
- Shifty phases within ~1% of configured (5/20/5 s → expect ~5000/20000/5000
  ms), then `onComplete IDLE → triggerExpression(glitchy)`.
- Payload row: N/A on hardware — the 1444 B cap is unreachable by any slider
  config; the LOUD `[cascade] ERR payload …B over …B cap` drop is covered by
  the native tests.
- Zero `BOD`/`Guru`/`abort` lines after boot completes (one boot-inrush BOD
  on USB power is a known artifact, see README).

## Not covered here

Receiver-side cascade (old lamp → this lamp) needs the app driving the
second lamp's Test button; expectation matrix in `BENCH_BUGS.md`.

# Pass: expression interactions

Verifies overlapping expressions (and greeting behaviors) compose without
stalling, corrupting state, or breaking each other's timing. This pass proves
LIFECYCLE composition from serial; it cannot prove pixel-level compositing
(who painted over whom) — that lives in the native compositor interaction
tests. If a row here looks wrong visually but clean in serial, the bug is in
compositing and needs the native suite (or an `fb.get` probe, not yet built).

## Hardware

- **1 lamp on USB serial** (beta/dev build).
- **1 additional powered lamp on the mesh, no serial** — required only for the
  real-greet row (G3). Its `lampId` (mesh-mac) is needed for G2; get it
  from the app's fleet screen, or skip G2 and rely on G3.
- **Wisp**: powered + painting ONLY for row G4; power it off for everything
  else (it suppresses the wisp-gated types and invalidates the other rows).
- The lamp must NOT have an app BLE-connected during greet rows (greeting
  scan pauses under app connections — greets won't fire; this is by design).

## Setup

Snapshot config (`expr.get` — keep the output for restore), then apply the
test set. Shifty's 20 s fade + 20 s hold is the "slow state" everything else
fires over; spotty + breathing run continuously underneath throughout.

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd 'expr.get' \
  --cmd 'expr.set [{"type":"glitchy","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"cascadeEnabled":0,"colors":["#10000000"],"durationMin":300,"durationMax":300},{"type":"pulse","enabled":true,"intervalMin":600,"intervalMax":900,"target":3,"cascadeEnabled":0,"colors":["#00100000"],"pulseSpeed":3},{"type":"shifty","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#10101000"],"fadeDuration":20,"shiftDurationMin":20,"shiftDurationMax":20,"fillMode":0},{"type":"spotty","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#10000000","#00100000"],"count":3,"size":3,"spotSpeed":3},{"type":"breathing","enabled":true,"intervalMin":600,"intervalMax":900,"target":2,"colors":["#08000800"],"breathSpeed":5,"count":1,"scatter":0}]' \
  --cmd 'sleep:3' \
  -o /tmp/qa_ix_setup.log --duration 30 2>&1 | grep -E "\[cmd\]"
```

## Rows

**X1 — one-shots over a slow fade.** Start shifty, fire glitchy mid-fade,
pulse mid-fade, glitchy again mid-hold, glitchy+pulse same tick mid-hold:

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd 'sleep:5' \
  --cmd '{"a":"test_expression","type":"shifty","target":2}' \
  --cmd 'sleep:5' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3}' \
  --cmd 'sleep:5' \
  --cmd '{"a":"test_expression","type":"pulse","target":3}' \
  --cmd 'sleep:12' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3}' \
  --cmd 'sleep:5' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3}' \
  --cmd '{"a":"test_expression","type":"pulse","target":3}' \
  --cmd 'sleep:25' \
  -o /tmp/qa_ix_x1.log --duration 120 2>&1 | \
  grep -E "\[cmd\] ok|\[trigger\]|\[shifty\]|\[expr\]|BOD|Guru|abort"
```

**X2 — transients over live continuous.** With spotty+breathing running,
inject transient glitchy and a transient breathing (colors ⇒ the
`triggerInvocation` mesh-receive path):

```sh
python3 scripts/bench_cmd.py <port> --wait-ready '\[show\] ready' \
  --cmd 'sleep:5' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3,"colors":["#10000000"]}' \
  --cmd 'sleep:3' \
  --cmd '{"a":"test_expression","type":"breathing","target":2,"colors":["#10001000"]}' \
  --cmd 'sleep:15' \
  -o /tmp/qa_ix_x2.log --duration 60 2>&1 | \
  grep -E "\[cmd\] ok|\[expr\] transient|BOD|Guru|abort"
```

**G1 — injected-roster greet context.** `inject_nearby` shapes the
personality engine's view (dispositions), then fire the chain rows again to
confirm greet-adjacent state doesn't perturb X1/X2 results:

```sh
  --cmd '{"a":"inject_nearby","peers":[{"name":"qa-peer","baseColor":"#10000000","disposition":5}]}' \
  # …repeat an X1-style shifty+glitchy overlap here… \
  --cmd '{"a":"clear_nearby"}'
```

**G2 — forced greet over a live fade** (needs the neighbor's lampId):

```sh
  --cmd '{"a":"test_expression","type":"shifty","target":2}' \
  --cmd 'sleep:5' \
  --cmd '{"a":"triggerGreet","lampId":"<peer-lampid>"}' \
  --cmd 'sleep:20'
```

**G3 — natural greet (manual step).** With shifty mid-hold and spotty
running, power-cycle the second lamp; when it reappears the lamp under test
should greet it. Requires no app BLE connection. Watch for the greet lines
interleaving with `[shifty]` phases arriving on schedule.

**G4 — greet during a wisp hold (optional).** Power the wisp, wait for
`OVERRIDE_COLORS`, repeat G2/G3. Expected per current design: wisp-gated
painting suppressed; confirm the greet either defers or paints per the
documented behavior in `docs/dev/expressions.md` — a mismatch is a finding.

## Restore

`expr.set` the snapshot JSON, remove test-only entries
(`{"op":"remove","type":…,"target":…}`), `expr.get`, diff — identical.

## Pass criteria (all rows)

- Every ACK `[cmd] ok`; every one-shot logs `fired=1`; transients
  created AND `reaped … (complete)` — nothing left for the 180 s GC.
- **Shifty's phase timestamps stay on schedule through every overlap** —
  `fade-start → hold-start` ≈ fadeDuration, `hold-start → fade-back-start` ≈
  hold, regardless of what fired in between. Drift or a missing phase line =
  the overlap broke the state machine.
- Spotty/breathing log ZERO extra `[expr] fired` lines during any overlap
  (no spurious retriggers).
- The shifty `onComplete → triggerExpression(glitchy)` chain still fires at
  the end of X1's cycle.
- Zero `BOD|Guru|abort` after boot.
- Visual (human, optional but valuable): no frozen frames, no backward jumps
  in the fade after a glitch ends, spots/breath resume seamlessly. A visual
  anomaly with clean serial = compositing bug → native suite territory.

# Pass: wisp-hold matrix

Verifies expression/transient behavior while a wisp actively holds paint on
the lamp. Run after changes to the wisp-override gate, transient lifecycle,
or `ExpressionManager::triggerInvocation`. Last full pass: 2026-07-15.

## Hardware

- **1 lamp on USB serial** (beta/dev build).
- **Wisp powered, on the mesh, and actively painting** (e.g. manual palette /
  drift). The wisp does NOT need serial — the pass gates on the lamp's own
  `[recv] OVERRIDE_COLORS` line. Put the wisp on the Mac's USB only if its
  side needs debugging (`scripts/bench_tap.py <wisp-port>:wisp`).
- Firmware pairing: flash lamp AND wisp from the same tree
  (`PLATFORMIO_UPLOAD_PORT=<wisp-port> npm run wisp:flash`) — a wisp on a
  different protocol lineage can silently not paint at all.

## Key facts the timing depends on

- A lamp reboot (any bench_cmd port open) drops the hold; the wisp
  re-establishes it up to ~2 min later. Gate sends on the paint actually
  arriving, not on wall time.
- The hold PERSISTS between wisp repaints (verified: transient still rejected
  112 s after the last OVERRIDE_COLORS).
- Sending a `test_expression` WITH a `colors` array goes through
  `triggerInvocation` — i.e. it creates a TRANSIENT exactly like a mesh
  cascade arrival. That is the lever this whole pass uses.
- Wisp serial shows `[mesh.send] FAIL to <lamp>` whenever the lamp is
  mid-reboot. Ignore those on the bench.

## Steps

One session; `--wait-ready` holds the sends until the wisp paints:

```sh
python3 scripts/bench_cmd.py <port> \
  --wait-ready 'OVERRIDE_COLORS surface' --wait-ready-timeout 180 \
  --cmd '{"a":"test_expression","type":"breathing","target":2,"colors":["#10001000"]}' \
  --cmd 'sleep:2' \
  --cmd '{"a":"test_expression","type":"breathing","target":2,"colors":["#10001000"]}' \
  --cmd 'sleep:2' \
  --cmd '{"a":"test_expression","type":"glitchy","target":3,"colors":["#10000000"]}' \
  --cmd 'sleep:8' \
  -o /tmp/qa_wisphold.log --duration 210 2>&1 | \
  grep -E "\[cmd\] ok|\[expr\] transient|OVERRIDE_COLORS|\[cascade\]|BOD|Guru|abort"
```

No config mutation happens (transients only), so no snapshot/restore needed.

## Pass criteria

- Both breathing (wisp-gated type) transients:
  `[expr] transient rejected type=breathing (never started)` — the SECOND one
  too. A retained first transient that coalesce-blocks the second for 180 s is
  the historical failure mode this pass exists to catch.
- Glitchy (gate=false) transient during the same hold:
  `transient created` ×2 (target=3 → one per surface) followed within ~1 s by
  `transient reaped type=glitchy (complete)` ×2 — coexists with the hold and
  is GC'd promptly.
- No crash signatures.

## Known-untested corner

A hold STARTING mid-cycle of a live transient (wisp's first paint racing a
~10 s breathing transient after a lamp boot). Covered natively by
`test_transient_lifetime`; staging the hardware race isn't worth it.

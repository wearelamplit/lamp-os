# QA bench runbooks

Hardware-in-the-loop test passes for lamp firmware, driven over USB serial.
Each `.md` here is a self-contained runbook: what to flash, what to send, what
the serial output must show. Written to be executed by Claude or a person.

## Tooling

All passes drive lamps through the LAMP_DEBUG serial command ingress (present
on beta/dev-channel builds only; stable firmware ignores serial input) using
two scripts:

- **`scripts/bench_cmd.py`** — send commands + capture output (read/write,
  exclusive port access):

  ```sh
  python3 scripts/bench_cmd.py PORT \
    --wait-ready '\[show\] ready' \        # hold sends until the lamp boots
    --cmd 'expr.get' \                     # commands, sent in order
    --cmd 'sleep:15' \                     # host-side pause pseudo-command
    -o /tmp/run.log --duration 60          # exit after N s (or --until REGEX)
  ```

  `--reset` pulses EN for a deterministic reboot. `--wait-ready` can gate on
  any line (e.g. `'controllingBase=1'` or `'OVERRIDE_COLORS surface'` to wait
  for a wisp hold).

- **`scripts/bench_tap.py`** — read-only multi-lamp tail for observation runs.
  Never run it on a port bench_cmd needs — stop it first.

### Serial command grammar (ACKed `[cmd] ok …` / `[cmd] err <reason>`)

| Command | Effect |
|---|---|
| `{"a":"test_expression","type":T,"target":N}` | trigger stored expression (app Test path) |
| `{"a":"test_expression","type":T,"target":N,"colors":[…]}` | inject a TRANSIENT via the mesh-receive path (`triggerInvocation`) |
| `{"a":"triggerGreet", …}` / `inject_nearby` / `clear_nearby` | greeting + roster verbs (see lamp_test_action.cpp for shapes) |
| `expr.get` | print stored expressions section JSON |
| `expr.set <section-array or op-object>` | apply expression config (CHAR_EXPRESSION_OP path); `{"op":"remove","type":T,"target":N}` removes |

## Conventions every pass follows

1. **Snapshot first, restore last.** `expr.get` before mutating a lamp's
   config; end the pass by re-applying the captured JSON and `expr.get` to
   verify byte-identical restore.
2. **Dim test colors** (`#10000000`-scale, W=00). USB-powered lamps brown out
   on boot inrush and on bright bursts; a BOD reset mid-pass reads as "the
   lamp ignored me". PSU power if available.
3. **Port open usually reboots the lamp.** Always `--wait-ready`; after a
   reboot the mesh roster is EMPTY for 2–25 s — any cascade test needs a
   `sleep:30`+ before triggering, or its sends drop as `skip no-peers`.
4. **Long intervals on test configs** (`intervalMin/Max` 600–900) so
   auto-triggers don't pollute the log; drive everything manually.
5. **Serial RX wedge**: if commands echo (`>>`) but no `[cmd]` ACKs appear,
   the CP2102 host side is wedged (appears after long-lived read sessions;
   survives lamp resets and esptool `chip_id`). Recovery: one
   `PORT=<port> npm run lamp:flash` cycle, then re-run.
6. Filter logs with a grep like
   `grep -E "\[cmd\]|\[trigger\]|\[cascade\]|\[expr\]|\[shifty\]|BOD|Guru|abort"` —
   and ALWAYS include crash signatures (`BOD|Guru|abort`) so a reset can't
   hide in the noise.

## Passes

| Runbook | Covers | Hardware |
|---|---|---|
| [expression-matrix.md](expression-matrix.md) | per-type triggers, shifty wall-clock timing, spotty/breathing single-fire, cascade permutations, payload cap | 1 lamp on serial; ≥1 powered mesh neighbor (no serial) for the cascade-send rows; no wisp |
| [wisp-hold-matrix.md](wisp-hold-matrix.md) | transient behavior under an active wisp paint hold | 1 lamp on serial + wisp powered and painting (wisp serial optional, only for debugging its side) |
| [expression-interactions.md](expression-interactions.md) | overlapping expressions + greeting behaviors composing correctly | 1 lamp on serial; a 2nd powered lamp (no serial) for the real-greet rows; wisp powered only for the greet-during-hold row |
| [ota.md](ota.md) | signed mesh OTA end-to-end, unsigned-never-offers, {type}-{channel} gating, quiet-mode indicator, NVS survival | 2 lamps on serial (PSU preferred); signed artifact (published beta release, or local `:signed` build with the key) |
| [ble-app.md](ble-app.md) | GATT contract, host plaintext rows (bleak), password/auth flip, app round-trips, force-stop reconnect, greet suppression | 1 lamp on serial + phone with the app; Mac BT for host rows; 2nd powered lamp for the suppression row |
| [wisp-health.md](wisp-health.md) | DIAGNOSTIC: "wisp vanished / isn't painting" triage — the four known causes in check order + benign noise | wisp on serial; ≥1 lamp on serial for link confirmation |
| [power-brownout.md](power-brownout.md) | brownout stimuli (boot inrush, full-white hold, expression burst, OTA, BLE-connect, wisp paint) × power sources | 1 lamp on serial + human swapping USB2/USB3/battery/PSU; 2nd lamp for the OTA row; wisp for its row |
| [config-nvs.md](config-nvs.md) | config load/clamp/persist across reboot + re-flash, name/password propagation, factory reset | 1 lamp on serial + host BLE (bleak) or phone; wisp optional for the name-in-HELLO check |
| [personality-matrix.md](personality-matrix.md) | injected-crowd dim, disposition extremes on the greet profile, triggerGreet, cooldown/no-respam, greet-gradient verify | 1 lamp on serial + 1 real powered peer lamp (bdAddr from the app); no wisp |
| [heap-health.md](heap-health.md) | heap leak/fragmentation detection: `heap.get` sampling across scripted return-to-idle cycles, quick + soak modes, minever high-water | 1 lamp on serial; optional 2nd powered lamp for greet/roster churn realism |
| [loop-stack.md](loop-stack.md) | `loopTask` peak stack measurement before shrinking `LAMP_LOOP_STACK_SIZE`: `-fstack-usage` chain re-derivation, canary prereq, HWM probe, worst-case recipe, ceiling + margin | 1 lamp on serial (dev, runtime paths); key-holder signed build for the boot-SHA path; both variants |

Each runbook opens with its own Hardware section repeating this. Findings go
to `BENCH_BUGS.md` at the repo root.

# Pass: multi-wisp

Verifies two wisps sharing a venue: a lamp moves to the wisp that can still
hear it, and app ops to one wisp survive the other wisp's status traffic. Run
after changes to `WispRoster` claims, `LampInventory` RSSI handling, or the
wisp's CONTROL_OP filter (`controlOpForWisp`).

## Hardware

- **Two wisps, A and B**, both flashed from this tree
  (`PLATFORMIO_UPLOAD_PORT=<wisp-port> npm run wisp:flash`, a dev build, so
  the 10 s `[wisp] roster` dump is on), both on USB serial.
- **One lamp, L, on USB serial** (dev build) for `[loop] drain wispState`,
  plus **at least one more powered lamp** between the two wisps to relay L's
  HELLO to A once L is out of A's range.
- Phone with the app, paired to L.
- Give A and B visibly different manual palettes so the lamp shows which wisp
  paints it.
- Tap A and L once for the whole pass (opening L's port reboots it; the C6
  wisps are immune): `npm run lamp:tap -- <wisp-A-port>:wispA <lamp-port>:L -o /tmp/multiwisp.log`.
  Leave B's port free for `bench_cmd.py` in row 3; for rows 1 and 2 tap it
  separately or skip it.

## Key facts the timing depends on

- Lamps HELLO every 30 s. A wisp forgets a lamp's direct RSSI 90 s after the
  last HELLO it heard directly, at its next 30 s prune, so 90 to 120 s.
- The roster dump prints `rssi=-128` once a lamp has no current direct
  RSSI; `age` keeps resetting while relayed HELLOs arrive.
- CLAIM goes out every ~4 s and STATE every ~2 s; a lamp eases home 60 s
  after its display wisp's last STATE.

## Steps

| # | Check | Do | Pass |
|---|---|---|---|
| 1 | A lamp that leaves A's range moves to B | Put L beside A, B across the room where it still hears L. Wait for A's dump to show L with a real `rssi=` and L painting A's palette. Carry L out of A's radio range, still in B's and in range of the relaying lamp. | Within about 2 min A's dump shows L with `rssi=-128` while `age` stays under ~30 s, and L paints B's palette (`[loop] drain wispState base=` lines resume with B's colours). It must not stay on `not painted (ease home)` past ~3 min. |
| 2 | One wisp keeps a present lamp | Power B off. L beside A for 5 min. | A's dump keeps a real `rssi=` for L and L paints A's palette throughout, no ease-home lines. |
| 3 | App ops to A survive B's status bursts | Both wisps on, L's display wisp is A. Hold B in its 20 s status burst with `src:manual` every 15 s: `python3 scripts/bench_cmd.py <wisp-B-port> $(for i in $(seq 24); do printf -- "--cmd src:manual --cmd sleep:15 "; done) -o /tmp/wispB.log`. In the app, toggle A's source Off / Manual 20 times, waiting for each confirm. | A's console prints a `[wisp.op] setSource mode=` line for every toggle and the app confirms all 20; no "couldn't reach the wisp". |

Restore A and B to their original source and palette afterwards. The loss
row 3 guards is rare (an estimated 1 op in 100 during a burst, not measured),
so a clean run is a regression check rather than proof.

## Pass criteria

- Row 1: the handoff completes within ~3 min and L ends painted by B.
- Row 2: no ease-home and no `rssi=-128` for L while B is off.
- Row 3: 20 of 20 ops applied and confirmed.
- No crash signatures (`BOD|Guru|abort`) on any port.

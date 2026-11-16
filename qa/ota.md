# Pass: mesh OTA

Verifies signed lamp→lamp firmware distribution end-to-end on real hardware.
OTA has regressed three separate times (cross-sector erase in the chunk-write
path, pipelined pre-erase, the 152-byte offer-auth frame) — this pass exists
to catch that class. Run after ANY change to `components/firmware/`
(distributor, receiver, ota_channel, signature, quiet mode, indicator) or the
mesh transport under it.

## Hardware

- **2 lamps on USB serial** (source + peer), both standard variant. PSU power
  strongly preferred: OTA is flash-writes + radio TX concurrently, a USB BOD
  mid-transfer aborts the pass (see [power-brownout.md](power-brownout.md)).
- **A signed artifact.** Local dev builds are unsigned and CANNOT source OTA
  (the distributor self-disables on the dev channel). Two sources of signed
  bits:
  - the published GitHub release (`beta` republishes on every `dev` push) via
    `npm run lamp:flash:release`;
  - a local signed build — no npm task exists; the key-holder runs pio directly
    (`cd software/lamp-os && LAMP_FIRMWARE_CHANNEL=beta PLATFORMIO_UPLOAD_PORT=<port> pio run -e upesy_wroom_standard -t upload`) —
    signing is automatic on a non-dev channel and requires both the firmware
    key at `~/.lamp-os-firmware-key.bin` and the command key
    (`LAMP_COMMAND_KEY_HEX` or `~/.lamp-os-command-key.hex`).
- No human steps beyond cabling; everything below drives over serial.

## Setup: version skew

The distributor only offers to a peer whose HELLO reports a LOWER version on
an acceptable `{type}-{channel}` slot (`ota_channel.cpp`). Manufacture the
skew:

1. Peer ← current published beta:
   `PORT=<peer-port> npm run lamp:flash:release`
   Note its version from the boot line `[fwdist] online; v=0x...`.
2. Source ← locally signed build at a higher version: bump the patch digit in
   the root `VERSION` file, then
   the raw pio invocation above with the bumped VERSION in place.
   Revert afterwards (`git checkout -- VERSION`); do not commit the bump.

## Steps

1. Tap both lamps and filter through the OTA monitor:

   ```sh
   python3 scripts/bench_tap.py <src-port>:src <peer-port>:peer -o /tmp/qa_ota.log &
   python3 scripts/ota_monitor.py -f /tmp/qa_ota.log --summary
   ```

   Leave both ports alone once the transfer starts — opening a port reboots
   the lamp and aborts the OTA.

2. **Signed lamp→lamp OTA end-to-end.** Wait for the lamps to exchange HELLOs
   (roster fills 2–25 s after boot), then watch the chain:

   - src: `[fwdist] consider <peer-mac> v=... our=... → OFFER`, then
     `[fwdist] OFFER -> <peer-mac> v=... chunks=... seq=...`
   - peer: `[fw_receiver] OFFER from <src-mac> v=... totalLen=... chunks=... → ACCEPT`,
     `[fw_receiver] upfront erase: N sectors in N ms`
   - src: `[fwdist] stream progress: sent N/N chunks` (every 256 chunks);
     peer: `[fw_receiver] recv progress: N chunks received`
   - src: `[fwdist] DONE -> <peer-mac> (attempt 1/…, waiting RESULT)`
   - peer: signature verify passes (a `signature verify FAILED` line is an
     instant fail), then a reboot into the new version, `[ota] booted in
     PENDING_VERIFY, 30s self-health check armed` at the end of its setup and
     `[ota] new partition marked valid (post-OTA boot)` about 30 s later (see
     [embedded-heap.md](../docs/dev/embedded-heap.md#rollback-after-an-update)).
     Leave the peer's port alone until that line: a reset before it, the port
     open's included, rolls the peer back. No `[ota]` line at all means the
     peer's bootloader predates rollback; note it, the rollback rows below
     cannot run on it.
   - src: `[fwdist] RESULT success from <peer-mac>`
   - monitor: `== VERSION <peer-mac>: <old> → <new>` (from the peer's
     post-reboot HELLOs on the src tap).

3. **Quiet-mode indicator during transfer (firmware OTA).** While chunks stream,
   the peer logs `[ota_ind] t=... rxInProgress=1 rxTotal=... done=... total=...`
   and `paused adv + scan`; visually both strips show the OTA progress bar
   (sender's base color advancing over the receiver's dimmed base), and the base
   surface settles to its configured colors then subtly pulses (≤20%) toward the
   peer color. After the flow: `resumed adv + scan`. FS-image OTA runs this same
   path but SILENT — see Indicator expectations. With the app connected to
   either lamp when the session starts, that lamp logs `kicked GATT client`
   and `Client disconnected`, then no `Client connected` that stays up until
   after `resumed adv + scan after OTA`, and no `Advertising already active` after
   the resume. Once resumed, the app reconnects to it (on the sender, which
   resumes without a reboot, this proves the resume finds advertising off and
   starts it). A phone link that completes under the pause logs `kicked GATT
   client handle=N: OTA radio pause`, then `Client connected` and `Client
   disconnected`; that is a pass.
   A `Host reset; reason=` line during the transfer must not be followed by a
   `Client connected` that stays up before the resume, nor by `Advertising
   already active` at it. The bench cannot trigger a host reset on demand, so
   this holds only when one happens; it is the one check on the
   `enforcePause` call in `ble_control::tick` and on `NimBleRadio::isAdvertising`
   reading true.
   Web config softAP (set the src lamp's Web Config window to never expire, or
   start inside its boot window): the src logs `[webapp] softAP down for OTA`
   as it starts streaming, and `[webapp] softAP back after OTA` about 10 s after
   `RESULT success` (the inter-session hold), and its `<name>-lamp` network is in
   a phone's Wi-Fi list again. Repeat with a phone joined to the src's network
   and the web config page open: the src logs no `[fwdist] ... → OFFER` while
   the phone stays joined, and a settings save from the page answers. Leave the
   network: the src offers within a few seconds. A
   `softAP not restored after OTA` line is a fail unless a heap shortage shows
   beside it. Note the `[heap]` largest block before the send and after the
   restore.

4. **NVS/config survives.** Before step 2, note the peer's
   `[cfg] loaded name=...` and `[cfg] expressions=...` boot lines. After the
   post-OTA reboot the same name/expression count must load. `expr.get` over
   serial for a byte-level check if the peer had expressions.

5. **Dev build never offers.** Flash the source with `PORT=<src-port> npm run
   lamp:flash` (always dev channel: unsigned, `LAMP_DEBUG` on). Boot must log
   `[fwdist] disabled (dev channel)` — the dev-channel gate fires before the
   signature check, so that is the string a stock dev build hits — and emit
   ZERO `[fwdist] OFFER` lines over ≥5 min with the older peer visible in
   HELLOs.

6. **Channel/type gate ({type}-{channel} LSIG slot).** Re-flash the peer to
   stable: `RELEASE_TAG=<stable version tag, e.g. v1.3.0> PORT=<peer-port> npm run lamp:flash:release`,
   source back to the signed higher-version beta build. In `otaAcceptable()`
   only a beta lamp accepts a stable offer (a beta lamp promotes UP to
   stable); a stable lamp never accepts a beta offer. Here the peer is stable
   and the source is beta, so the source must log
   `[fwdist] consider <peer-mac> skip: ...` and never OFFER; if an offer ever
   reaches the receiver anyway, the peer logs
   `[fw_receiver] OFFER v=... ch=standard-beta declined` (the drop is logged,
   not silent). Cross-VARIANT gating (`standard` vs `snafu` type prefix) is
   the same code path with a different prefix; exercising it for real needs a
   snafu lamp + snafu-signed artifact — see Not covered.

7. Restore: flash both lamps back to the intended baseline, revert `VERSION`.

## Rollback

An updated lamp stays unverified for its first ~30 s of loop, and any reset
in that window boots its previous firmware. These rows need serial commands
on the peer, so both images are signed debug builds of this firmware
(`DEBUG=1 ... scripts/build_signed_lamp.sh`, see Signed local builds): peer
at version N, source at N+1. The peer runs on `bench_cmd.py` in place of the
tap, opened before the transfer so the open's reset lands on the old image.

8. **A hang in the first 30 s rolls back.** On the peer:

   ```sh
   scripts/bench_cmd.py <peer-port> --wait-ready '\[ota\] booted in PENDING_VERIFY' \
       --wait-ready-timeout 900 --cmd 'loopwd.hang' --duration 1200 -o /tmp/qa_rollback.log
   ```

   Run step 2's transfer. Expect, on the peer: `[ota] booted in
   PENDING_VERIFY`, `[cmd] ok loopwd.hang`, a `Task watchdog got triggered`
   panic within about 5 s, and a boot of version N (its `[fwdist] online;
   v=0x...` line) with `[boots] -1 fw<N+1> ... end taskwdt rolledback`. No
   `marked valid` line before the reset. Later the source offers N+1 again
   and the peer installs it and logs `marked valid`; that second pass is the
   row's re-check that a rolled-back lamp takes the update again.

9. **A marked image survives a reset.** After row 8's second pass logs
   `[ota] new partition marked valid`, run `scripts/bench_cmd.py <peer-port>
   --cmd 'loopwd.hang' --duration 30 -o /tmp/qa_rollback.log`. The peer resets
   and boots N+1 again, and its `[boots] -1 fw<N+1> ... end taskwdt` line has
   no `rolledback`. A power cycle in place of the hang reads the same with
   `end power`.

10. **No offer is taken or sent while unverified.** Power two more lamps in
    range: one on N+2, one on N (it needs N+1 too). Repeat row 8 without the
    hang (`--cmd` left out). Any OFFER reaching the peer before its `marked
    valid` line logs `[fw_receiver] OFFER while this image is unverified,
    declining`, and no `ACCEPT` or `upfront erase` follows before that line.
    The peer logs no `[fwdist] OFFER` to the N lamp before that line either;
    after it, the peer may offer N+1 to it (the source may get there first).
    The N+2 lamp may take up to its 10-minute per-peer backoff to offer
    again. Filesystem offers show up in either direction only when the lamps'
    web UIs differ, so this row checks firmware offers only.

11. **The web app in the window keeps the update.** Repeat row 8 without the
    hang. Right after `[ota] booted in PENDING_VERIFY`, join the peer's Wi-Fi
    from a phone, open the config page, and stay on it. Expect `[ble_control]
    GATT control service stopped`, then `[ota] new partition marked valid`
    about 30 s after the PENDING_VERIFY line, and no `BLE never started`.

## Indicator expectations

- **Firmware OTA** — a DIM full-shade fill (~16% physical, own base color when
  the roster is cold) + a peer-color progress band, plus the base surface
  settling to its configured colors and subtly pulsing (≤20%) toward the peer
  color. Reads as "the lamp dimmed with a calm base", not an obvious bar.
  `[ota_ind] done=/total=` serial lines are the reliable signal. (Dim + band
  verified 2026-07-15; base settle/pulse 2026-07-17, bench-pending.)
- **FS-image OTA is visually SILENT** — no dim, no progress band, no pulse: the
  base surface settles to its configured colors and holds, and the coex radio
  pause still applies. A silent FS catch-up is CORRECT, not a miss; judge it by
  `[fs_ota]`/chunk lines, never the strip. A ~0.5–1 s render freeze during the
  receiver's up-front SPIFFS erase is expected and lands on the settled base.
  (2026-07-17, bench-pending.)
- The compositor `(ota=…)` probe and `[fwdist] hb` lines are UNRELIABLE
  during OTA: two engine pairs (firmware + FS image) share log tags and a
  heartbeat static, and every probe consults only the firmware pair. An FS
  session shows quiet=0/ota=0 by (flawed, pre-existing) design. Judge the
  transfer by chunk/progress/[ota_ind] lines only.
- Channel isolation: run the whole pass on a scratch CHANNEL (e.g. qa) so
  powered fleet lamps on beta can never accept the test offer.

## Pass criteria

- The src's web config network back after the send (step 3), or the src
  rebooted with BLE back when a page was open.
- Full step-2 chain present on the taps, ending in `RESULT success` and a
  `== VERSION` transition; exactly one transfer (no OFFER/DONE thrash loop).
- Firmware OTA: `[ota_ind]` lines + visible progress bar + base settle/pulse
  during transfer; adv/scan pause + resume lines bracket it. FS-image OTA: NO
  bar/dim/pulse (silent by design) — a calm base settle is the pass, judged by
  `[fs_ota]`/chunk lines.
- Peer's name/config identical across the OTA reboot.
- Rollback (rows 8-11): a hang before `marked valid` boots version N with
  `rolledback` on the N+1 line, a reset after it keeps N+1 with no
  `rolledback`, no offer is accepted or firmware offer sent before `marked
  valid`, and a phone on the web app in the window does not stop the mark.
- Dev source: the `disabled (dev channel)` line, zero OFFERs.
- Gated pair (step 6): zero OFFERs from the source; any receiver-side decline
  is logged, never a silent flash.
- Zero `BOD|Guru|abort` after boot on either lamp (one boot-inrush BOD on USB
  power is the known artifact).

## Not covered here

- The BLE chunk path dropping a write into a mesh session, or a chunk from a
  second phone into a BLE session. The drop rule (`checkFwChunk`) is natively
  tested; its wiring (`FwChunkCallback` passing `FirmwareTransportKind::Ble`
  and the connection handle) is not, and the stock app cannot reach it: the
  lamp drops any phone link under the mesh pause, so a write can land only
  between the session arming and the kick taking effect, and the app streams
  only after its own OFFER was accepted. If NimBLE gives the next connection
  the same handle number, the connection check cannot tell the two phones
  apart; the digest check at verify still refuses a mixed image.

- True cross-variant gating (standard↔snafu) — needs a snafu lamp and a
  snafu-signed artifact; the channel-suffix row exercises the same
  `otaAcceptable()` gate.
- Multi-hop cascade across 3+ lamps and convergence speed on lossy links
  (known-slow DONE/REQ state-pivot behavior; parked).
- FS-image (SPIFFS web UI) OTA — same transport, `[fsdist]`/`[fs_ota]` tags;
  gets incidental coverage only.
- The rollback of an updated lamp whose BLE never starts (`[ota] BLE never
  started; rolling back`): the bench cannot stop BLE starting. The decision is
  natively tested in `test_ota_mark_valid`; the call it makes is not. The
  same goes for the halt on a malformed `HwConfig` (compiled in, so no bench
  build reaches it) and for the restart after IDF refuses a rollback.
- A filesystem offer sent or taken while unverified: `test_ota_offer` pins
  the send gate, and the rig has no pair of lamps whose web UIs differ.
- A hang in an updated lamp's setup: `loopwd.hangsetup` restarts the lamp as
  it arms, so the hang lands on the old image. Row 8's loop hang takes the
  same reset path.

## Signed local builds

`scripts/build_signed_lamp.sh` builds a signed beta/stable image and optionally
flashes the signed `app0`:

```sh
VARIANT=standard CHANNEL=beta scripts/build_signed_lamp.sh                          # build only
VARIANT=standard CHANNEL=beta PORT=<port> scripts/build_signed_lamp.sh --flash      # + flash
DEBUG=1 ... --flash    # force LAMP_DEBUG on a signed build for bench serial logs
```

Signing is automatic on a non-dev channel (post-build hook + `~/.lamp-os-firmware-key.bin`).

## Chunk-size sweep (`scripts/ota_size_test.sh`)

Picks `FW_CHUNK_SIZE_MAX` by measuring OTA time + REQ-retransmit count across
sizes on hardware. Flashes a fleet (receivers @1.1.1 + sender @1.1.2) at a size,
captures the cascade, parses per-target time + REQ.

```sh
# map ports to lamps first — device names shuffle on replug:
python3 -m esptool --port <PORT> --chip esp32 --after hard_reset read_mac
scripts/ota_size_test.sh <SIZE> <captureSeconds> <SENDER_PORT> <RECEIVER_PORT>...
```

4-lamp bench, 1.6 MB image: **1444 ≈ 38 s / ~4 REQ** beat **768 ≈ 67 s / ~7 REQ**
— fewer, bigger frames cost less airtime, and 1444 stayed ahead even attenuated.
Default is 1444 (v2 frame ceiling: 1444 + 26 B header = 1470).

## Lossy-link simulation (`LAMP_ESPNOW_TX_QDBM`)

Build with `-D LAMP_ESPNOW_TX_QDBM=<8..84>` (0.25 dBm units; 8 = 2 dBm) to cap
ESP-NOW TX power and weaken the link like distance; `TX_QDBM=<n>` on the sweep
harness threads it in. NOTE: TX-power reduction also degrades TX signal quality,
so it is a *pessimistic* distance proxy — real path-loss at the same RSSI is
gentler. At 2 dBm, 1444 still completed but with heavy loss (~144 REQ) — the
data behind the RSSI gate.

## Bench mechanics (do these or lose an hour)

- **Reset with esptool, not hand-toggled DTR/RTS** — a manual RTS pulse can drop
  a lamp into the bootloader (silent). `esptool --after hard_reset read_mac` runs it.
- **A version delta needs a forced clean build** — the injected version define is
  not a tracked dependency, so a `VERSION`-only change flashes a cached binary.
  Purge `.pio/build` + `.buildcache` first.
- **Do not touch `VERSION` while a build reads it** — the pre-build hook races.
- **Tail read-only and attach before the transfer** — opening a port mid-OTA
  resets the lamp and aborts it. `bench_tap.py` (or a DTR-held read) holds the lines.
- **Retry esptool flashes** — sync hiccups are transient.

## Cross-variant reject without a snafu lamp

The reject matrix (variant/channel/version gating) runs on plain standard lamps:
add a build env with standard hardware (`+<lamps/standard/>`) but a synthetic
`custom_lamp_variant = qatype`, so it presents as `qatype-beta` — cross-variant
to a real `standard` lamp — exercising the `otaAcceptable()` type-prefix gate
(`{type}-{channel}` must stay ≤ 16 bytes).

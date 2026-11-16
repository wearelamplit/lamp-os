# Pass: loop watchdog

Checks that a stalled loop task, in setup or in the loop, resets the lamp and
lands in `lastCrash` and the boot history, that the boot-loop guard backs off
and re-arms without a power cycle, and that nothing legitimate comes near the
timeout. The loop task is on the ESP-IDF task watchdog from the start of
`setup()` (`util/loop_watchdog.hpp`): 30 s per setup stage, then 5 s, or 30 s
while a firmware or filesystem image is being received.

## Hardware

- **1 lamp on USB serial**, dev build (`npm run lamp:flash`), tailed with
  `npm run lamp:tap -- <port>:lamp -o /tmp/loopwd.log`. `loopwd.hang`,
  `loopwd.hangsetup`, the `[setup]` lines and the `[loopwd]` high-waters are
  dev-only. Stop the tap before row 1: `bench_cmd.py`
  needs the port to itself.
- The app or host BLE (bleak) to read the `lamp` section.
- For row 3, a 2nd lamp on a newer signed image to send a mesh OTA (see
  [ota.md](ota.md)).

## Rows

1. **Stall resets and is recorded.** Note `lastCrash.count` in the `lamp`
   section (absent counts as 0). Run:

   ```sh
   scripts/bench_cmd.py <port> --cmd 'loopwd.hang' --duration 20 \
       -o /tmp/loopwd-hang.log
   ```

   Expect `[cmd] ok loopwd.hang`, then within about 5 s
   `task_wdt: Task watchdog got triggered` naming `loopTask (CPU 1)`, a panic
   and a reboot. After it boots, the `lamp` section shows `resetReason` 6 (task
   watchdog) and `lastCrash` with `count` one higher, `reset` 6, `core` 1, and
   a `pc` that `xtensa-esp32-elf-addr2line` puts in the `loopwd.hang` branch
   of `core/lamp_test_action.cpp`.
2. **Margin on an ordinary session.** Boot, connect the app, drag colours,
   open and close every sheet, save, then, with the app still connected, turn
   the web config on and join its softAP (this runs the BLE teardown). The
   2x rule reads the unfed span: the last `[loopwd] longest unfed span` must
   be at or under 2500 ms, half the 5 s timeout. A new longest always prints
   within about a second, so the last line covers the teardown. Record it,
   and the last `[loopwd] longest loop iteration` for reference (the teardown
   feeds mid-iteration, so the iteration can run longer than the span).
   Repeat the softAP join with the phone carried out of BLE range just before
   it, and record both again.
3. **Margin across an OTA.** Receive a mesh OTA on the tapped lamp. The upfront
   erase, the verify and the reboot must run through with no `task_wdt` line,
   and the post-reboot lamp boots the new version. The erase runs under the
   30 s timeout and the verify under 5 s; the stat covers both, so hold it to
   the tighter bound: the last `[loopwd] longest unfed span` before the
   reboot must be at or under 2500 ms. Record it, and the last `[loopwd]
   longest loop iteration`: the erase runs in one iteration, so this is how
   long the lamp stopped animating, and no bound applies to it.
4. **No reset loop on a normal boot.** Power-cycle the lamp three times. Each
   boot reaches the app with `resetReason` 1 (power-on) and no new
   `lastCrash` entry.

5. **Boot-loop backoff and re-arm.** Power-cycle, then run row 1's
   `loopwd.hang` three times, each within 60 s of the previous boot. After the
   third reset the boot log shows `[loopwd] 3 watchdog resets in a row;
   timeout 300 s until the loop runs 60 s`. Leave it alone: at 60 s of uptime
   it prints `[loopwd] loop ran 60 s; timeout back to 5 s`, and a
   `loopwd.hang` then resets it within about 5 s, with no power cycle. The
   next boot shows `4 watchdog resets in a row; timeout 300 s until the loop
   runs 120 s`; send `loopwd.hang` straight after that line: the lamp freezes
   and resets about 300 s after the hang.
6. **A stall after the first minute backs off too.** Power-cycle, then send
   row 1's `loopwd.hang` about 90 s into each boot. The first three boots reset
   within about 5 s of the hang. The fourth shows `3 watchdog resets in a row;
   timeout 300 s until the loop runs 60 s` and still resets within about 5 s
   of a hang at 90 s. The fifth shows `until the loop runs 120 s`: a hang at
   90 s now freezes the lamp and resets it about 300 s later. Each further
   boot's line doubles the window, up to 960 s.
7. **Setup hang resets and names its stage.** For each of `wifi`, `mesh` and
   `ota`, run:

   ```sh
   scripts/bench_cmd.py <port> --cmd 'loopwd.hangsetup mesh' --duration 60 \
       -o /tmp/loopwd-setup.log
   ```

   Expect `[cmd] ok loopwd.hangsetup mesh 1`, a restart, `[setup] bench hang at
   mesh, 0 more` with the strips dark, then within about 30 s
   `task_wdt: Task watchdog got triggered` naming `loopTask (CPU 1)`, a reboot,
   and a normal boot. Its `[boots] -1` line reads `end taskwdt setup mesh`,
   and the `lamp` section shows `resetReason` 6 and `lastCrash` one higher.
8. **Setup hang every boot backs off.** Run `loopwd.hangsetup ble 4`. Three
   boots hang and reset about 30 s apart; the fourth prints the backoff line,
   hangs at `ble` and resets about 300 s after the hang. The fifth boots
   normally, still backing off at `4 watchdog resets in a row; timeout 300 s
   until the loop runs 120 s`, and prints `loop ran 120 s; timeout back to 5 s`
   at 120 s. Its `[boots]` lines `-4` to `-1` all read `end taskwdt setup
   ble`.
9. **Setup stages stay inside the watchdog.** Flash a blank board with the web
   installer, then `npm run lamp:flash` the dev build, and tap its first boot
   (fresh NVS, PHY calibration). Then power-cycle and tap a normal boot. On
   both, every `[setup] <stage> +N ms` line must be at or under 15000 ms, half
   the 30 s stage timeout. Record the largest of each.

## Known limits

The watchdog arms in `setup()` right after `Serial.begin`, so a hang in the
ESP-IDF and Arduino start-up before `setup()` still leaves the lamp dark until
a power cycle. A hang before the boot history opens (in `initFlash`) resets but
leaves no `[boots]` entry of its own. The timeout is global, so a stall on
core 0's idle task is caught at 30 s during setup, and at 300 s in a backoff
boot until its loop has run its backoff window, rather than 5 s.

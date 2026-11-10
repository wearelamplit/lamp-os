# Pass: loop watchdog

Checks that a stalled loop task resets the lamp and lands in `lastCrash`, and
that nothing legitimate comes near the timeout. The loop task is on the ESP-IDF
task watchdog from the end of `setup()` (`util/loop_watchdog.hpp`): 5 s, or
30 s while a firmware or filesystem image is being received.

## Hardware

- **1 lamp on USB serial**, dev build (`npm run lamp:flash`), tailed with
  `npm run lamp:tap -- <port>:lamp -o /tmp/loopwd.log`. `loopwd.hang` and the
  `[loopwd]` line are dev-only. Stop the tap before row 1: `bench_cmd.py`
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

5. **Boot-loop guard.** Run row 1's `loopwd.hang` three times, each within
   60 s of the previous boot. After the third reset the boot log shows
   `[loopwd] 3 watchdog resets in a row; loop unwatched until a
   non-watchdog reset`, and a fourth `loopwd.hang` freezes the lamp with no
   reset. Power cycle it: the next boot is watched again, and a
   `loopwd.hang` resets it.

## Known limits

`setup()` runs unwatched, so a hang there still leaves the lamp dark until a
power cycle. After the boot-loop guard trips, the loop stays unwatched until
the lamp next restarts for any reason other than the task watchdog.

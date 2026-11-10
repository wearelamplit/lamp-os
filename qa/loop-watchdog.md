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
   section (absent counts as 0). Run `scripts/bench_cmd.py <port> --cmd
   'loopwd.hang' --duration 20 -o /tmp/loopwd-hang.log`. Expect `[cmd] ok loopwd.hang`, then within about 5 s
   `task_wdt: Task watchdog got triggered` naming `loopTask (CPU 1)`, a panic
   and a reboot. After it boots, the `lamp` section shows `resetReason` 6 (task
   watchdog) and `lastCrash` with `count` one higher, `reset` 6, `core` 1, and
   a `pc` that `xtensa-esp32-elf-addr2line` puts in the `loopwd.hang` branch
   of `core/lamp_test_action.cpp`.
2. **Margin on an ordinary session.** Boot, connect the app, drag colours,
   open and close every sheet, save, then turn the web config on and join its
   softAP (this runs the BLE teardown). Grep the log for `[loopwd] longest
   unfed span`: the last value must stay well under 5000 ms. Record it.
3. **Margin across an OTA.** Receive a mesh OTA on the tapped lamp. The upfront
   erase, the verify and the reboot must run through with no
   `task_wdt` line, and the post-reboot lamp boots the new version. Record the
   last `[loopwd] longest unfed span` before the reboot (the 30 s timeout
   applies here).
4. **No reset loop on a normal boot.** Power-cycle the lamp three times. Each
   boot reaches the app with `resetReason` 1 (power-on) and no new
   `lastCrash` entry.

## Known limit

`setup()` runs unwatched, so a hang there still leaves the lamp dark until a
power cycle.

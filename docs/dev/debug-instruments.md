# Debug instruments

Every entry is a bracket-tagged serial log line printed only under `LAMP_DEBUG`
(the dev channel; `signed ≡ channel != dev`). Tail them with `npm run lamp:tap`
(reset-safe) — **never** a raw `pyserial` / `screen` / `pio device monitor`
open, which DTR-resets the board and aborts any in-flight OTA or mesh wave. The
tag is the grep key; find the checkpoint that matters, then grep for it.

This catalog is transcribed from a string-literal grep over `software/**`; the
source file is where the tag's format strings live.

## Lamp firmware

| Tag | Source | Prints |
|---|---|---|
| `[heap]` | `util/heap_probe.hpp`, `core/lamp.cpp` | Free + largest contiguous block at named checkpoints (`at=%s free=%u largest=%u`): boot, mesh, webapp, ble-connect/disconnect/pre/post, ota-stream, web-save; every 30 s in a debug build a `trend` checkpoint and `txstalls=.. txrefused=..`, the TX credit resets and the sends dropped for want of a credit |
| `[loop]` | `core/lamp.cpp`, `core/lamp_drains.cpp` | Main-loop health: stack high-water mark, delayed-trigger queue eviction, knockout-drain pixel/brightness; `settingsBlob: save deferred; reboot waits for it`, a settings blob that needs a reboot whose save failed, and `settingsBlob: save never landed; no reboot`, the wait given up after 5 minutes (every build) |
| `[lamp]` | `core/lamp.cpp`, `main.cpp` | Boot: `FATAL` malformed HwConfig (halt), compiled vs NVS `lampType` |
| `[loopwd]` | `util/loop_watchdog.cpp` | Loop watchdog, two high-waters since boot, each printed when it sets a new high, at most once a second (a new high inside that second prints on a later feed): `longest unfed span N ms`, the longest the loop task went without feeding the task watchdog (the watchdog margin; the OTA erase and verify feed mid-iteration), and `longest loop iteration N ms`, the longest from one loop iteration's start to the next (how long the lamp stopped animating). Also `subscribe FAILED; loop unwatched`, and `N watchdog resets in a row; loop unwatched until a reset that is not a task-watchdog reset within 60 s of boot` (every build), the boot-loop guard in [`embedded-heap.md`](embedded-heap.md#reading-a-crash-record) |
| `[gov]` | `core/lamp.cpp` | Power governor clamp / release (`clamp demand=.. budget=.. ceiling=..`, `release demand=..`) |
| `[boots]` | `core/boot_history.cpp` | One line per ended boot, oldest first, at every boot on every build: firmware, uptime reached, lowest free heap, smallest largest block, how it ended; see [`embedded-heap.md`](embedded-heap.md#reading-the-boot-history) |
| `[power]` | `core/lamp.cpp` | Brownout-reset warning (supply-budget constants may be mis-sized) |
| `[ota]` | `core/lamp.cpp` | OTA boot health: `PENDING_VERIFY` self-check arm, new-partition-marked-valid, `mark_app_valid` failure |
| `[ledsnap]` | `core/lamp.cpp` | Per-strip LED snapshot transition (`from=(r,g,b,w) to=(...) d=..`) |
| `[compos]` | `core/compositor.cpp` | Compositor quiet/normal frame accounting (`quiet=.. normal=.. otaQuietHits=.. (ota=..)`) |
| `[override]` | `components/transient_override/color_override.cpp` | Transient color override apply / beginFade / drop-while-operator-editing |
| `[recv]` | `components/network/mesh/mesh_link.cpp` | Mesh RX dispatch: OVERRIDE_COLORS, RESTORE_COLORS, COMMAND parse drops |
| `[send]` | `components/network/mesh/mesh_link.cpp` | COMMAND resend dropped (frame > ring cap; single-send only) |
| `[show]` | `components/network/mesh/mesh_link.cpp` | Lamp ESP-NOW init / ready (mac) / HELLO recv |
| `[meshmix]` | `components/network/mesh/mesh_link.cpp` | 30 s mesh RX mix window (`hello / wisp_hello / paint / …` counts) |
| `[hellosupp]` | `components/network/mesh/mesh_link.cpp` | 30 s HELLO relay-suppression rate (`win=30s suppressed=.. relayed=.. rate=..%`) |
| `[relaystagger]` | `components/network/mesh/mesh_link.cpp` | 30 s staggered wisp-frame relays (`win=30s queued=.. failopen=.. deferred=.. dropped=.. maxlate=..ms`): relays queued, relayed inline (table full, frame oversize, or a known loop stall), passes a due relay waited for a TX credit, copies dropped 1 s past their fire time (a credit stall, a failing driver, an unannounced loop stall over 1 s, or a known stall that outlasts 1 s holding copies queued before it), and the latest a relay went out past its fire time. Over a run, `relayed_out` in `[meshmix]` less `[hellosupp] relayed` should equal queued + failopen - dropped, plus one per retried submit (`[espnow.tx] submit FAIL`) |
| `[wispstate]` | `components/network/mesh/mesh_link.cpp` | MSG_WISP_STATE adopt / release + 30 s window summary |
| `[wispcoex]` | `components/network/mesh/mesh_link.cpp` | Wisp-frame coex reception meter (`recv=.. maxgap=..ms`) |
| `[wispdirect]` | `components/network/mesh/mesh_link.cpp` | Direct wisp-paint receive (srcMac) |
| `[espnow]` | `components/network/transport/espnow_link.cpp` | ESP-NOW transport init / peer-add / reject-len |
| `[espnow.tx]` | `components/network/transport/espnow_link.cpp` | `no tx callback, credits reset n=..`: TX credits presumed lost after 2 s with no send callback and reset, with the running count (every build, at most every 10 s) |
| `[wifi]` | `components/network/transport/wifi.cpp` | WiFi state machine (softAP up/failed with `auth=wpa2\|open`, scan) |
| `[recovery]` | `core/lamp.cpp` | BOOT-hold cleared the control password (5s hold, first minute of uptime) |
| `[roster]` | `components/network/mesh/lamp_roster.cpp` | Roster update dropped on mutex contention |
| `[arrival]` | `core/arrival_notifier.cpp` | `onArrival` observer registry full |
| `[moment]` | `core/moments.cpp`, `core/lamp_test_action.cpp` | Moments: `start`, `join`, `accept`, `end` with kind and seed, a Whisper hop or chain end, settle reason, a frame given up, a skip with its reason (every build); `moment_state` dumps, each Fireflies beat `ff k=.. d=..` and a refused render (debug builds). See [`moments.md`](moments.md) |
| `[social]` | `behaviors/social.cpp` | Greet decision (`greet %s mode=.. frames=..`), introvert fatigue window |
| `[ble]` | `components/network/ble/bluetooth.cpp`, `ble_control.cpp` | Advertising config (adv colors, central scan stop/restart) + brightness recv |
| `[ble_control]` | `components/network/ble/ble_control.cpp` | GATT client connect / disconnect / auth, edit-session, home-mode focus, knockout write, settings_blob write, page CTRL/DATA, GATT binding + service start/stop, OTA pause/resume |
| `[wisp_state]` | `components/network/ble/ble_control.cpp`, `core/lamp_behaviors.cpp` | Wisp-state notify to app (controllingBase/Shade, preview) + provider active toggle |
| `[nvs]` | `config/config.cpp`, `config/disposition_store.cpp`, `config/nvs_config_store.cpp` | Config save (`via=`); a failed config save or dispositions flush with its cause and retry wait; a failed web document save with its cause (`invalid`, `no heap`, `too large`, `write`, `held for web-save reboot`); a save held for a web save's reboot; a failed NVS open/set/commit with its error code; an NVS init failure and whether the partition was erased (failures log on every build) |
| `[cfg]` | `config/config.cpp` | Config load (`cfg_bytes` is the "cfg" string alone) / parse-failure (serving defaults); `expressions=N from records|cfg json`, the loaded count, every build; an unreadable expression record dropped; stored expressions truncated to the caps on load; `STORED CONFIG BLOB UNREADABLE`, a rest or lamp blob that exists but does not read (every build) |
| `[drain]` | `core/lamp_drains.cpp` | Core 0→1 drain of brightness / colors / edit-session |
| `[drop]` | `util/dropped_write_log.cpp` | A live-control write the lamp dropped, with the characteristic and the reason (`shadeColors: ota quiet`, `baseColors: parse failed`): every BLE write refused in OTA quiet mode, every colour, expression, wifi, remote-op, dispositions or settings-blob write whose payload did not parse, and a settings blob discarded while a firmware image is being received (`ota receiving`). At most 4 lines a second across all of them; the next line printed carries `(+N suppressed)` |
| `[webapp]` | `components/webapp/webapp.cpp` | softAP web-config server up / failed, with the AP's `auth=` and boot window; `pending save failed (seq N)`, a web save that failed after its handler answered 202 (every build); `reboot after 10 min OTA wait`, a web save's reboot that stopped waiting on OTA quiet mode (every build) |
| `[fwdist]` | `components/firmware/firmware_distributor.cpp` | Firmware OTA distribution state (disabled reasons, offer/serve) |
| `[fsdist]` | `components/firmware/firmware_distributor.cpp` | Filesystem-image OTA distribution (disabled reasons) |
| `[fw_receiver]` | `components/firmware/firmware_receiver.cpp` | Mesh OTA receive: OFFER accept/decline, chunk recv/drop, upfront-erase, verify, signature/digest, type-gate, set_boot_partition |
| `[fw_sig]` | `components/firmware/firmware_signature.cpp` | Signature-verify detail (imageLen / signed-region len, footer + computed sha256 + pubkey hex dumps) |
| `[ota_ind]` | `components/firmware/ota_indicator.cpp` | OTA progress-indicator state (`rxInProgress`, rxTotal, done/total whole/frac) |
| `[fs_ota]` | `components/firmware/fs_ota.cpp` | Filesystem-image OTA apply / online (spiffs bytes, digestReady, version; no-spiffs disable) |
| `[expr]` | `expressions/expression_manager.cpp`, `expressions/expression_observer.cpp` | Expression fired / coalesce-in-flight / home-mode drop; a dropped transient (local or remote, every build, at most every 5 s); upsert over caps, a rejected target or an unregistered type (`core/lamp.cpp`) |
| `[cascade]` | `expressions/expression_manager.cpp` | Cascade fan-out skip reasons (not-enabled / continuous / dedup), start, a waiting cascade replaced, an over-cap payload or delay rewrite dropped, a drain stopped for OTA |
| `[trigger]` | `expressions/expression_manager.cpp` | Named trigger fired (matched type/target) |
| `[invocation]` | `expressions/expression_invocation.cpp` | Invocation dropping bad colors |
| `[param]` | `expressions/param_utils.hpp` | Descriptor key absent after `applyDefaults` (schema bug) |
| `[shifty]` | `expressions/shifty/shifty_expression.cpp` | Shifty fade-start / hold-start / fade-back timing |

### Bench-control tags

Serial-ingress commands (`scripts/bench_cmd.py`), `core/lamp_test_action.cpp`:

| Tag | Prints |
|---|---|
| `[cmd]` | Serial command parse (`ok a=%s`, parse / missing-action errors); `ok loopwd.hang` before the `loopwd.hang` command stalls the loop task until the watchdog resets the lamp ([`qa/loop-watchdog.md`](../../qa/loop-watchdog.md)) |
| `[test]` | Transient pulse / configured-trigger echo |
| `[personality]` | `inject_nearby` / `clear_nearby` bench overrides |

## Wisp firmware

| Tag | Source | Prints |
|---|---|---|
| `[wisp]` | `wisp/src/core/wisp_controller.cpp`, `console/serial_console.cpp`, `main.cpp` | Roster dump, source/paintMode state |
| `[wisploop]` | `wisp/src/main.cpp` | Loop stall (`stall=..ms`) |
| `[wispheap]` | `wisp/src/status/status_emitter.cpp` | Wisp free + largest heap (`free=.. largest=..`) |
| `[mesh]` | `wisp/src/net/mesh_link.cpp` | Wisp ESP-NOW init / ready (ch, mac) |
| `[wifi]` | `wisp/src/net/wifi_link.cpp`, `console/serial_console.cpp` | Wisp WiFi STA join: `<ssid> found on ch 6; joining`, `<ssid> not on ch 6; retry in Ns`, `mesh-channel scan failed to start`, `mesh-channel scan failed; retry in Ns`, `<ssid> join failed\|link lost; retry in Ns`, `<ssid> join\|scan timed out; retry in Ns`, `STA not wanted (source not Osc, or Ethernet up); idle`, `disconnected reason=N`, `got IP`; channel-6 guard drop logs `AP ch=N != mesh ch=6; dropping association, re-pinning radio to mesh, retries halted` |
| `[eth]` | `wisp/src/net/eth_link.cpp` | Wisp W5500 Ethernet: `no W5500 (cs=N); Ethernet off` (once at boot, no module fitted, after the framework's expected `w5500.mac` / `esp_eth` / `ETH.cpp` error lines listed in the wisp README), `spi_bus_free failed: N`, `W5500 up as <hostname>; waiting for link`, `link up N Mbps full\|half duplex`, `link down`, `got IP a.b.c.d`, `lost IP`; `eth:show` prints the same link line with `ip=` |
| `[wisp.beacon]` | `wisp/src/status/status_emitter.cpp` | `connectivity edge wifi=N eth=N` (re-emits wispStatus), wispStatus / wispConfig build failures |
| `[bright]` | `wisp/src/paint/paint_distributor.cpp` | Per-lamp brightness send (`send %u->mac seq=..`) |
| `[osc]` | `wisp/src/core/wisp_controller.cpp`, `osc/osc_listener.cpp` | OSC palette in (`palette n=.. via serial\|udp`, at most 1/s), stream stale, UDP listen / bind failures |
| `[artnet]` | `wisp/src/artnet/artnet_emitter.cpp` | Art-Net UDP emit (ready / frame-build-failed) |
| `[stage]` | `wisp/src/net/stage_beacon.cpp` | Stage BLE beacon advertise (creds, mfg-data size) |

The wisp RX signal has no `[wisprx]` literal; it rides `[wispcoex]` / `[recv]`
(lamp side) and `[mesh]` (wisp side).

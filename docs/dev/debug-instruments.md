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
| `[heap]` | `util/heap_probe.hpp` | Free + largest contiguous block at named checkpoints (`at=%s free=%u largest=%u`): boot, mesh, webapp, ble-connect/disconnect/pre/post, ota-stream, web-save |
| `[loop]` | `core/lamp.cpp`, `core/lamp_drains.cpp` | Main-loop health: stack high-water mark, delayed-trigger queue eviction, knockout-drain pixel/brightness |
| `[lamp]` | `core/lamp.cpp`, `main.cpp` | Boot: `FATAL` malformed HwConfig (halt), compiled vs NVS `lampType` |
| `[gov]` | `core/lamp.cpp` | Power governor clamp / release (`clamp demand=.. budget=.. ceiling=..`, `release demand=..`) |
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
| `[wispstate]` | `components/network/mesh/mesh_link.cpp` | MSG_WISP_STATE adopt / release + 30 s window summary |
| `[wispcoex]` | `components/network/mesh/mesh_link.cpp` | Wisp-frame coex reception meter (`recv=.. maxgap=..ms`) |
| `[wispdirect]` | `components/network/mesh/mesh_link.cpp` | Direct wisp-paint receive (srcMac) |
| `[espnow]` | `components/network/transport/espnow_link.cpp` | ESP-NOW transport init / peer-add / reject-len |
| `[wifi]` | `components/network/transport/wifi.cpp` | WiFi state machine (softAP up/failed, scan) |
| `[roster]` | `components/network/mesh/lamp_roster.cpp` | Roster update dropped on mutex contention |
| `[arrival]` | `core/arrival_notifier.cpp` | `onArrival` observer registry full |
| `[social]` | `behaviors/social.cpp` | Greet decision (`greet %s mode=.. frames=..`), introvert fatigue window |
| `[ble]` | `components/network/ble/bluetooth.cpp`, `ble_control.cpp` | Advertising config (adv colors, central scan stop/restart) + brightness recv |
| `[ble_control]` | `components/network/ble/ble_control.cpp` | GATT client connect / disconnect / auth, edit-session, home-mode focus, knockout write, settings_blob write, page CTRL/DATA, GATT binding + service start/stop, OTA pause/resume |
| `[wisp_state]` | `components/network/ble/ble_control.cpp`, `core/lamp_behaviors.cpp` | Wisp-state notify to app (controllingBase/Shade, preview) + provider active toggle |
| `[nvs]` | `config/config.cpp`, `core/lamp_drains.cpp` | `persistConfig` write (bytes, OOM, zero-byte) |
| `[cfg]` | `config/config.cpp` | Config load / parse-failure (serving defaults) |
| `[drain]` | `core/lamp_drains.cpp` | Core 0→1 drain of brightness / colors / edit-session |
| `[webapp]` | `components/webapp/webapp.cpp` | softAP web-config server up / failed |
| `[fwdist]` | `components/firmware/firmware_distributor.cpp` | Firmware OTA distribution state (disabled reasons, offer/serve) |
| `[fsdist]` | `components/firmware/firmware_distributor.cpp` | Filesystem-image OTA distribution (disabled reasons) |
| `[fw_receiver]` | `components/firmware/firmware_receiver.cpp` | Mesh OTA receive: OFFER accept/decline, chunk recv/drop, upfront-erase, verify, signature/digest, type-gate, set_boot_partition |
| `[fw_sig]` | `components/firmware/firmware_signature.cpp` | Signature-verify detail (imageLen / signed-region len, footer + computed sha256 + pubkey hex dumps) |
| `[ota_ind]` | `components/firmware/ota_indicator.cpp` | OTA progress-indicator state (`rxInProgress`, rxTotal, done/total whole/frac) |
| `[fs_ota]` | `components/firmware/fs_ota.cpp` | Filesystem-image OTA apply / online (spiffs bytes, digestReady, version; no-spiffs disable) |
| `[expr]` | `expressions/expression_manager.cpp`, `expressions/expression_observer.cpp` | Expression fired / coalesce-in-flight / home-mode drop |
| `[cascade]` | `expressions/expression_manager.cpp` | Cascade fan-out skip reasons (not-enabled / continuous / dedup) |
| `[trigger]` | `expressions/expression_manager.cpp` | Named trigger fired (matched type/target) |
| `[invocation]` | `expressions/expression_invocation.cpp` | Invocation dropping bad colors |
| `[param]` | `expressions/param_utils.hpp` | Descriptor key absent after `applyDefaults` (schema bug) |
| `[shifty]` | `expressions/shifty/shifty_expression.cpp` | Shifty fade-start / hold-start / fade-back timing |

### Bench-control tags

Serial-ingress commands (`scripts/bench_cmd.py`), `core/lamp_test_action.cpp`:

| Tag | Prints |
|---|---|
| `[cmd]` | Serial command parse (`ok a=%s`, parse / missing-action errors) |
| `[test]` | Transient pulse / configured-trigger echo |
| `[personality]` | `inject_nearby` / `clear_nearby` bench overrides |

## Wisp firmware

| Tag | Source | Prints |
|---|---|---|
| `[wisp]` | `wisp/src/core/wisp_controller.cpp`, `console/serial_console.cpp`, `main.cpp` | Roster dump, zone/source/paintMode state |
| `[wisploop]` | `wisp/src/main.cpp` | Loop stall (`stall=..ms`) |
| `[wispheap]` | `wisp/src/status/status_emitter.cpp` | Wisp free + largest heap (`free=.. largest=..`) |
| `[mesh]` | `wisp/src/net/mesh_link.cpp` | Wisp ESP-NOW init / ready (ch, mac) |
| `[wifi]` | `wisp/src/net/wifi_link.cpp`, `console/serial_console.cpp` | Wisp WiFi link state; channel-6 guard drop logs `AP ch=N != mesh ch=6; dropping association, re-pinning radio to mesh, auto-reconnect off` |
| `[bright]` | `wisp/src/paint/paint_distributor.cpp` | Per-lamp brightness send (`send %u->mac seq=..`) |
| `[client]` | `wisp/src/aurora/AuroraPaletteClient.cpp` | Aurora client idle / armed / static-host |
| `[ws]` | `wisp/src/aurora/AuroraWsConnection.cpp` | Aurora WebSocket connecting / lost |
| `[mdns]` | `wisp/src/aurora/AuroraDiscovery.cpp` | mDNS Aurora discovery (found / begin-failed) |
| `[http]` | `wisp/src/aurora/PaletteFetcher.cpp` | HTTP palette fetch (`GET palette .. -> ..`) |
| `[artnet]` | `wisp/src/artnet/artnet_emitter.cpp` | Art-Net UDP emit (ready / frame-build-failed) |
| `[stage]` | `wisp/src/net/stage_beacon.cpp` | Stage BLE beacon advertise (creds, mfg-data size) |

The wisp RX signal has no `[wisprx]` literal; it rides `[wispcoex]` / `[recv]`
(lamp side) and `[mesh]` (wisp side).

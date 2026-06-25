# Wisp overnight build summary

**Branch:** `flutter-rewrite` (local only, nothing pushed)

## Phases that landed

| Phase | Commit | What |
|---|---|---|
| **0 — Scaffolding** | `d52d085` | New `software/wisp/` PlatformIO project for the Seeed Xiao ESP32-C6. Stub `main.cpp` that prints `wisp` on boot. README, .gitignore, include/lib/test skeleton READMEs. `software/lamp-os/TODO.md` line 65 updated to point at wisp. Old `software/artnet-repeater/` left in place for reference. |
| **A — Mesh awareness + firmware version** | `47a5577` | New `software/lamp-os/src/version.hpp` (`FIRMWARE_VERSION = 0x010000`, `FIRMWARE_CHANNEL`, `FIRMWARE_BUILD_STAMP`). HELLO wire format gained a `uint32_t firmwareVersion` field; `PROTOCOL_VERSION` bumped `0x01 → 0x02`. `show_receiver` emits the version on every HELLO and parses it back into `NearbyLamp.firmwareVersion`. Wisp side gained a mirrored `lamp_protocol.hpp`, a `MeshLink` (esp_now_init + channel pin + recv/send), and a `LampInventory` (MAC-keyed, mutex-guarded, prune at 2 min). Wisp `main.cpp` brings the radio up on channel 1, registers the HELLO handler, and dumps the inventory every 10 s. |
| **B — Aurora ingest** | `5754377` | Vendored `~/Downloads/esp32-palette-client/` into `software/wisp/src/aurora/` + `software/wisp/lib/miniz/`: 16 Aurora .cpp/.h, 5 proto files (nanopb-generated), 2 miniz files. `platformio.ini` got `lib_deps` (`nanopb`, `ArduinoWebsockets`, `ArduinoJson`), build flags (`-I src -I src/aurora -I src/aurora/proto -I lib/miniz -D PB_FIELD_32BIT=1`), and a `native` env for host-portable tests. `CurrentPalette` holder reads from both `palette.hexColors` (w=0) and `palette.colors[].{r,g,b,w}`; amber + UV dropped. `main.cpp` instantiates `AuroraPaletteClient`, registers `onActivePalette` (first-zone-wins), and logs every palette change. |

## Verification at last build

- **lamp-os firmware**: `pio test -e native` → **90/90 green**. `pio run -e upesy_wroom` → SUCCESS, RAM 18.7 %, Flash 69.6 %.
- **wisp firmware**: `pio run -e seeed_xiao_esp32_c6` → SUCCESS, RAM 13.8 %, Flash 58.6 %.
- **wisp host tests**: `pio test -e native` → **5/5 green** across `test_palette_parse` (4) + `test_notification_codec` (1).

## Intentional deferrals

- **Phase B Step 5 — real WiFi credentials.** `WiFi.begin(...)` is commented out in `software/wisp/src/main.cpp` with a `// TODO Phase D` marker. Aurora discovery will fail without a network — that's expected until Phase D lands the BLE-proxy onboarding flow.
- **Phase B Step 7 — hardware test.** Build-only verification overnight; needs you to point wisp at a real Aurora on your LAN.
- **artnet-repeater archival.** `software/artnet-repeater/` is still in the tree. Either delete it whenever you're confident wisp has fully taken over, or leave it as a reference — your call.
- **Phase C onward.** Wisp paint plumbing (Phase C), app pane (D), signed firmware (E), mesh OTA (F), rollback (G) are untouched. C is the natural next session and the spec is detailed enough to dispatch one focused subagent per phase the same way.

## First thing to do when you wake up

1. **Re-flash both lamps.** Phase A bumped `PROTOCOL_VERSION` from `0x01` to `0x02`, so currently-flashed lamps will drop HELLOs from a freshly-built lamp and vice versa. Easy fix — `npm run flash` (or `pio run -e upesy_wroom -t upload` from `software/lamp-os/`) on each lamp.
2. **Sanity-check the inventory.** After both lamps are on the new firmware, point a serial monitor at one of them (`pio device monitor -e upesy_wroom`). You should see `[show] CONTROL_OP apply` lines as before, and `nearby_lamps` should now carry firmware versions internally (not yet surfaced in the BLE characteristic — that's Phase D).
3. **Pick what to tackle next.**
   - If you want to keep wisp moving: Phase C dispatches cleanly (`MSG_WISP_*` types + lamp-side override drain + wisp `PaintDistributor`).
   - If the cascade-glitch diagnosis from yesterday is still bugging you: the printfs I left in `expression_manager.cpp` / `shifty_expression.cpp` / `show_receiver.cpp` will tell you whether `firstFired` was null after the test, whether shifty's chain-to-glitchy ran, and how many peers the fanout found. Quickest move is to look at the connected lamp's monitor when you click Test and confirm one of the four `[cascade]` skip-reasons doesn't print (which would mean `firstFired == nullptr` — likely target mismatch).

## Files in scope, files NOT touched

**Touched** (per the plan, nothing else):
- `software/wisp/**` — entire new directory.
- `software/lamp-os/src/version.hpp` (new).
- `software/lamp-os/src/components/network/lamp_protocol.hpp` (HelloFixed + PROTOCOL_VERSION).
- `software/lamp-os/src/components/network/show_receiver.cpp` (HELLO emit + parse).
- `software/lamp-os/src/components/network/nearby_lamps.{hpp,cpp}` (firmwareVersion field).
- `software/lamp-os/TODO.md` (line 65).

**Not touched:** app side, BLE characteristics, anything else in `software/lamp-os/src/` outside the four files above. `~/.claude/` and `.superpowers/` untracked dirs left alone.

Nothing was pushed.

# Dev environment

Three ways to develop with the app:

| Path | Who | BLE |
|---|---|---|
| **Emulator + bridge** | Any OS | Host Bluetooth via the Python bridge |
| **Real phone** | Mac (reference platform) | Direct; no bridge needed |
| **Unit tests only** | Any OS | Not needed |

The bridge (`tools/fake-lamp-bridge/`) runs on your Mac/Windows host, connects
to real lamps over Bluetooth, and proxies GATT over HTTP/WebSocket so the app
in an emulator can drive them. OTA is not supported over the bridge; flash
firmware separately over USB.

## Guides

- [`setup.md`](setup.md) — install the toolchain, task catalog, flash a lamp, update the app.
- [`windows-emulator.md`](windows-emulator.md) — Windows + Android emulator + bridge (the constrained path).
- [`mac-hardware.md`](mac-hardware.md) — Mac + real phone/emulator + bridge (the reference path).

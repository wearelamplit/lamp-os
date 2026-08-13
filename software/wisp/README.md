# Wisp

A small magical light that signals to others. Wisp is dedicated infrastructure (Seeed Xiao ESP32-C6 with a remote antenna) — the rebuilt identity of the old `software/artnet-repeater/` board. It is not a lamp; it is a non-participating helper that sits at the edge of the mesh.

## Two jobs

1. **Palette bridge.** Discovers an Aurora device on the LAN (mDNS + WebSocket + Protobuf + HTTP), follows its active palette, and fans out per-lamp paired colors to the mesh as transient base-color overrides over ESP-NOW.
2. **Firmware carrier.** Holds the latest signed lamp firmware blob and pushes it out to any lamp on the mesh running an older version (single-peer-at-a-time, signed with ed25519, channel-matched). Wisp itself is flashed via USB; it never self-updates.

## Off-by-default paint

Status reporting, mesh-inventory tracking, and firmware push all run unconditionally at boot. Aurora discovery and the notification WebSocket run only while the source mode is Aurora. **Palette painting requires explicit user opt-in via the app each boot.** A power blip therefore never surprises lamps with a sudden color change — wisp comes back up watching, not painting.

Lamp overrides are transient (RAM-only on the lamp side). When wisp drops or paint mode goes off, lamps re-render their saved base config. The user's stored config is never touched.

## How the user talks to it

Wisp has no BLE. The app talks to wisp via any nearby lamp acting as a proxy: the lamp surfaces wisp's last-overheard `MSG_WISP_HELLO` on a BLE characteristic, and forwards ops back to wisp over the mesh. WiFi credentials are configured the same way — out of the box, flash wisp once over USB and configure it from the app.

## Wire format

Wisp/lamp message types (`MSG_WISP_HELLO`, `MSG_WISP_PALETTE`, `MSG_OVERRIDE_COLORS`/`MSG_RESTORE_COLORS`, `MSG_CONTROL_OP` wisp envelopes) and the OTA exchange (`MSG_FW_OFFER` … `MSG_FW_RESULT`) are documented in [`docs/dev/networking.md`](../../docs/dev/networking.md). LSIG signed-firmware footer format lives alongside the build scripts at [`scripts/README.md`](../../scripts/README.md).

# Architecture map

The big picture in one screen: the three components, the links between them,
where each subsystem lives, and pointers to the deep docs. The wire format is
[`networking.md`](networking.md); the *why* behind the hard choices is the
[ADRs](../adrs/README.md). This page duplicates neither.

## The three components

Everything lives under [`software/`](../../software/).

- **`lamp-os/`** — the lamp firmware (ESP32-WROOM). Runs the behavior stack,
  compositor, expressions, and personality; talks BLE GATT to the app and
  ESP-NOW mesh to other lamps. Per-variant builds
  (`upesy_wroom_standard` / `_snafu` / `_staff`). Source: `software/lamp-os/src/`.
- **`wisp/`** — the infrastructure node (Seeed XIAO ESP32-C6, external antenna).
  Subscribes to an Aurora palette source over LAN, samples it, and distributes
  paint to lamps over the mesh; beacons its status. USB-flash-only (no OTA).
  Build env `seeed_xiao_esp32_c6`. Source: `software/wisp/src/`.
- **`lamp-app-flutter/`** — the iOS/Android control app. Talks BLE GATT to lamps;
  no direct mesh participation. Source: `software/lamp-app-flutter/lib/`.

## How they fit

```
        Aurora device ──LAN (WiFi: mDNS + WebSocket + HTTP)──► wisp
                                                                │
                                                                │ ESP-NOW (channel 6)
                                                                ▼
        Phone ──BLE GATT (NimBLE, AES-GCM)──► Lamp ◄──ESP-NOW mesh──► Lamp ◄──► Lamp N
```

Four links carry everything:

- **app ↔ lamp** — BLE GATT, app-layer AES-GCM auth. Settings, colors,
  expressions, OTA push.
- **lamp ↔ lamp** — ESP-NOW broadcast mesh, channel 6. Presence, greetings,
  transient overrides, expression announce, firmware distribution.
- **Aurora → wisp** — LAN WiFi (mDNS discovery, WebSocket palette subscription,
  HTTP palette fetch).
- **wisp → lamps** — ESP-NOW mesh (paint state + status beacon). The wisp reaches
  the app only by proxy: it broadcasts, lamps cache, the app reads off a
  connected lamp.

Full topology and channel details: [`networking.md`](networking.md#topology).

## Data flows

| Flow | Path | Deep doc |
|---|---|---|
| **App control write** | Phone → BLE GATT characteristic → Core-0 callback → pending slot → Core 1 applies + persists NVS (and mesh-cascades where relevant) | [`networking.md`](networking.md#ble-gatt-characteristics-lamp--phone) |
| **Presence / roster** | Each lamp broadcasts `MSG_HELLO` (TLV trailer); the wisp broadcasts `MSG_WISP_HELLO`. Receivers fold sightings into the unified `LampRoster` (near + mesh, RSSI) | [`networking.md`](networking.md#tier-1-presence) |
| **Aurora paint** | Aurora palette → wisp samples → `MSG_WISP_STATE` paint broadcast → lamp composites the wisp layer over its scene, easing on adopt/release | [`networking.md`](networking.md#wisp-paints-a-lamps-base--shade-declarative-broadcast) |
| **Firmware OTA** | A signed lamp offers `MSG_FW_*` over the mesh; an RSSI-gated peer requests chunks, verifies the ed25519 signature from flash, then swaps A/B slots | [`networking.md`](networking.md#firmware-distribution-messages-msg_fw_) |

## Where subsystems live

| Concept | Location |
|---|---|
| Lamp runtime / compositor | `software/lamp-os/src/core/` (`lamp.cpp`, `compositor.cpp`, `power_governor.cpp`) |
| Behaviors (social, idle, fade, configurator) | `software/lamp-os/src/behaviors/` |
| Expressions | `software/lamp-os/src/expressions/` |
| Personality / crowd-dim / bids | `software/lamp-os/src/core/` (`personality_engine`, `bid_receiver`, `arrival_notifier`) |
| Mesh | `software/lamp-os/src/components/network/mesh/` |
| BLE GATT | `software/lamp-os/src/components/network/ble/` |
| Firmware OTA | `software/lamp-os/src/components/firmware/` |
| Config / NVS | `software/lamp-os/src/config/` |
| Variants | `software/lamp-os/src/lamps/{standard,snafu,staff}/` |
| Shared protocol / crypto / LED | `software/shared/{protocol,crypto,led-common}/` |
| Wisp Aurora / paint / status | `software/wisp/src/{aurora,paint,status}/` |
| App | `software/lamp-app-flutter/lib/` |

## The lock-ins

Three contracts don't change without care, and their source of truth is
[`CLAUDE.md`](../../CLAUDE.md) (**Lock-ins**), not this page: the mesh
**receive-range** version (`[RX_MIN, RX_MAX]`, TLV-first for additive fields),
the **frozen BLE GATT attribute layout** (positional handles, append-only
growth), and the per-variant `{type}-{channel}` **OTA gate**. The rationale
behind the mesh, OTA, and dual-core decisions is in the
[ADRs](../adrs/README.md).

## Where to go next

- [`lamp-framework.md`](lamp-framework.md) — the lamp runtime internals.
- [`building-custom-lamps.md`](building-custom-lamps.md) — authoring a variant.
- [`networking.md`](networking.md) — the authoritative wire-format spec.
- [`debug-instruments.md`](debug-instruments.md) — the serial-log tag catalog.

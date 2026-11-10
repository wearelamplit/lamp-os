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
  Takes a palette from Straylight over OSC (USB serial or UDP), samples it, and
  distributes paint to lamps over the mesh (and to pre-mesh lamps as ArtNet DMX
  over its own softAP); beacons its status. USB-flash-only (no OTA).
  Build env `seeed_xiao_esp32_c6`. Source: `software/wisp/src/`.
- **`lamp-app-flutter/`** — the iOS/Android control app. Talks BLE GATT to lamps;
  no direct mesh participation. Source: `software/lamp-app-flutter/lib/`.

## How they fit

```
        Straylight ──OSC (SLIP over USB serial, or UDP :9000)──► wisp
                                                                │
                                                                │ ESP-NOW (channel 6)
                                                                ▼
        Phone ──BLE GATT (NimBLE, AES-GCM)──► Lamp ◄──ESP-NOW mesh──► Lamp ◄──► Lamp N

        wisp ──softAP + BLE stage beacon, ArtNet DMX over UDP──► pre-mesh lamps
```

Five links carry everything:

- **app ↔ lamp** — BLE GATT, app-layer AES-GCM auth. Settings, colors,
  expressions, OTA push.
- **lamp ↔ lamp** — ESP-NOW broadcast mesh, channel 6. Presence, greetings,
  transient overrides, expression announce, firmware distribution.
- **Straylight → wisp** — OSC 1.0 palettes, as SLIP frames on the USB serial
  console or UDP datagrams on port 9000 (softAP, the wisp's STA LAN join, or
  wired Ethernet through an optional W5500 on SPI, which keeps the feed off the
  radio). The STA join runs only while the source is Osc and Ethernet is not
  up, and joins only an AP a channel-6-only scan found: one shared radio serves
  both WiFi and ESP-NOW, so a scan elsewhere, or a missing AP rescanned every
  few seconds, would drag the radio off the mesh. Failed attempts retry on a
  backoff.
- **wisp → lamps** — ESP-NOW mesh (paint state + status beacon). The wisp reaches
  the app only by proxy: it broadcasts, lamps cache, the app reads off a
  connected lamp.
- **wisp → pre-mesh lamps** — ArtNet DMX over UDP/6454, on a wisp-hosted softAP
  whose credentials ride a BLE stage beacon. The only path to lamps too old to
  speak mesh. Details: [`software/wisp/README.md`](../../software/wisp/README.md).

Full topology and channel details: [`networking.md`](networking.md#topology).

## Data flows

| Flow | Path | Deep doc |
|---|---|---|
| **App control write** | Phone → BLE GATT characteristic → Core-0 callback → pending slot → Core 1 applies + persists NVS (and mesh-cascades where relevant) | [`networking.md`](networking.md#ble-gatt-characteristics-lamp--phone) |
| **Presence / roster** | Each lamp broadcasts `MSG_HELLO` (TLV trailer); the wisp broadcasts `MSG_WISP_HELLO`. Receivers fold sightings into the unified `LampRoster` (near + mesh, RSSI) | [`networking.md`](networking.md#tier-1-presence) |
| **Wisp paint** | Straylight OSC (or manual) palette → wisp samples → `MSG_WISP_STATE` paint broadcast → lamp composites the wisp layer over its scene, easing on adopt/release | [`networking.md`](networking.md#wisp-paints-a-lamps-base--shade-declarative-broadcast) |
| **Firmware OTA** | A signed lamp offers `MSG_FW_*` over the mesh; an RSSI-gated peer requests chunks, verifies the ed25519 signature from flash, then swaps A/B slots | [`networking.md`](networking.md#firmware-distribution-messages-msg_fw_) |

## Where subsystems live

| Concept | Location |
|---|---|
| Lamp runtime / compositor | `software/lamp-os/src/core/` (`lamp.cpp`, `compositor.cpp`, `power_governor.cpp`) |
| Behaviors (social, idle, fade, configurator) | `software/lamp-os/src/behaviors/` |
| Expressions | `software/lamp-os/src/expressions/` |
| Personality / crowd-dim / arrivals | `software/lamp-os/src/core/` (`personality_engine`, `arrival_notifier`) |
| Mesh | `software/lamp-os/src/components/network/mesh/` |
| BLE GATT | `software/lamp-os/src/components/network/ble/` |
| Firmware OTA | `software/lamp-os/src/components/firmware/` |
| Config / NVS | `software/lamp-os/src/config/` |
| Variants | `software/lamp-os/src/lamps/{standard,snafu,staff}/` |
| Shared protocol / crypto / LED / clock | `software/shared/{protocol,crypto,led-common,clock}/` |
| Wisp OSC / paint / status | `software/wisp/src/{osc,paint,status}/` |
| Wisp ArtNet bridge (pre-mesh lamps) | `software/wisp/src/artnet/`, `src/net/{wifi_link,stage_beacon}` |
| Wisp wired Ethernet (W5500) | `software/wisp/src/net/eth_link`, STA rule `staWanted()` in `src/net/sta_join.hpp` |
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
- [`app-ble.md`](app-ble.md) — how the app holds BLE links and lists lamps.

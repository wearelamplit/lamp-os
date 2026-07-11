# Lamp OS networking reference

This document describes the communication protocols that move data between the
lamps, the wisp infrastructure node, the Flutter app, and the Aurora palette
device — the **ESP-NOW mesh** between lamps, the **BLE GATT** link to the phone,
and the firmware-distribution messages. It is the authoritative wire-format
reference; if code and this doc disagree, the code wins and this doc should be
updated.

## Topology

```
                                 ╔═══════════════════════════╗
                                 ║       Aurora device       ║
                                 ║      (palette source)     ║
                                 ╚════════════╤══════════════╝
                                              │ LAN (WiFi)
                                              │ mDNS discovery
                                              │ WebSocket subscription
                                              │ Protobuf notifications
                                              │ HTTP palette fetch
                                              ▼
            ┌──────────────────────────────────────────────────────┐
            │                       wisp                           │
            │              Seeed Xiao ESP32-C6, external antenna   │
            │                                                      │
            │   Aurora client  ──►  CurrentPalette                 │
            │                       TupleSampler                   │
            │                       PaintDistributor               │
            │                                                      │
            │   StatusBeacon   ──►  MSG_WISP_HELLO                 │
            └─────────────────┬────────────────────────────────────┘
                              │                   
                              │ESP-NOW            
                              ▼                  
            ╔════════════════════════════════════════=================
            ║                  ESP-NOW grid mesh        channel 6    ║
            ╠════════════╦═══════════════════════╦═══════════════════╣
            ║   Lamp 1   ║       Lamp 2          ║      Lamp N       ║
            ║ ESP32-WROOM║      ESP32-WROOM      ║    ESP32-WROOM    ║
            ╚═════╤══════╩══════════════╤════════╩═══════════════════╝
                  │                     │
                  │ BLE GATT            │ BLE GATT
                  │ (NimBLE,           │
                  │  AES-GCM auth)      │
                  ▼                     ▼
            ┌─────────────┐       ┌─────────────┐
            │    Phone    │       │    Phone    │
            │  (Flutter)  │       │  (Flutter)  │
            └─────────────┘       └─────────────┘
```

## Message tier overview

Four behavioral tiers, each with its own crypto posture, reach, and lifetime:

| Tier | Reach | Crypto | Lifetime | Examples |
|---|---|---|---|---|
| **Presence** | Broadcast (lamp: 5s, wisp: 2s) | Plaintext | None, pure state report | `MSG_HELLO`, `MSG_WISP_HELLO` |
| **Authenticated commands** | Unicast (or broadcast) | AES-GCM with target's password OR plaintext JSON | NVS-writable; can mutate config | `MSG_CONTROL_OP` |
| **Transient overrides** | Unicast (broadcast for restore) | Plaintext | RAM-only; watchdog-released after 60s | `MSG_OVERRIDE_COLORS/RESTORE_COLORS`, `MSG_OVERRIDE_BRIGHTNESS/RESTORE_BRIGHTNESS` |
| **Expression announce** | Broadcast (no relay) | Plaintext | RAM-only; observer-delivered | `MSG_EVENT` |

**Relay policy** (v0x03):

| msgType | Reach | Relay? | Storm bound |
|---|---|---|---|
| `MSG_HELLO` (0x01) | broadcast | yes, gossip-rebroadcast on first sight | `helloDedup_` 64-slot ring per (sourceMac, seq) |
| `MSG_CONTROL_OP` (0x03) | unicast or broadcast | yes, unconditional after FLAG_LOCAL_ONLY retirement | `controlOpDedup_` 64-slot ring |
| `MSG_WISP_HELLO` (0x20) | broadcast | yes, gossip-rebroadcast | `wispHelloDedup_` 64-slot ring |
| `MSG_WISP_PALETTE` (0x26) | broadcast | yes, gossip-rebroadcast | `wispPaletteDedup_` 64-slot ring |
| `MSG_WISP_PAINT` (0x27) | broadcast | yes, gossip-rebroadcast | `wispPaintDedup_` 64-slot ring |
| `MSG_OVERRIDE_COLORS` (0x21) | unicast | **no**, single-hop, addressedToUs filter | n/a (no relay) |
| `MSG_RESTORE_COLORS` (0x22) | unicast or broadcast | **no**, single-hop | n/a |
| `MSG_OVERRIDE_BRIGHTNESS` (0x23) | unicast | **no**, single-hop | n/a |
| `MSG_RESTORE_BRIGHTNESS` (0x24) | unicast or broadcast | **no**, single-hop | n/a |
| `MSG_EVENT` (0x30) | broadcast | **no**, single-hop nearby-only | `eventDedup_` 64-slot ring |
| `MSG_COMMAND` (0x31) | broadcast (physical); addressedToUs filter on recv | **no** | `commandDedup_` 64-slot ring |

Relay rule: every lamp that successfully parses + dedup-records a relayable frame AND is not the originator (self-MAC drop) rebroadcasts the frame verbatim before any application-level filtering. Per-message-type `DedupRing` instances (64-slot, separate per msgType) bound the storm to ≤ N relays per cascade in an N-lamp mesh.

`OVERRIDE_*` / `RESTORE_*` deliberately stay single-hop. They're unicast by design (wisp paint uses `esp_now_send(targetMac, ...)` with 802.11 driver-level retries; per-link reliability is already strong). Gossip-relay would amplify airtime without obvious benefit because non-addressed receivers drop after the relay step anyway.

## ESP-NOW message catalog

Every frame starts with the same 6-byte header:

```
[MAGIC_0='L'(1)] [MAGIC_1='M'(1)] [PROTOCOL_VERSION(1)] [msgType(1)] [seq(2 LE)]
```

The wire carries a **receive range**, not a single version: `PROTOCOL_VERSION_EMIT = 0x05` is what a node broadcasts; `RX_MIN = 0x04` .. `RX_MAX = 0x05` is what it parses. Splitting emit from receive lets the fleet *receive* a newer version before any node *emits* one — the safe path for a multi-version OTA wave, where mixed versions coexist as long as every node's RX range covers what its peers emit. The v0x03 lock-in established the core wire contract, DedupRing capacity 64, and HELLO interval 5s; v0x04 widened the FW channel slot to 16 bytes for per-variant OTA gating (`{type}-{channel}`); v0x05 added the TLV trailer to HELLO + WISP_HELLO (TLVs: `HELLO_TLV_OTA_STATE`, `HELLO_TLV_FW_CHANNEL`, `HELLO_TLV_FS_STATE`). Bump the version only for a genuine parser-contract change — additive fields ride as TLVs (unknown TLVs are skipped, forward-compat). `inspect()` rejects a frame whose version falls outside `[RX_MIN, RX_MAX]`, so a node emitting outside the fleet's range silently stops showing up — a loud, diagnosable failure by design. The **wisp** is the standing hazard here: it's OTA-excluded, so it never moves forward on its own and goes invisible on the mesh after a bump pushes emit past its RX window, until it's hand-flashed.

**Reserved bits** (must be 0; receivers reject any frame that sets them):

- `kReservedMsgTypeHighBit = 0x80` on the `msgType` byte (`data[3]`). Previously held `FLAG_LOCAL_ONLY` for the cascade-locality hack; retired when cascade migrated off MSG_EVENT. `inspect()` no longer masks the bit, so any future reuse surfaces as an unknown msgType.

### Tier 1: Presence

**`MSG_HELLO` (0x01)**, Lamp presence beacon. Broadcast by every lamp every 5 s (v0x03; was 2s). Pruned from `nearbyLamps` after 120 s of silence.
```
header(6) + sourceMac(6) + shade[4 RGBW] + base[4 RGBW] +
firmwareVersion(4 LE) + nameLen(1) + name[0..32] +
tlv_count(1) + TLV trailer (v0x05+)
= 26..~79 bytes
```
TLV trailer (v0x05+): `tlv_count(1)`, then per TLV `type(1) + len(1) + value(len)`:
- `HELLO_TLV_OTA_STATE` (0x01), len 1: 0=idle / 1=sending / 2=receiving. Emitted only when non-idle.
- `HELLO_TLV_FW_CHANNEL` (0x02), len 16: this lamp's `{type}-{channel}` identity (e.g. `standard-beta`, zero-padded) — same string as the LSIG footer + `MSG_FW_OFFER` channel. The distributor reads it to skip OFFERs at a wrong-type/channel peer.
- `HELLO_TLV_FS_STATE` (0x03), len 8: first 8 bytes of the peer's FS-image manifest digest. Used by FS-OTA to detect peers whose filesystem image differs from the distributor's.

Unknown TLV types are skipped by length (forward-compat); a receiver that doesn't know a type just gets the default for that field.

**`MSG_WISP_HELLO` (0x20)**, Wisp presence beacon. Broadcast by wisp every 2 s via a FreeRTOS software timer (so HELLO cadence survives WiFi/WebSocket blocks).
```
header(6) + sourceMac(6) + wispVersion(4 LE) + flags(1) +
paletteIdPrefix(8 utf-8, null-padded) +
carriedFwChannel(16 utf-8, null-padded) + carriedFwVersion(4 LE)
= 45 bytes  (WISP_HELLO_FIXED_SIZE; channel slot widened 8→16 in v0x04)
flags bit 0 = paintMode, bit 1 = wifiConnected, bit 2 = auroraConnected
```

**`MSG_WISP_PALETTE` (0x26)**, Wisp's canonical manual palette, broadcast for app convergence. The wisp is the source of truth for the manual palette so the same operator gets the same view regardless of which paired lamp they're talking to.
```
header(6) + sourceMac(6) + count(1) + rgb[count*3]
= 13..163 bytes  (count ≤ kMaxWispPaletteColors = 50)
```
- **Sender**: wisp(s) only. Lamps cache + gossip-relay (dedup ring `wispPaletteDedup_`); they never originate.
- **Cadence**: piggybacked on the existing 30 s `emitStatus()` tick, emitted right after `MSG_CONTROL_OP wispStatus` in the same broadcast pass. Also fired on `triggerOnChange()` after a `setManualPalette` wispOp, so app edits propagate within ~2 s.
- **Encoding**: raw R, G, B bytes; W is intentionally dropped (the lamp's headroom math does warm tinting locally).
- **Truncation**: Aurora palettes can exceed 50 colors; the wisp builder caps at `kMaxWispPaletteColors` and logs once per oversize burst (`[wisp.beacon] manualPalette truncated:`). The full palette stays usable on the wisp side for actual paint distribution, only the app's view of the palette gets the cap.
- **Lamp-side cache**: each lamp stores the latest `(mac, rgb[], count)` in `NearbyLamps::WispCache.manualPaletteRgb`. The cache is exposed to the app inside `CHAR_WISP_STATUS` reads as a base64-encoded `manualPalette` field, see the wispStatus envelope below.

**`MSG_WISP_PAINT` (0x27)**, Per-lamp live paint colors, so the app's "Painted lamps" preview shows each lamp's actual color including drift. Drift picks are random per slot (`sampleTupleAtPositions` with `esp_random()`), so the app can't predict them; the wisp remembers each pick in its roster and ships it.
```
header(6) + sourceMac(6) + count(1) + entries[count*12]
  entry: lampMac(6) + baseRGB(3) + shadeRGB(3)
= 13..229 bytes  (count ≤ WISP_PAINT_MAX_ENTRIES = 18)
```
- **Sender**: wisp(s) only. Lamps cache + gossip-relay; they never originate.
- **Cadence**: broadcast right after `MSG_WISP_CLAIM` on the ~2 s presence tick. Drift fades run ~20 s, so 2 s convergence is imperceptibly tight.
- **Encoding**: raw R, G, B per surface (base, shade); W dropped. An all-zero pair is the sentinel for "no live color" (lamp not currently painted).
- **Cap**: the roster tracks up to 32 claimed lamps but one frame carries the first 18 (ESP-NOW frame budget); overflow lamps fall back to the app's `predictTuple` newcomer prediction.
- **Lamp-side cache**: each lamp stores per-lamp `{base, shade, lastSeenMs}` in `NearbyLamps`, aged on the 60 s claim window, and appends it to the `CHAR_WISP_CLAIMS` blob (see below). Membership in that blob is the union of the claim roster and the paint roster so a lamp painted before its claim message arrives is not invisible.

### Tier 2: Authenticated commands

**`MSG_CONTROL_OP` (0x03)**, Authenticated peer command. Unicast or broadcast.
```
header(6) + targetMac(6) + sourceMac(6) + payloadLen(2 LE) + payload(N)
```
`payload` is opaque: AES-GCM ciphertext (target's password) for forwarded BLE writes, OR plaintext JSON tagged with a `char` field (`brightness`, `shadeColors`, `baseColors`, `expressionOp`, `wifiOp`, `knockout`, `wispOp`, `wispStatus`).

CONTROL_OP does not carry `triggerExpression`; directed expression triggers ride MSG_COMMAND.

**Wisp envelopes** (plaintext JSON, broadcast):

`wispStatus`, wisp → fleet, periodic state report.

Off mode (source == "off") — carries `offColor`, no drift fields:
```json
{
  "char":"wispStatus",
  "currentZone": 3,
  "zoneSource": "nvs",
  "observedZones": [0, 3, 7],
  "wifiConnected": true,
  "auroraConnected": true,
  "paletteIdPrefix": "abc12345",
  "lastSeenMs": 14823,
  "source": "off",
  "offColor": [255, 180, 60],
  "name": "living room",
  "hasPassword": true
}
```

Manual/Aurora mode — carries drift fields, no `offColor`:
```json
{
  "char":"wispStatus",
  "currentZone": 3,
  "zoneSource": "nvs",
  "observedZones": [0, 3, 7],
  "wifiConnected": true,
  "auroraConnected": true,
  "paletteIdPrefix": "abc12345",
  "lastSeenMs": 14823,
  "source": "aurora",
  "name": "living room",
  "hasPassword": false,
  "driftIntervalMs": 120000,
  "driftFadePct": 50
}
```
- **Sender**: wisp(s) only. Lamps gossip-relay (per the v0x03 relay rule for `MSG_CONTROL_OP`) but never originate.
- **Cadence**: on-change + 30s heartbeat. Change triggers: zone change, WiFi connect/disconnect, Aurora connect/disconnect. `MSG_WISP_PALETTE` is emitted in the same tick (see Tier 1) so the app's view of the palette converges on the same cadence.
- **`zoneSource`**: `"nvs"` | `"firstSeen"` | `"appOp"` | `"none"`.
- **`source`**: `"aurora"` | `"manual"` | `"off"`. Consumed by the Flutter app to surface and round-trip the source-toggle state.
- **`offColor`**: `[R, G, B]` in 0–255, the ring color used in Off mode. Present only when `source == "off"`. App defaults to warm amber when absent.
- **`driftIntervalMs`**: how often the wisp re-targets each lamp, in milliseconds. Present only when `source != "off"`. App defaults to 120000 when absent.
- **`driftFadePct`**: fade length as a percentage of the drift interval [0..100]. Present only when `source != "off"`. App defaults to 50 when absent.
- **`offColor` / `driftIntervalMs` / `driftFadePct` are mutually exclusive** by mode: the Off-mode offColor picker and the Manual/Aurora drift sliders never appear together in the UI, and this ensures both fit within the 230 B cap.
- **`name`**: wisp display name, ≤ 20 characters. Omitted when empty. Emitted before the droppable fields (`driftIntervalMs`, `driftFadePct`, `observedZones`) so it survives budget pressure, but is dropped as a last resort if needed.
- **`hasPassword`**: `true` when the wisp has a control password set. Non-droppable; always present. A missing field defaults to `false`, which would wrongly assume open access when a password is set.
- **`observedZones`**: capped at 16 entries (oldest-eviction FIFO). When the serialized payload would exceed `CONTROL_MAX_PAYLOAD`, trailing zones are dropped greedily until it fits.
- **`lastSeenMs`**: wisp-local `millis()` at emission. Does not survive wisp reboot, the app does local-epoch math for "X seconds ago" UI rather than trusting this value across reconnects.
- **Payload budget**: guaranteed ≤ 230 B (`CONTROL_MAX_PAYLOAD`) by construction — the builder adds observed zones one at a time and stops before the cap. `manualPalette` is intentionally NOT carried here, it ships via the separate `MSG_WISP_PALETTE` broadcast.
- **Lamp-side cache**: each lamp keeps the latest `wispStatus` per wisp MAC in `NearbyLamps`. `CHAR_WISP_STATUS` reads merge this cache with the last `MSG_WISP_HELLO` snapshot AND the cached manualPalette (base64-encoded `manualPalette` field in the served JSON) for the same MAC.

`wispOp`, app → wisp, control writes proxied through any nearby lamp.

**Wire framing.** The payload written to `CHAR_WISP_OP` (and broadcast verbatim inside `MSG_CONTROL_OP`) is in one of three shapes:
- `0x02` prefix: AES-GCM sealed. `[0x02][12B nonce][16B tag][ciphertext]`. Used when the wisp has a password set. Key derivation: HKDF-SHA256, salt = `uuidSaltLE16(CHAR_WISP_OP)`, info = `"lamp-v1"\0wispOp`, IKM = wisp password. Same framing as lamp control-op sealing.
- `0x01` prefix: explicit plaintext marker. `[0x01][JSON]`. Accepted when no password is set.
- Bare `{`: plaintext JSON at offset 0. Accepted when no password is set (legacy/compat path).

When a password is set, the wisp rejects plaintext (both `0x01`-prefixed and bare) with one exception: `setManualPalette` is always accepted plaintext regardless of password state. This is an **integrity exception**: palette colors are unauthenticated by design, because an attacker in BLE proximity can push override colors anyway; sealing adds nothing under the proximity threat model.

**Replay protection.** The wisp maintains a RAM-only bounded nonce ring (32 entries). A seen nonce is rejected; a forced reboot resets the ring. Config ops are low-value and idempotent, so reboot-replay is an accepted ceiling.

```json
{"char":"wispOp","op":"setZone","zoneId":3}
{"char":"wispOp","op":"clearZone"}
{"char":"wispOp","op":"setWifi","ssid":"...","pw":"..."}
{"char":"wispOp","op":"setDrift","intervalMs":120000,"fadePct":50}
{"char":"wispOp","op":"setName","name":"living room"}
{"char":"wispOp","op":"setPassword","password":"newpass"}
{"char":"wispOp","op":"setManualPalette","colors":[[255,0,0],[0,255,0]]}
```
- **Sender**: the Flutter app writes the payload (sealed or plaintext) to `CHAR_WISP_OP` on any paired lamp; the lamp broadcasts it verbatim as `MSG_CONTROL_OP` for the wisp(s) to consume.
- **Receiver**: wisp(s) only. Lamps gossip-relay but do NOT apply locally, there is no `wispOp` branch in `applyRemoteOpLocal`, by design.
- **Wisp dedup**: the wisp runs its own 64-slot `controlOpDedup_` ring keyed on `(sourceMac, msgType, seq)` so gossip-relayed copies of the same op don't re-apply.
- **NVS persistence**: `setZone` and `clearZone` persist via `WispConfig` (NVS namespace `"wisp"`, key `selZone`). The wisp boots into the persisted zone if one is set.
- **`setWifi`**: persists credentials to NVS and triggers a reconnect attempt. The relay lamp passes the payload through opaque; the wisp handles the WiFi join.
- **`setDrift`**: sets the color-drift cadence. `intervalMs` [30000..3600000]: how often each lamp is re-targeted in milliseconds. `fadePct` [0..100]: fade length as a percentage of the interval. Persisted to NVS; reflected in the next `wispStatus` broadcast under `driftIntervalMs`/`driftFadePct`.
- **`setName`**: sets the wisp's display name, clamped to 20 characters. Persisted to NVS (key `wispName`). Reflected in the next `wispStatus` broadcast under `name`.
- **`setPassword`**: sets or clears the wisp control password. Persisted to NVS (key `wispPassword`). When factory-fresh (no current password), a plaintext `setPassword` is accepted. When a password already exists, `setPassword` must arrive sealed under the OLD password; the decrypt step authenticates the caller before the password is replaced. An empty `"password"` field clears the password (removes the NVS key), reverting to open access.
- **`setManualPalette`**: always accepted plaintext, even when a password is set (integrity exception; see framing note above). Updates the wisp's stored palette; triggers a `MSG_WISP_PALETTE` broadcast.

### Tier 3: Transient overrides

All four override/restore messages share the same prefix through `sourceKind` (bytes 0..19). `fadeDurationMs` follows at offset 20: **4 LE bytes** for `MSG_OVERRIDE_COLORS`, **2 LE bytes** for the other three. Only the tail after the fade differs.

**`MSG_OVERRIDE_COLORS` (0x21)**, Push transient colors onto a renderable surface.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(4 LE) +
numColors(1) + colors[numColors × 4 RGBW]
= 25 + 4N bytes (= 33 for N=2)

surface:        0x01 Base, 0x02 Shade, 0x03 BaseAndShade
sourceKind:     0x01 Wisp, 0x10+ user-defined
fadeDurationMs: u32 LE. 0 = instant snap; otherwise lamp-side
                duration-controlled fade via ConfiguratorBehavior.
                Range up to ~1hr (3,600,000 ms) for wisp color-drift fades.
numColors:      1..8 (kMaxOverrideColorsPerFrame, single source of truth)

surface = 0x03 (BaseAndShade) carries TWO RGBW colors in a single frame:
colors[0] → base, colors[1] → shade. The wisp's PaintDistributor uses
this exclusively, halving ESP-NOW unicast traffic per peer per cycle
(was two separate frames). Atomic delivery: either both surfaces update
or neither does. Eliminated the asymmetric Base-loss / Shade-loss
pattern (measured 31% Base / 15% Shade loss under BLE coex pre-fix).
The lamp's `drainOverrideColors` dispatches colors[0]→baseColorOverride
and colors[1]→shadeColorOverride.
```

**`MSG_RESTORE_COLORS` (0x22)**, Drop colors override, restore baseline.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE)
= 22 bytes

targetMac == FF:FF:FF:FF:FF:FF means "restore on every lamp"
surface    == 0xFF means "restore every surface" (master reset)
sourceKind == 0xFF means "restore from any source" (panic stop)
```

**`MSG_OVERRIDE_BRIGHTNESS` (0x23)**, Push transient brightness 0..100.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE) + brightness(1)
= 23 bytes

surface = 0xFF means "global" (v1 default, applies to all strips)
surface = 0x01 / 0x02 reserved
```

Anti-defeat brightness floor: receivers reject `brightness < 5` from non-paired sources. A "paired" source is one whose MAC matches a `MSG_WISP_HELLO` received within the last 60 s.

**`MSG_RESTORE_BRIGHTNESS` (0x24)**, Drop brightness override.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE)
= 22 bytes
```

**Fade behavior on receivers:**
- Fade happens in `ConfiguratorBehavior::draw` (single source of truth for per-pixel interpolation). Override modules set the target colors + `fadeDurationMs` once at apply-time.
- Mid-fade interrupt: a new OVERRIDE arriving during an in-flight fade snapshots the current interpolated buffer as the new "from", smooth handoff, no visual jerk.
- Watchdog auto-restore (60 s of no refresh) uses a sensible default fade (~1 s) since no sender supplied one.
- Brightness uses a change-driven callback: `BrightnessOverride::tick` invokes the registered callback only when the integer-rounded value actually changes, no per-frame `setBrightness` regression.

### Tier 4: Expression announce

**`MSG_EVENT` (0x30)**, Unauthenticated, nearby-scoped expression-fired announce.

```
[header(6)] [sourceMac(6)] [payload(1..238) — ExpressionInvocation JSON]
```

- **Sender**: any lamp that fires an expression locally, via `MeshLink::sendEvent()`. Emitted for every local fire; not gated on `cascadeEnabled`.
- **Receiver**: delivers to the `ExpressionObserverRegistry` fan-out on Core 1. No auto-action; observers react if registered.
- **No relay**: nearby-scoped by design — lamps only observe expressions they can physically hear.
- **Dedup**: `eventDedup_` 64-slot ring per `(sourceMac, seq)`. Originator pre-records its own seq so the broadcast echo does not re-deliver via observers.
- **Drain**: Core 1 loop via `PendingEvent` slot → `Lamp::drainEvent()` → `ExpressionObserverRegistry::fanOut()`.
- **Payload**: `ExpressionInvocation` JSON (cascade keys stripped). `delayMs` is carried but not acted on by the receiver; observers interpret it as they see fit.

**`MSG_COMMAND` (0x31)**, Targeted expression invocation from one lamp to a specific nearby lamp.

```
[header(6)] [sourceMac(6)] [targetMac(6)] [payload(1..232) — ExpressionInvocation JSON]
```

- **Sender**: any lamp, via `MeshLink::sendCommand()`.
- **Receiver**: applies locally only when `targetMac == myMac || broadcast`. No gossip relay.
- **Dedup**: `commandDedup_` 64-slot ring per `(sourceMac, seq)`.
- **Drain**: Core 1 loop via `PendingCommand` slot → `Lamp::drainCommand()`.
- **Payload**: `ExpressionInvocation` JSON. `delayMs` in the invocation is honored (enqueued to `pendingTriggers` if non-zero). `sourceMac` propagates to `triggerInvocation` for cascade coalescing.
- **Known senders**: cascade fan-out (`ExpressionManager::maybeCascade`); snafu `Greeting::control()` on peer arrival — sends a fast glitch (`type="glitchy"`, `durationMin=durationMax=12`, `target=SHADE`) colored with the sender's stem first color (`config.base.colors[0]`). Only sent when the peer has a known ESP-NOW MAC (`hasMac=true`).

## BLE GATT characteristics (lamp ↔ phone)

Service UUID `5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40`. UUIDs share that base; the `(0xNN)` shorthand below is the distinguishing 4th byte (`5f64f4NN-…`), except `commit` which uses an unrelated UUID. AES-GCM-gated characteristics require a successful `CHAR_AUTH` write within the connection lifetime.

**The layout is a frozen, positional contract.** `kGattLayout` in [`gatt_layout.hpp`](../../software/lamp-os/src/components/network/ble/gatt_layout.hpp) is the single source of truth and lists characteristics in **registration order** — handles are positional, so that order *is* the contract (a boot-time assert in `ble_control.cpp` checks the live registration against it). Grow append-only at the tail or evolve a payload; never insert/remove/reorder/add-a-CCCD without bumping `kGattSchemaVersion` (currently **4**) and re-pinning the hash in `test_ble_gatt_layout`. See the frozen-layout lock-in in `CLAUDE.md`. The table below is in layout order.

**Auth + state notify:**
- `CHAR_AUTH` (0xd1), write: lamp password (ciphertext or plaintext). Gates every auth-gated characteristic for the connection's lifetime.
- `CHAR_STATE_NOTIFY` (0xd8), notify: lamp-driven state-changed notifications. JSON payload includes `previewActive` (bool) and a `greeting` object. `greeting.active=false` when idle; `greeting.active=true` carries `peer` (BD_ADDR string) and `kind` (`"warm"` / `"reserved"` / `"snub"` on standard lamps; `"glitch"` on snafu). Edge-triggered: fires on greeting start and stop. Commit results (`{"commit":"ok"|"err"}`) also arrive on this characteristic.

**Slider-rate write channels (write / no-response, ~30 Hz):**
- `CHAR_BRIGHTNESS` (0xd2), write+wnr: 1 byte 0..100. Routed to `homeMode.brightness` while a Home-Mode edit session is focused.
- `CHAR_SHADE_COLORS` (0xd3), write+wnr: JSON array of hex color strings.
- `CHAR_BASE_COLORS` (0xd4), write+wnr: JSON array of hex color strings.
- `CHAR_BASE_KNOCKOUT` (0xd5), write+wnr: 2 bytes `[pixelIndex, brightness 0..100]`.
- `CHAR_HOME_MODE_FOCUS` (0xe5), write+wnr: 1 byte 0/1 — app sets 1 while the user is on the Home-Mode setup page (forces preview + reroutes brightness). Auto-cleared on disconnect.
- `CHAR_EDIT_SESSION` (0xe9), write+wnr: 2 bytes `[surface, state]` — operator-priority lockout against wisp overrides. `surface` bitmask: 0x01 base, 0x02 shade, 0x04 brightness; `state` != 0 opens.

**Commit:**
- `CHAR_COMMIT` (`48537d49-…`), write+wnr: parameterless commit signal — the *arrival* of the write is the signal (bytes ignored). Flushes pending slider/color writes to NVS off the NimBLE host task; result returns via `CHAR_STATE_NOTIFY`. Also force-flushed on disconnect.

**Op channels (write):**
- `CHAR_EXPRESSION_TEST` (0xd6), write+wnr: utf-8 expression type, empty = complete.
- `CHAR_EXPRESSION_OP` (0xd9), write: `{op: upsert/remove, ...}` JSON.
- `CHAR_WIFI_OP` (0xda), write: `{op: scan/forget}` JSON.
- `CHAR_REMOTE_OP` (0xe4), write: encrypted JSON, forwarded to a far lamp via `MSG_CONTROL_OP`.
- `CHAR_SOCIAL_DISPOSITIONS` (0xe6), read+write: per-peer disposition map (1..5).
- `CHAR_SETTINGS_BLOB` (0xd7), write: full settings JSON (write path only; reads go through the page protocol below).

**Section reads — page protocol (replaces the former per-section read characteristics):**
- `CHAR_PAGE_CTRL` (0xdc), write: section name (`lamp` | `base` | `shade` | `expr` | `home` | `nearby` | `exprcat`). Snapshots that section's cached JSON into a per-connection buffer and resets the read cursor; an optional trailing byte caps the chunk MTU.
- `CHAR_PAGE_DATA` (0xdd), read: returns the next chunk of the snapshot and advances the cursor. The app reads repeatedly until a short chunk (`< kPageMaxChunkSize`) signals end-of-section. Per-connection cursor state, so concurrent phones don't collide.

**Mesh state mirrors:**
- `CHAR_WIFI_STATE` (0xdb), read+notify: JSON snapshot of WiFi/scan state.

**Firmware OTA (phone → lamp):**
- `CHAR_FW_CONTROL` (0xe7), write+notify: app writes `MSG_FW_*` control frames (OFFER/DONE) in lamp_protocol wire format; the lamp's receiver replies via notify on this same characteristic.
- `CHAR_FW_CHUNK` (0xe8), write+wnr: high-frequency `MSG_FW_CHUNK` stream (~200-byte chunks).

**Schema + claims:**
- `CHAR_SCHEMA_VERSION` (0xea), read: single byte = `kGattSchemaVersion`. Absent on legacy lamps that predate it (app falls back to legacy behavior). No app consumer gates on it yet.
- `CHAR_WISP_CLAIMS` (0xeb), read, auth-gated (schema v2, v4 tail): binary blob `[count(1)][lampMac(6)]×count[colorPair(6)]×count`. Membership is the de-duplicated union of the fresh `MSG_WISP_CLAIM` roster and the fresh `MSG_WISP_PAINT` roster (same 60 s staleness window for both); claim-roster macs come first, paint-only macs follow. This means a lamp that arrived via paint before its claim message is not invisible. `count = 0` when both caches are stale. Split from `CHAR_WISP_STATUS` because that payload is already near the MTU budget. The trailing `colorPair` section (`baseRGB(3)+shadeRGB(3)` positionally aligned to each mac, sourced from `MSG_WISP_PAINT`) grew the payload append-only: a reader that stops after `[count][mac×count]` still parses the older format, and an all-zero pair means "no live color, predict instead". A partial or absent color section reads as the legacy mac-only blob.

**Wisp proxy**:
- `CHAR_WISP_OP` (`5f64f4e1-...`), write, auth-gated. Sealed or plaintext payload re-broadcast verbatim as `MSG_CONTROL_OP` for the wisp(s) to consume. The relay lamp passes the payload through opaque; the wisp handles decryption. No new wire-format msg type was added; the routing rides the existing `char`-tagged JSON envelope. Lamps gossip-relay but do not apply locally, `applyRemoteOpLocal` has no `wispOp` branch. See wispOp framing details above.
- `CHAR_WISP_STATUS` (`5f64f4e2-...`), read+notify, auth-gated. Returns the lamp's cached `wispStatus` JSON (sourced from gossip-relayed `MSG_CONTROL_OP` broadcasts the wisp emits) merged with the last `MSG_WISP_HELLO` data on file (`wispMac`, `wispVersion`, `helloFlags`, `helloPaletteIdPrefix`, `helloLastSeenMs`, `statusLastSeenMs`). Notify fires whenever a new wispStatus lands on the lamp.

## Sequence diagrams

### Lamp A triggers an expression (MSG_COMMAND cascade fan-out)

```
lamp A                       mesh                            lamp B (in range)
  │                            │                              │
  │── expression auto-fires    │                              │
  │── ExpressionManager        │                              │
  │   .maybeCascade()          │                              │
  │── build ExpressionInvocation                              │
  │── sort NearbyLamps by RSSI desc                           │
  │── for i, peer in sorted:                                  │
  │     inv.delayMs = (i+1) × cascadeStaggerMs                │
  │     serialize inv to JSON                                 │
  │     sendCommand(peer.mac, json)                           │
  │                            │── ESP-NOW broadcast ────────►│
  │                            │                              │── parse MSG_COMMAND
  │                            │                              │── commandDedup_ check
  │                            │                              │── addressedToUs filter
  │                            │                              │── PendingCommand slot
  │                            │                              │── Core 1 drainCommand:
  │                            │                              │   parseInvocation
  │                            │                              │── delayMs > 0 →
  │                            │                              │   enqueueDelayedInvocation
  │                            │                              │── triggerInvocation(
  │                            │                              │     suppressCascade=true)
```

### Wisp paints a lamp's base + shade (combined frame)

```
wisp                                mesh                    lamp
  │                                   │                       │
  │── Aurora palette change           │                       │
  │── TupleSampler.assign(macA, p)    │                       │
  │   → {baseColor, shadeColor}       │                       │
  │── MSG_OVERRIDE_COLORS unicast ───►│── ESP-NOW unicast ───►│
  │   targetMac=lamp A                │                       │
  │   surface=BaseAndShade (0x03)     │                       │
  │   sourceKind=Wisp                 │                       │
  │   fadeDurationMs=1500             │                       │
  │   numColors=2                     │                       │
  │   colors[0]=base, colors[1]=shade │                       │
  │                                   │                       │── parse, pending slot
  │                                   │                       │── Core 1 drain:
  │                                   │                       │   surface==BaseAndShade →
  │                                   │                       │   baseColorOverride.apply(
  │                                   │                       │     colors[0], numColors=1)
  │                                   │                       │   shadeColorOverride.apply(
  │                                   │                       │     colors[1], numColors=1)
  │                                   │                       │── snapshot config.{base,shade}
  │                                   │                       │   .colors via beginFade
  │                                   │                       │── ConfiguratorBehavior
  │                                   │                       │   ::draw interpolates
  │                                   │                       │   per-pixel for 1.5s
  │                                   │                       │── settles into target
  │                                   │                       │
  │  ...60s no refresh OR             │                       │── tick() watchdog fires
  │     MSG_RESTORE_COLORS arrives    │                       │── restore baseline w/ fade
```

The combined-frame model (`surface=BaseAndShade, numColors=2`) packs
both surfaces into a single ESP-NOW frame. The prior two-unicast model
(Base then Shade, 10 ms apart) suffered asymmetric loss under BLE coex
(Base ~31%, Shade ~15%) because the send-fail callback on the first
frame raced the second send. Combined frames are atomic: both colors
land or neither does.

### Phone picks a wisp zone via mesh proxy

```
phone        lamp A (paired)             mesh                    wisp
  │              │                        │                       │
  │── BLE write ─►│                        │                       │
  │  CHAR_WISP_OP│                        │                       │
  │  {char:wispOp│                        │                       │
  │   op:setZone│                        │                       │
  │   zoneId:3} │                        │                       │
  │              │── MSG_CONTROL_OP ──────►│── ESP-NOW broadcast ─►│
  │              │   payload: same JSON   │                       │
  │              │                        │                       │── controlOpDedup_.record
  │              │                        │                       │── WispOpDispatcher.dispatch
  │              │                        │                       │── WispConfig
  │              │                        │                       │   .setSelectedZone(3)
  │              │                        │                       │   (NVS: "wisp"/selZone)
  │              │                        │                       │── ZoneSelector.setFromOp(3)
  │              │                        │                       │── StatusBeacon
  │              │                        │                       │   .triggerOnChange()
  │              │                        │── ESP-NOW broadcast ◄─│
  │              │                        │   MSG_CONTROL_OP
  │              │                        │   {char:wispStatus,
  │              │                        │    currentZone:3,
  │              │                        │    zoneSource:"appOp",
  │              │                        │    ...}
  │              │── applyRemoteOpLocal ◄─│                       │
  │              │   char=="wispStatus"  │                       │
  │              │── NearbyLamps          │                       │
  │              │   .cacheWispStatus(    │                       │
  │              │     wispMac, json)     │                       │
  │              │── ble_control          │                       │
  │              │   .notifyWispStatus()  │                       │
  │── BLE notify ◄│                        │                       │
  │  CHAR_WISP_  │                        │                       │
  │  STATUS      │                        │                       │
  │  {currentZone:3,                     │                       │
  │   zoneSource:"appOp",...}             │                       │
```

`setWifi` follows the same proxy shape (BLE → `MSG_CONTROL_OP` → wisp dispatch → status broadcast); the wisp-side handler is currently a stub.

## Firmware-distribution messages (`MSG_FW_*`)

These carry the **lamp-to-lamp gossip OTA**: a lamp running newer firmware streams its running image to an out-of-date peer it meets on the mesh. Lamp↔lamp only, the wisp does **not** participate in OTA (it is USB-flash-only).

| Type | Direction | Purpose |
|---|---|---|
| `MSG_FW_OFFER` (0x40) | distributor → peer | "I have firmware version X for you" |
| `MSG_FW_ACCEPT` (0x41) | peer → distributor | "begin streaming" |
| `MSG_FW_CHUNK` (0x42) | distributor → peer | ordered firmware bytes |
| `MSG_FW_REQ` (0x43) | peer → distributor | retransmit request |
| `MSG_FW_DONE` (0x44) | distributor → peer | "verify now" |
| `MSG_FW_RESULT` (0x45) | peer → distributor | terminal status code |

See `software/lamp-os/src/components/firmware/` (distributor + receiver) and [`../../scripts/README.md`](../../scripts/README.md) for the signed-image OTA model.

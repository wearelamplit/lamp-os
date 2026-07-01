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
            ║                  ESP-NOW grid mesh        channel 11   ║
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
| **Event broadcasts** | Broadcast, gossip-relayed | Plaintext | Fire-and-forget (no ACK) | `MSG_EVENT` |

**Relay policy** (v0x03):

| msgType | Reach | Relay? | Storm bound |
|---|---|---|---|
| `MSG_HELLO` (0x01) | broadcast | yes, gossip-rebroadcast on first sight | `helloDedup_` 64-slot ring per (sourceMac, seq) |
| `MSG_CONTROL_OP` (0x03) | unicast or broadcast | yes, unconditional after FLAG_LOCAL_ONLY retirement | `controlOpDedup_` 64-slot ring |
| `MSG_WISP_HELLO` (0x20) | broadcast | yes, gossip-rebroadcast | `wispHelloDedup_` 64-slot ring |
| `MSG_WISP_PALETTE` (0x26) | broadcast | yes, gossip-rebroadcast | `wispPaletteDedup_` 64-slot ring |
| `MSG_OVERRIDE_COLORS` (0x21) | unicast | **no**, single-hop, addressedToUs filter | n/a (no relay) |
| `MSG_RESTORE_COLORS` (0x22) | unicast or broadcast | **no**, single-hop | n/a |
| `MSG_OVERRIDE_BRIGHTNESS` (0x23) | unicast | **no**, single-hop | n/a |
| `MSG_RESTORE_BRIGHTNESS` (0x24) | unicast or broadcast | **no**, single-hop | n/a |
| `MSG_EVENT` (0x30) | broadcast | **yes**, gossip-rebroadcast as of v0x03 (was no) | `eventDedup_` 64-slot ring |

Relay rule: every lamp that successfully parses + dedup-records a relayable frame AND is not the originator (self-MAC drop) rebroadcasts the frame verbatim before any application-level filtering. Per-message-type `DedupRing` instances (64-slot, separate per msgType) bound the storm to ≤ N relays per cascade in an N-lamp mesh.

`OVERRIDE_*` / `RESTORE_*` deliberately stay single-hop. They're unicast by design (wisp paint uses `esp_now_send(targetMac, ...)` with 802.11 driver-level retries; per-link reliability is already strong). Gossip-relay would amplify airtime without obvious benefit because non-addressed receivers drop after the relay step anyway.

## ESP-NOW message catalog

Every frame starts with the same 6-byte header:

```
[MAGIC_0='L'(1)] [MAGIC_1='M'(1)] [PROTOCOL_VERSION(1)] [msgType(1)] [seq(2 LE)]
```

The wire carries a **receive range**, not a single version: `PROTOCOL_VERSION_EMIT = 0x05` is what a node broadcasts; `RX_MIN = 0x04` .. `RX_MAX = 0x05` is what it parses. Splitting emit from receive lets the fleet *receive* a newer version before any node *emits* one — the safe path for a multi-version OTA wave, where mixed versions coexist as long as every node's RX range covers what its peers emit. The v0x03 lock-in established the core wire contract, MSG_EVENT gossip-relays, DedupRing capacity 64, and HELLO interval 5s; v0x04 widened the FW channel slot to 16 bytes for per-variant OTA gating (`{type}-{channel}`); v0x05 added the TLV trailer to HELLO + WISP_HELLO (current TLV: `HELLO_TLV_OTA_STATE`). Bump the version only for a genuine parser-contract change — additive fields ride as TLVs (unknown TLVs are skipped, forward-compat). `inspect()` rejects a frame whose version falls outside `[RX_MIN, RX_MAX]`, so a node emitting outside the fleet's range silently stops showing up — a loud, diagnosable failure by design. The **wisp** is the standing hazard here: it's OTA-excluded, so it never moves forward on its own and goes invisible on the mesh after a bump pushes emit past its RX window, until it's hand-flashed.

**Reserved bits** (must be 0; receivers reject any frame that sets them):

- `kReservedMsgTypeHighBit = 0x80` on the `msgType` byte (`data[3]`). Previously held `FLAG_LOCAL_ONLY` for the cascade-locality hack; retired when cascade migrated to MSG_EVENT. `inspect()` no longer masks the bit, so any future reuse surfaces as an unknown msgType.
- `kStaggerCountReservedHighBit = 0x80` on the `numStaggerEntries` byte (`data[13]` of MSG_EVENT, v0x03 addition). Plausible future use: a scope flag on stagger semantics. `parseEvent` rejects any frame that sets it.

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

### Tier 2: Authenticated commands

**`MSG_CONTROL_OP` (0x03)**, Authenticated peer command. Unicast or broadcast.
```
header(6) + targetMac(6) + sourceMac(6) + payloadLen(2 LE) + payload(N)
```
`payload` is opaque: AES-GCM ciphertext (target's password) for forwarded BLE writes, OR plaintext JSON tagged with a `char` field (`brightness`, `shadeColors`, `baseColors`, `expressionOp`, `wifiOp`, `knockout`, `triggerExpression`, `wispOp`, `wispStatus`).

After the MSG_EVENT cascade migration, CONTROL_OP no longer carries `triggerExpression` announcements.

**Wisp envelopes** (plaintext JSON, broadcast):

`wispStatus`, wisp → fleet, periodic state report.
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
  "offColor": [255, 180, 60]
}
```
- **Sender**: wisp(s) only. Lamps gossip-relay (per the v0x03 relay rule for `MSG_CONTROL_OP`) but never originate.
- **Cadence**: on-change + 30s heartbeat. Change triggers: zone change, WiFi connect/disconnect, Aurora connect/disconnect. `MSG_WISP_PALETTE` is emitted in the same tick (see Tier 1) so the app's view of the palette converges on the same cadence.
- **`zoneSource`**: `"nvs"` | `"firstSeen"` | `"appOp"` | `"none"`.
- **`source`**: `"aurora"` | `"manual"` | `"off"`. Consumed by the Flutter app to surface and round-trip the source-toggle state.
- **`offColor`**: `[R, G, B]` in 0–255, the ring color used in Off mode. Consumed by the Flutter app.
- **`observedZones`**: capped at 16 entries (oldest-eviction FIFO). When the serialized payload would exceed `CONTROL_MAX_PAYLOAD`, trailing zones are dropped greedily until it fits — the fixed fields and `source`/`offColor` are always preserved.
- **`lastSeenMs`**: wisp-local `millis()` at emission. Does not survive wisp reboot, the app does local-epoch math for "X seconds ago" UI rather than trusting this value across reconnects.
- **Payload budget**: guaranteed ≤ 230 B (`CONTROL_MAX_PAYLOAD`) by construction — the builder adds observed zones one at a time and stops before the cap. `manualPalette` is intentionally NOT carried here, it ships via the separate `MSG_WISP_PALETTE` broadcast.
- **Lamp-side cache**: each lamp keeps the latest `wispStatus` per wisp MAC in `NearbyLamps`. `CHAR_WISP_STATUS` reads merge this cache with the last `MSG_WISP_HELLO` snapshot AND the cached manualPalette (base64-encoded `manualPalette` field in the served JSON) for the same MAC.

`wispOp`, app → wisp, control writes proxied through any nearby lamp.
```json
{"char":"wispOp","op":"setZone","zoneId":3}
{"char":"wispOp","op":"clearZone"}
{"char":"wispOp","op":"setWifi","ssid":"...","pw":"..."}
```
- **Sender**: the Flutter app writes the JSON to `CHAR_WISP_OP` on any paired lamp; the lamp broadcasts it verbatim as `MSG_CONTROL_OP` for the wisp(s) to consume.
- **Receiver**: wisp(s) only. Lamps gossip-relay but do NOT apply locally, there is no `wispOp` branch in `applyRemoteOpLocal`, by design.
- **Wisp dedup**: the wisp runs its own 64-slot `controlOpDedup_` ring keyed on `(sourceMac, msgType, seq)` so gossip-relayed copies of the same op don't re-apply.
- **NVS persistence**: `setZone` and `clearZone` persist via `WispConfig` (NVS namespace `"wisp"`, key `selZone`). The wisp boots into the persisted zone if one is set.
- **`setWifi`**: parsed wisp-side; no-op.

### Tier 3: Transient overrides

All four override/restore messages share the same header layout (`sourceMac + targetMac + surface + sourceKind + fadeDurationMs`). Only the tail differs.

**`MSG_OVERRIDE_COLORS` (0x21)**, Push transient colors onto a renderable surface.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE) +
numColors(1) + colors[numColors × 4 RGBW]
= 23 + 4N bytes (= 31 for N=2)

surface:        0x01 Base, 0x02 Shade, 0x03 BaseAndShade
sourceKind:     0x01 Wisp, 0x10+ user-defined
fadeDurationMs: u16 0..65535. 0 = instant snap; otherwise lamp-side
                duration-controlled fade via ConfiguratorBehavior.
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

### Tier 4: Event broadcasts

**`MSG_EVENT` (0x30)**, Plaintext broadcast announcement. Open-set `eventKind`.
```
header(6) + sourceMac(6) + eventKind(1) +
numStaggerEntries(1) + staggerEntries[N × (mac(6) + delayMs(2 LE))] +
payloadLen(2 LE) + payload(N bytes)

eventKind catalog (open-set, unknown kinds silently dropped):
  0x01 = expression-triggered (payload = ExpressionInvocation JSON)
  0x02..0x0F = reserved
  0x10..0xFF = user-defined

numStaggerEntries: capped at 12 (kMaxStaggerEntries).
                   Receiver bounds-checks numStaggerEntries × 8 +
                   payloadLen + header ≤ frame length.
staggerEntries[].delayMs: clamped to kMaxDelayMs = 10000 on receive.
```

**Stagger semantics:** the sender pre-computes per-peer delays, sorted by RSSI descending (strongest signal first → physically closest → fires earliest in the wave). Each peer's `delayMs = (position + 1) × cascadeStaggerMs`. The `(position + 1)` offset means the closest peer fires `cascadeStaggerMs` after the sender, not at the same instant, without the offset, a 2-lamp mesh would fire simultaneously regardless of `cascadeStaggerMs` and the "wave from the trigger source outward" UX is lost.

**Receiver flow** (v0x03, in order, early-out at any step):
1. Dedup by `sourceMac + seq` via `eventDedup_` ring (64-slot in v0x03).
2. Drop if `sourceMac == myMac` (own broadcast).
3. **Gossip-relay**: `link_.broadcast(data, len)`. Verbatim rebroadcast. Runs BEFORE the eventKind filter so unknown-but-well-formed kinds still propagate through the mesh, forward-compat for future EventKind additions.
4. Drop if `eventKind` unknown (today: anything other than `ExpressionTriggered`).
5. Cheap byte-scan the payload for `"type":"..."` (no JsonDocument yet), used as the `recentCascades_` dedup key.
6. Check `recentCascades_` dedup.
7. Look up own MAC in `staggerEntries`; if found use its `delayMs`, otherwise tail-fire at `numStaggerEntries × 50ms`.
8. Full `parseInvocation` and `triggerInvocation(suppressCascade=true)` with `fireAtMs = millis() + clampedDelayMs`.

**Cascade is sender-authoritative.** Receivers fire whatever the sender announces, the wire payload carries the full invocation (`type`, `target`, `colors`, `parameters`) and a fresh transient Expression is built directly from it. The receiver's local expression config (its own `expressions` vector, including its own `cascadeEnabled` setting for the same type) is intentionally irrelevant.

**Reliability strategy (v0x03):**
1. **Sender emits MSG_EVENT twice back-to-back** (no inter-send delay) inside `ExpressionManager::maybeCascade`. ESP-NOW broadcasts have no link-layer ACK, so the duplicate is best-effort insurance against a single dropped frame from RF contention (BLE adv burst, brief channel noise). No inter-send `delay()`: any gap stalls the sender's Core 1 render pipeline and the sender's own LEDs visibly lag receivers'. Back-to-back loses the across-window spread but keeps the two-TX-attempts resilience without blocking.
2. **Gossip-relay through the mesh** (v0x03 addition). Every lamp that receives a MSG_EVENT for the first time and isn't the originator rebroadcasts it. A BLE-coex'd originator (IDF #14904 SW-coex packet loss) no longer single-points the cascade: any peer that hears one of the two original broadcasts amplifies into the rest of the mesh. Storm bounded by the per-msgType DedupRing (64 slots, separate per type).
3. **HW coex** at the radio layer (Espressif IDF flag, see `software/lamp-os/platformio.ini`). HW coex avoids the SW-coex starvation that caused the underlying 22% baseline reliability.

The DedupRing collapses by `(sourceMac, seq)` so dispatch only fires once per cascade per receiver regardless of how many gossip copies arrive.

## BLE GATT characteristics (lamp ↔ phone)

Service UUID `5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40`. UUIDs share that base; the `(0xNN)` shorthand below is the distinguishing 4th byte (`5f64f4NN-…`), except `commit` which uses an unrelated UUID. AES-GCM-gated characteristics require a successful `CHAR_AUTH` write within the connection lifetime.

**The layout is a frozen, positional contract.** `kGattLayout` in [`gatt_layout.hpp`](../../software/lamp-os/src/components/network/ble/gatt_layout.hpp) is the single source of truth and lists characteristics in **registration order** — handles are positional, so that order *is* the contract (a boot-time assert in `ble_control.cpp` checks the live registration against it). Grow append-only at the tail or evolve a payload; never insert/remove/reorder/add-a-CCCD without bumping `kGattSchemaVersion` (currently **2**) and re-pinning the hash in `test_ble_gatt_layout`. See the frozen-layout lock-in in `CLAUDE.md`. The table below is in layout order.

**Auth + state notify:**
- `CHAR_AUTH` (0xd1), write: lamp password (ciphertext or plaintext). Gates every auth-gated characteristic for the connection's lifetime.
- `CHAR_STATE_NOTIFY` (0xd8), notify: lamp-driven state-changed notifications, including the `{"commit":"ok"|"err"}` result after a commit.

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
- `CHAR_PAGE_CTRL` (0xdc), write: section name (`lamp` | `base` | `shade` | `expr` | `home`). Snapshots that section's cached JSON into a per-connection buffer and resets the read cursor; an optional trailing byte caps the chunk MTU.
- `CHAR_PAGE_DATA` (0xdd), read: returns the next chunk of the snapshot and advances the cursor. The app reads repeatedly until a short chunk (`< kPageMaxChunkSize`) signals end-of-section. Per-connection cursor state, so concurrent phones don't collide.

**Mesh state mirrors:**
- `CHAR_WIFI_STATE` (0xdb), read+notify: JSON snapshot of WiFi/scan state.

**Firmware OTA (phone → lamp):**
- `CHAR_FW_CONTROL` (0xe7), write+notify: app writes `MSG_FW_*` control frames (OFFER/DONE) in lamp_protocol wire format; the lamp's receiver replies via notify on this same characteristic.
- `CHAR_FW_CHUNK` (0xe8), write+wnr: high-frequency `MSG_FW_CHUNK` stream (~200-byte chunks).

**Schema + claims (tail):**
- `CHAR_SCHEMA_VERSION` (0xea), read: single byte = `kGattSchemaVersion`. Absent on legacy lamps that predate it (app falls back to legacy behavior). No app consumer gates on it yet.
- `CHAR_WISP_CLAIMS` (0xeb), read, auth-gated (schema v2 tail): binary blob `[count(1)][lampMac(6)]×count` of lamps claimed by a wisp; `count = 0` when no claim heard in 60 s. Split from `CHAR_WISP_STATUS` because that payload is already near the MTU budget.

**Wisp proxy**:
- `CHAR_WISP_OP` (`5f64f4e1-...`), write, auth-gated. Plaintext JSON `{"char":"wispOp","op":"...","..."}` re-broadcast verbatim as `MSG_CONTROL_OP` for the wisp(s) to consume. No new wire-format msg type was added; the routing rides the existing `char`-tagged JSON envelope. Lamps gossip-relay but do not apply locally, `applyRemoteOpLocal` has no `wispOp` branch.
- `CHAR_WISP_STATUS` (`5f64f4e2-...`), read+notify, auth-gated. Returns the lamp's cached `wispStatus` JSON (sourced from gossip-relayed `MSG_CONTROL_OP` broadcasts the wisp emits) merged with the last `MSG_WISP_HELLO` data on file (`wispMac`, `wispVersion`, `helloFlags`, `helloPaletteIdPrefix`, `helloLastSeenMs`, `statusLastSeenMs`). Notify fires whenever a new wispStatus lands on the lamp.

## Sequence diagrams

### Lamp A triggers an expression (post-MSG_EVENT cascade)

```
lamp A                       mesh                            lamp B (in range)
  │                            │                              │
  │── expression auto-fires    │                              │
  │── ExpressionManager        │                              │
  │   .triggerInvocation()     │                              │
  │── build ExpressionInvocation                              │
  │── sort NearbyLamps by RSSI desc                           │
  │── compute stagger:                                        │
  │   for i, peer in sorted:                                  │
  │     entries[i] = (peer.mac, i × cascadeStaggerMs)         │
  │── emit MSG_EVENT broadcast│                               │
  │   kind=expression-triggered                               │
  │   staggerEntries=[(macB,0),(macC,200),(macD,400),...]    │
  │   payload=invocation JSON  │                              │
  │── emit again, back-to-back│                               │
  │                            │── ESP-NOW broadcast ────────►│
  │                            │                              │── parse MSG_EVENT
  │                            │                              │── dedup + drop own
  │                            │                              │── peek "type" in JSON
  │                            │                              │   (dedup key only, 
  │                            │                              │    no local-config consult)
  │                            │                              │── lookup own MAC in
  │                            │                              │   staggerEntries
  │                            │                              │   → index 0, delay=0
  │                            │                              │── recentCascades dedup
  │                            │                              │── full parseInvocation
  │                            │                              │── triggerInvocation(
  │                            │                              │     suppress=true,
  │                            │                              │     fireAt=now+0)
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

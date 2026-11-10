# Lamp OS networking reference

This document describes the communication protocols that move data between the
lamps, the wisp infrastructure node, the Flutter app, and the Straylight
palette feed (OSC) — the **ESP-NOW mesh** between lamps, the **BLE GATT** link to the phone,
and the firmware-distribution messages. It is the authoritative wire-format
reference; if code and this doc disagree, the code wins and this doc should be
updated.

## Topology

```
                                 ╔═══════════════════════════╗
                                 ║        Straylight         ║
                                 ║      (palette source)     ║
                                 ╚════════════╤══════════════╝
                                              │ OSC 1.0
                                              │ SLIP over USB serial, or
                                              │ UDP :9000 (softAP / STA / ETH)
                                              ▼
            ┌──────────────────────────────────────────────────────┐
            │                       wisp                           │
            │              Seeed Xiao ESP32-C6, external antenna   │
            │                                                      │
            │   OscListener    ──►  CurrentPalette                 │
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
| **Presence** | Broadcast (lamp: 30s, wisp: 2s) | Plaintext | None, pure state report | `MSG_HELLO`, `MSG_WISP_HELLO` |
| **Wisp control** | Unicast (lamp → wisp) or broadcast (wisp → lamps) | Sealed wispOp OR plaintext JSON | Wisp-side NVS; lamps only cache what the wisp reports | `MSG_CONTROL_OP` |
| **Transient overrides** | Unicast (broadcast for restore) | Plaintext | RAM-only; watchdog-released after 60s | `MSG_OVERRIDE_BRIGHTNESS/RESTORE_BRIGHTNESS` (space-dim). Wisp paint no longer rides `MSG_OVERRIDE_COLORS/RESTORE_COLORS`; it's declared in `MSG_WISP_STATE`. |
| **Expression announce / directed** | No relay: EVENT nearby-broadcast, COMMAND targeted (addressedToUs filter on recv) | 8-byte HMAC-SHA256 shared-key tag (`command_auth`) | RAM-only; observer-delivered / applied | `MSG_EVENT`, `MSG_COMMAND` |

**Relay policy:**

| msgType | Reach | Relay? | Storm bound |
|---|---|---|---|
| `MSG_HELLO` (0x01) | broadcast | yes, gossip-rebroadcast, counter-suppressed (see below) | `helloDedup_` 64-slot ring per (sourceMac, seq) + `HelloRelaySuppressor` 16-slot pending table |
| `MSG_CONTROL_OP` (0x03) | unicast or broadcast | yes, unconditional | `controlOpDedup_` 64-slot ring |
| `MSG_WISP_HELLO` (0x20) | broadcast | one hop, relayed only when heard direct from the wisp | `wispHelloDedup_` 32-slot ring |
| `MSG_WISP_CLAIM` (0x25) | broadcast | **no**, direct radio range only | `wispClaimDedup_` 16-slot ring |
| `MSG_WISP_PALETTE` (0x26) | broadcast | one hop, relayed only when heard direct from the wisp | `wispPaletteDedup_` 32-slot ring |
| `MSG_WISP_PAINT` (0x27) | broadcast | **no**, ignored on receive | n/a — **retired**, wisp no longer emits it; STATE carries per-lamp colors |
| `MSG_WISP_STATE` (0x28) | broadcast | **no**, direct radio range only | `wispStateDedup_` 16-slot ring — sole wisp paint render path |
| `MSG_OVERRIDE_COLORS` (0x21) | unicast | **no**, single-hop, addressedToUs filter | n/a (no relay) — **retired** as the wisp paint path; parser kept, no sender |
| `MSG_RESTORE_COLORS` (0x22) | unicast or broadcast | **no**, single-hop | n/a — **retired**, STATE-absence eases a lamp home |
| `MSG_OVERRIDE_BRIGHTNESS` (0x23) | unicast | **no**, single-hop | n/a |
| `MSG_RESTORE_BRIGHTNESS` (0x24) | unicast or broadcast | **no**, single-hop | n/a |
| `MSG_EVENT` (0x30) | broadcast | **no**, single-hop nearby-only | `eventDedup_` 32-slot ring |
| `MSG_COMMAND` (0x31) | broadcast (physical); addressedToUs filter on recv | **no** | `commandDedup_` 64-slot ring |
| `MSG_COLOR_QUERY` (0x32) | broadcast (physical); addressedToUs filter on recv | **no** | n/a (single-hop) |
| `MSG_COLOR_INFO` (0x33) | broadcast (physical); addressedToUs filter on recv | **no** | n/a (single-hop) |

Relay rule: every lamp that successfully parses + dedup-records a relayable frame AND is not the originator (self-MAC drop) rebroadcasts the frame verbatim before any application-level filtering. `MSG_CONTROL_OP` relays unconditionally.

`MSG_HELLO` relay is **counter-suppressed** (receive-side only, no wire change). A first-seen HELLO is not relayed immediately; `HelloRelaySuppressor` enqueues it in a 16-slot pending table keyed on `(sourceMac, seq)` with a randomized fire delay (`kHelloRelayJitterMinMs`..`kHelloRelayJitterMaxMs`, derived deterministically from the mac+seq). Every duplicate `(sourceMac, seq)` heard before the delay elapses — the frames that `helloDedup_` would otherwise silently drop — increments that entry's `dupCount`; each duplicate is one neighbor that already relayed the beacon. At fire time (`MeshLink::tick`) an entry with `dupCount >= kHelloSuppressThreshold` (3) is dropped, since the mesh already covered it; otherwise the stored frame is relayed verbatim. This pulls total HELLO airtime down from ~N²/interval (every node relaying every beacon) toward the coverage the mesh actually needs, without RSSI gating. Table overflow **fails open** (relay immediately) so a burst never loses coverage. A suppressing node simply transmits less; old-firmware peers that relay unconditionally still interoperate, and the relayed bytes are byte-identical to what arrived. Under `LAMP_DEBUG` each lamp prints a 30 s `[hellosupp] win=30s suppressed=N relayed=M rate=XX%` line (piggybacked on the `[meshmix]` window) so the kill rate is directly observable on the bench. `MSG_WISP_HELLO` and `MSG_WISP_PALETTE` relay one hop: a lamp rebroadcasts them only when the frame transmitter equals the originator wisp (heard direct), so a relayed copy (`srcMac != sourceMac`) is not re-relayed. This carries wisp presence/palette to lamps one hop past the wisp's own radio range so the app's wisp view converges across paired lamps despite coex-dropped broadcasts, while bounding propagation to exactly one hop. Remaining wisp traffic (`CLAIM`, `STATE`, `OVERRIDE_BRIGHTNESS`) stays direct-only. Per-message-type `DedupRing` instances (separate per msgType, each sized to its traffic — 64 slots for relay-heavy types, fewer for single-hop / low-rate ones) bound the storm to ≤ N relays per cascade in an N-lamp mesh.

`OVERRIDE_BRIGHTNESS` / `RESTORE_BRIGHTNESS` deliberately stay single-hop. They're unicast by design (`esp_now_send(targetMac, ...)` with 802.11 driver-level retries; per-link reliability is already strong). Gossip-relay would amplify airtime without obvious benefit because non-addressed receivers drop after the relay step anyway.

## ESP-NOW message catalog

Every frame starts with the same 6-byte header:

```
[MAGIC_0='L'(1)] [MAGIC_1='M'(1)] [PROTOCOL_VERSION(1)] [msgType(1)] [seq(2 LE)]
```

The wire carries a **receive range**, not a single version: `PROTOCOL_VERSION_EMIT = 0x05` is what a node broadcasts; `RX_MIN = 0x04` .. `RX_MAX = 0x05` is what it parses. Splitting emit from receive lets the fleet *receive* a newer version before any node *emits* one — the safe path for a multi-version OTA wave, where mixed versions coexist as long as every node's RX range covers what its peers emit. The v0x05 emit carries a TLV trailer on HELLO + WISP_HELLO (TLVs: `HELLO_TLV_OTA_STATE`, `HELLO_TLV_FW_CHANNEL`, `HELLO_TLV_FS_STATE`, `HELLO_TLV_FW_MAX_CHUNK`, `HELLO_TLV_OTA_SENDING_TO`, `HELLO_TLV_VARIANT`); v0x04 frames omit it and parsers accept both. Per-message-type DedupRing capacities are sized per traffic (receive-side state, not a wire contract — a resize needs no version bump) and the HELLO interval is 30 s. Bump the version only for a genuine parser-contract change — additive fields ride as TLVs (unknown TLVs are skipped, forward-compat). `inspect()` rejects a frame whose version falls outside `[RX_MIN, RX_MAX]`, so a node emitting outside the fleet's range silently stops showing up — a loud, diagnosable failure by design. The **wisp** is the standing hazard here: it's OTA-excluded, so it never moves forward on its own and goes invisible on the mesh after a bump pushes emit past its RX window, until it's hand-flashed.

**Reserved bits** (must be 0; receivers reject any frame that sets them):

- `kReservedMsgTypeHighBit = 0x80` on the `msgType` byte (`data[3]`). `inspect()` does not mask the bit, so any frame that sets it surfaces as an unknown msgType.

### Tier 1: Presence

**`MSG_HELLO` (0x01)**, Lamp presence beacon. Broadcast by every lamp every 5 s for the first 30 s of uptime (boot burst, so a missed first HELLO refills the roster in seconds), then every 30 s. Pruned from `lampRoster` after 240 s of silence.
```
header(6) + sourceMac(6) + shade[4 RGBW] + base[4 RGBW] +
firmwareVersion(4 LE) + nameLen(1) + name[0..32] +
tlv_count(1) + TLV trailer (v0x05+)
= 26..93 bytes
```
`shade` and `base` each carry their gradient's blended-identity color (`lampos::led::blendedIdentity`), a linear-light weighted blend of all stops rather than a single indexed stop.

TLV trailer (v0x05+): `tlv_count(1)`, then per TLV `type(1) + len(1) + value(len)`:
- `HELLO_TLV_OTA_STATE` (0x01), len 1: 0=idle / 1=sending / 2=receiving. Emitted only when non-idle.
- `HELLO_TLV_FW_CHANNEL` (0x02), len 16: this lamp's `{type}-{channel}` identity (e.g. `standard-beta`, zero-padded) — same string as the LSIG footer + `MSG_FW_OFFER` channel. The distributor uses it via `otaAcceptable` to gate offers (see Channel promotion below).
- `HELLO_TLV_FS_STATE` (0x03), len 8: first 8 bytes of the peer's FS-image manifest digest. Used by FS-OTA to detect peers whose filesystem image differs from the distributor's.
- `HELLO_TLV_FW_MAX_CHUNK` (0x04), len 2: this lamp's largest acceptable `MSG_FW_CHUNK` payload (LE, `FW_CHUNK_SIZE_MAX` = 1444). The firmware distributor negotiates the session's chunk size from it: `min(FW_CHUNK_SIZE_MAX, peer's advertised value)` when present, else `FW_CHUNK_SIZE_BASELINE` (200) — the floor every OTA receiver, old or new, accepts.
- `HELLO_TLV_NEED_FS` (0x05), len 1: set when this lamp is FS-capable but has no valid local FS digest (SPIFFS empty/unmountable), so it can't emit `HELLO_TLV_FS_STATE`. Distinguishes "needs an FS image" from "legacy/FS-disabled" so the FS distributor offers to it (the offer is version-coupled to the distributor's firmware, which the peer already runs). Re-evaluated every HELLO; cleared once a valid FS is present.
- `HELLO_TLV_OTA_SENDING_TO` (0x06), len 6: the mesh MAC of the peer this lamp is currently OTA-distributing firmware to. Emitted only while sending (single-peer distributor), alongside `HELLO_TLV_OTA_STATE` = sending. Lets the app name the send edge and mark the receiver, which is HELLO-silent during its own OTA. Absent on idle lamps and older firmware.
- `HELLO_TLV_VARIANT` (0x07), len 1: the sender's `LampVariant` — `Unknown=0, Standard=1, Snafu=2, Staff=3, Lioness=4, Loaf=5` (append-only; a new variant takes the next value). Lets a variant-aware behavior react to what kind of lamp a peer is (surfaced as `PeerView::variant`). Absent on legacy / BLE-only peers, which read as `Unknown`. Additive TLV — **no `PROTOCOL_VERSION` bump** (unknown TLV skipped by older parsers).

Unknown TLV types are skipped by length (forward-compat); a receiver that doesn't know a type just gets the default for that field.

**`MSG_WISP_HELLO` (0x20)**, Wisp presence beacon. Broadcast by wisp every 2 s. A FreeRTOS software timer flags the emit due on the 2 s cadence; `PresenceBeacon::pump()` on the loop task does the build + broadcast, off the 2 KB timer stack (so a busy loop pass delays the actual send). Liveness only: presence, version, and flags for the app. It does not hold a lamp's colour paint alive — that hold rides `MSG_WISP_STATE` freshness (see `MSG_WISP_STATE` below).
```
header(6) + sourceMac(6) + wispVersion(4 LE) + flags(1) +
paletteIdPrefix(8 utf-8, null-padded) +
carriedFwChannel(16 utf-8, null-padded) + carriedFwVersion(4 LE) +
tlv_count(1) + TLV trailer (v0x05+)
= 45 bytes fixed (WISP_HELLO_FIXED_SIZE); wisp emits 46 (tlv_count=0),
  cap WISP_HELLO_MAX_SIZE = 96
flags bit 0 = paintMode, bit 1 = wifiConnected, bit 2 = sourceLive
```
Bit 2 (`WISP_HELLO_FLAG_SOURCE_LIVE`) is the same signal as wispStatus
`sourceLive`: Osc mode, a non-blank palette held, and something from the
Straylight feed within the last 30 s. It reuses the bit position the retired
Aurora source used for "connected", with no protocol version bump: no
receiver gates on bit 2 (lamps ignore the flags byte; the app reads the same
signal from wispStatus `sourceLive`, and `helloFlags` is only passed through),
so no parser contract changed.
The TLV trailer shares HELLO's shape (`tlv_count(1)`, then `type(1) + len(1)
+ value(len)` per TLV); the wisp emits `tlv_count=0` and the parser skips
unknown types by length (forward-compat).
- **Sender**: wisp(s) only. Lamps adopt into the display slot on receipt, whether heard direct or via a one-hop relay; only a direct frame is re-relayed (one hop). Lamps never originate.

**`MSG_WISP_CLAIM` (0x25)**, Wisp claim broadcast: the lamps this wisp currently paints, at the RSSI it hears each one. Peer wisps consume it for boundary arbitration; lamps accumulate the entries for `CHAR_WISP_CLAIMS` and display-slot admission.
```
header(6) + sourceMac(6) + count(1) + entries[count*7]
  entry: lampMac(6) + int8 rssi(1)
= 13..713 bytes  (count ≤ kMaxWispClaimEntries = 100)
```
- **Sender**: wisp(s) only, keyed to the 2 s presence tick. `CLAIM` goes out with the HELLO on odd ticks (every ~4 s). `MSG_WISP_STATE` goes out every tick (~2 s) at a random 200 to 800 ms offset after the HELLO, in a loop pass of its own, so two fat frames never go out in one pass on the single core (see `MSG_WISP_STATE` cadence). A paint-mode edge also emits `MSG_WISP_STATE` out of cadence for a prompt on/off. No gossip relay (direct radio range only).
- **Full-set frame**: one v2 frame carries the whole claim set (up to `LampInventory::MAX_LAMPS` = 100 = the receiver's `WispFleetCache` capacity), so every claimed lamp refreshes each tick — no windowing, no rotation cursor. Past 100 claimed the extras are truncated cleanly (unreachable on one wisp).
- **Admission**: a wisp claims every lamp it directly hears (a real RSSI measurement). Lamps it never hears itself are never claimed.
- **Arbitration**: peer entries age out after 10 s of silence; ±5 dB RSSI hysteresis with last-owner stickiness; lower MAC wins a simultaneous-claim tiebreak.

**`MSG_WISP_PALETTE` (0x26)**, Wisp's canonical manual palette, broadcast for app convergence. The wisp is the source of truth for the manual palette so the same operator gets the same view regardless of which paired lamp they're talking to.
```
header(6) + sourceMac(6) + count(1) + rgb[count*3] + w[count]
= 13..213 bytes  (count ≤ kMaxWispPaletteColors = 50)
```
- **Sender**: wisp(s) only. Lamps cache on receipt (dedup ring `wispPaletteDedup_`) and re-relay one hop only when heard direct; they never originate.
- **Cadence**: piggybacked on the existing 30 s `emitStatus()` tick, emitted right after `MSG_CONTROL_OP wispStatus` in the same broadcast pass. Also fired on `triggerOnChange()` after a `setManualPalette` wispOp, so app edits propagate within ~2 s.
- **Encoding**: a packed R, G, B block followed by a per-color W plane. The plane is a length-gated trailer: `parseWispPalette` accepts frames longer than `count*3` (it always has), so parsers that read only the RGB block skip it, and frames without it parse with `w == nullptr` (W reads as 0). W is **warm white** — the lamp grid's dedicated warm emitter, "amber" in ArtNet fixture terms. RGB-only surfaces (the wisp's WS2812 ring, the app's gradient bar) fold W back into RGB with the shared warm-bias math.
- **Manual only**: the frame carries the stored manual palette in every source mode. An OSC palette is never broadcast here, so in Osc mode the app has no copy of the live palette beyond `paletteIdPrefix`.
- **Lamp-side cache**: each lamp stores the latest `(mac, rgbw[], count)` in `LampRoster::WispCache.manualPaletteRgbw` (interleaved R,G,B,W). The app reads it raw from the `wisppalette` page section (see the page protocol below), off the status, whose frequent palette-less notify would clobber it.

**`MSG_WISP_PAINT` (0x27)**, **retired**. The wisp no longer emits it; `MSG_WISP_STATE` carries the same per-lamp base/shade colors and feeds the app's "Painted lamps" preview. The wire format (`header(6) + sourceMac(6) + count(1) + entries[count*12]`, entry `lampMac(6) + baseRGB(3) + shadeRGB(3)`) stays documented and the type ID stays reserved, because a wisp older than beta-1.1.8 still broadcasts it. A lamp ignores the frame on receive: no parse, no dedup, no slot.

**`MSG_WISP_STATE` (0x28)**, Declarative per-lamp state: base+shade per claimed lamp plus the wisp's per-frame globals — the **sole** wisp paint render path. A lamp gates the frame to its display wisp (`sourceMac` match), then scans for its own mesh MAC: on a hit it edge-triggers the eased wisp `LayerStack` base/shade targets and holds presence; on a miss in a fresh frame (Off, or unclaimed) it eases home promptly. Every entry also feeds the app's painted-lamps preview (`WispFleetCache`), the role `MSG_WISP_PAINT` used to fill. `brightness` / `driftRateMs` are carried but not yet consumed by the lamp render path (space-dim still rides `MSG_OVERRIDE_BRIGHTNESS`).
```
header(6) + sourceMac(6) + brightness(1) + driftRateMs(4 LE) +
presenceFlags(1) + count(1) + entries[count*12] + w[count*2]
  entry: lampMac(6) + baseRGB(3) + shadeRGB(3)
  w:     baseW(1) + shadeW(1) per entry, in entry order (WISP_STATE_W_SIZE)
= 19..1461 bytes  (count ≤ WISP_STATE_MAX_ENTRIES = 103)
```
- **Sender**: wisp(s) only. In Off (paint disabled), and in Osc mode while the stream is stale, the wisp packs zero entries, so every claimed lamp finds its MAC absent and eases home. While painting, `WispRoster::snapshotStateForBroadcast` packs every claim whose `painted` flag `setLampPaint` has set, whatever the colour, so a lamp painted black paints black. A fresh claim with no colour assigned yet is omitted, so that lamp holds its own scene until its colour lands. No gossip relay (direct radio range only); it re-covers the whole claimed set on its own cadence.
- **Off / presence**: presence holds while STATE from the display wisp keeps naming this lamp. A fresh STATE that drops the lamp eases it home now; total STATE silence ages presence out after `kWispStateFreshMs` (60 s) as a failsafe. A paint-mode edge emits STATE out of cadence, on the next free loop pass, so on/off lands within a frame or two of the change.
- **Cadence**: every presence tick (~2 s), at a uniformly random offset after the HELLO, drawn fresh each tick (`stateTickOffsetMs` over `kStateOffsetMinMs`..`kStateOffsetMaxMs`, 200 to 800 ms, in `presence_beacon.hpp`), so a receiver's periodic deaf window cannot phase-lock against a fixed send time. `PresenceBeacon::emit()` schedules it in a `TickStateSchedule` that holds one pending STATE: a STATE still unsent when the next HELLO fires is replaced by that tick's. No resend ring: at up to 1461 B it overflows the claim-sized resend slot. Edge coverage instead comes from a burst window (`kStateBurstMs` ≈ 2 s, `kStateBurstIntervalMs` ≈ 700 ms in `presence_beacon.hpp`): a paint-mode edge re-emits STATE a few times over ~2 s so a take/release lost to coex is caught without waiting the full 2 s re-cover, which remains the backstop.
- **One fat frame per loop pass**: every STATE (paint-edge, burst copy and steady tick) waits in `PresenceBeacon::pump()` (`nextStateSend`) for a loop pass that sent no other fat frame: no HELLO, no CLAIM, no resend copy, and no `StatusEmitter::pump()` emit (wispStatus with its `MSG_WISP_PALETTE`, or wispConfig; `pump()` returns whether it ran one of those emits, and `loop()` passes that in). At most one STATE goes out per pass, the edge first, then a due burst copy, then the steady tick STATE, which waits for the next free pass when one of the others takes this one.
- **Per-frame globals**: `brightness` (space brightness), `driftRateMs` (drift interval, matches `WispConfig::driftIntervalMs`), `presenceFlags` (the same bitfield as this tick's `MSG_WISP_HELLO` flags, incl `WISP_HELLO_FLAG_PAINT_MODE`).
- **Encoding**: raw R, G, B per surface (base, shade) in each entry; W rides the W plane after the entries. `buildWispState` appends the plane when given a W buffer (the wisp always passes one when it packs entries). `parseWispState` sets `ParsedWispState::w` only when the frame is long enough for the whole plane, else `nullptr`, and the lamp reads W = 0 (`fillPendingWispState` / `wispStateColorsFor` in `pending_slots.hpp`). A parser that reads only `count*12` entry bytes ignores the plane, so the trailer needs no protocol version bump.
- **Full-set frame**: one v2 frame carries the whole painted set; no windowing. `WISP_STATE_MAX_ENTRIES` (103) is derived as the most entries plus W plane that fit `ESPNOW_V2_FRAME_MAX` (1470), which keeps it above the wisp's claim cap (`WISP_ROSTER_MAX_LAMPS` = 100). Past the cap the extras truncate cleanly.
- **Lamp-side cache**: each lamp accumulates per-lamp `{base, shade, lastSeenMs}` entries in `WispFleetCache` (capacity 100) from STATE entries (`LampRoster::cacheWispState` takes the drained STATE slot; base and shade are RGBW, W from the W plane, W = 0 when the frame has no plane), upserting per MAC with per-entry staleness eviction on the 60 s claim window. `MSG_WISP_CLAIM` entries accumulate the same way. The union of both fresh sets feeds the `CHAR_WISP_CLAIMS` blob (see below) so a lamp painted before its claim message arrives is not invisible.

### Tier 2: Wisp control

**`MSG_CONTROL_OP` (0x03)**, wisp control envelope. Unicast or broadcast.
```
header(6) + targetMac(6) + sourceMac(6) + payloadLen(2 LE) + payload(N)
```
`payload` is opaque to the relaying lamp: a `wispOp` exactly as the app wrote it to `CHAR_WISP_OP`, sealed or plaintext (see the wispOp framing below), or the wisp's plaintext `wispStatus` / `wispConfig` JSON. Each JSON payload carries a `char` tag. Lamps on older firmware also sent lamp settings here (`brightness`, `shadeColors`, `baseColors`, `expressionOp`, `knockout`).

CONTROL_OP does not carry `triggerExpression`; directed expression triggers ride MSG_COMMAND.

Only wisp traffic uses CONTROL_OP between devices: a lamp unicasts the app's `wispOp` to its display wisp, and the wisp broadcasts `wispStatus` and `wispConfig`. A lamp never sends lamp settings to another lamp, and the app never routes them through one. A lamp still parses, dedups and relays every CONTROL_OP whatever its tag, so lamps of any version coexist, but from a mesh-received op it takes only `wispStatus` and `wispConfig` (`inboundControlOpKind`, `applyInboundControlOp`); any other tag (an older lamp's settings, a relayed `wispOp`) is relayed and dropped. The lamp-setting tags apply only to the app's own `CHAR_REMOTE_OP` write on the lamp it is connected to.

The unacked broadcast types a lamp sends (`MSG_COMMAND`, `MSG_COLOR_QUERY`, `MSG_COLOR_INFO`) have no per-frame retry, so each send buffers the identical frame (same seq) in a per-type `ResendRing` and `MeshLink::tick()` re-broadcasts it `kResends` more times, `kResendGapMs` (40 ms) apart — spaced so the copies fall in different RX-scan gaps rather than one hole. The gap must exceed the ~15 ms scan window, which only holds because the lamp runs a uniform continuous 1.5% BLE scan with no periodic high-duty burst to black out RX. The rings are per-type (not one shared ring) so a command fan-out's replays don't crowd out a colour query's; a lamp's only control op, the unicast wispOp, has its own ring (`controlOpUnicastResend_`, below); a command's ring holds one slot per fan-out target so every peer's frame stays in flight, and a command frame with a payload larger than the ring's 358 B budget sends once. `MSG_COMMAND` is how a cascade reaches each nearby lamp, so this is what makes a triggered-expression wave survive coex loss. The receiver's per-type `DedupRing` collapses the copies to one apply; receive-side state, no wire change.

**Wisp envelopes** (plaintext JSON, broadcast):

`wispStatus`, wisp → fleet, periodic state report.

Off mode (source == "off") — carries `offColor`; the drift fields ride the config copy (below) in every mode:
```json
{
  "char":"wispStatus",
  "source": "off",
  "wifiConnected": true,
  "sourceLive": false,
  "lastSeenMs": 14823,
  "hasPassword": true,
  "offColor": "ffb43c00",
  "name": "living room",
  "driftIntervalMs": 90000,
  "driftFadePct": 40
}
```

Manual/Osc mode — no `offColor`; `oscRole` rides in any mode off its default:
```json
{
  "char":"wispStatus",
  "source": "osc",
  "wifiConnected": true,
  "sourceLive": true,
  "lastSeenMs": 14823,
  "oscRole": "secondary",
  "paletteIdPrefix": "abc12345",
  "name": "living room",
  "driftIntervalMs": 90000,
  "driftFadePct": 40
}
```
- **Sender**: wisp(s) only. Lamps gossip-relay (the `MSG_CONTROL_OP` relay rule) but never originate. The wisp's shared seq counter starts at a random value at boot so a quick double reboot cannot replay `(mac, type, seq)` tuples still cached in peers' dedup rings.
- **Cadence**: on-change + 30 s heartbeat, coalesced to at most one emit per 5 s (a due emit inside the window is deferred, never dropped). A change opens a ~20 s burst that re-broadcasts the status every 2 s, so at least one copy clears a multi-second coex reception blackout on the lamps and the app's control-op confirmation survives. Change triggers: any applied wispOp, WiFi connect/disconnect edge, Ethernet up/down edge, OSC stream live/stale edge, `pollStatus`. `MSG_WISP_PALETTE` is emitted in the same tick (see Tier 1) so the app's view of the palette converges on the same cadence. A status is held back while a `wispConfig` went out less than 250 ms ago (see `wispConfig` below).
- **Fields: live state and the config copy.** The status carries the wisp's live state: `char`, `source`, `wifiConnected`, `sourceLive`, `lastSeenMs`, `hasPassword`, `offColor`, `oscRole`, `paletteIdPrefix`, `name`, `ethIp`, `wifiChannelMismatch`, `wifiApChannel`. The wisp's settings (`shuffleSeed`, `opSeq`, `driftIntervalMs`, `driftFadePct`, `ledType`, `px`, `brightness`) belong to `wispConfig`, and during the changeover the status also carries them as a lowest-priority copy, so a lamp that predates `wispConfig` still shows an app the wisp's settings. The copy is marked `// TODO:` in `status_json.cpp`: once the fleet's lamps carry `wispConfig`, the follow-up drops it and sets `"configSplit": true` on the status, and that removal is what takes the settings off the 30 s heartbeat every lamp relays. The app already honours the marker: a status with `configSplit` true has no config fields, and the app takes settings only from `wispConfig` (absent counts as false, which is every wisp today).
- **Payload budget — degrade by construction**: guaranteed ≤ 576 B (`CONTROL_MAX_PAYLOAD`). A guaranteed core — `char`, `source`, `wifiConnected`, `sourceLive`, `lastSeenMs`, plus `hasPassword` when true — is pinned by a native test at ≤ 179 B at worst-case field widths (123 B today), so the builder always produces a frame. Every other field is add-if-fits in priority order: the live fields `offColor` (Off only), `oscRole`, `paletteIdPrefix`, `name`, `ethIp`, `wifiChannelMismatch`, `wifiApChannel`, then the config copy `shuffleSeed`, `opSeq`, `driftIntervalMs`, `driftFadePct`, `ledType`, `px`, `brightness`. `offColor`, `oscRole` and `paletteIdPrefix` lead: the prefix is the app's palette re-read trigger, a dropped `offColor` silently renders the app's amber default, and a dropped `oscRole` shows the wrong role selected. Every live field outranks the whole copy, which a lamp that reads `wispConfig` never needs. Within the copy `shuffleSeed` and `opSeq` lead: the seed drives the app's tuple prediction and `opSeq` confirms a sealed op. The pathological worst case with the copy (421 B — Off mode, offColor beside the drift fields, every field widest, both mismatch fields, a 15-character `ethIp`; 402 B in Manual; `test_status_json`'s `test_worst_case_untruncated_length`) fits the cap whole, so nothing sheds on the mesh; the order governs only if the frame grows. Without the copy the widest status is 295 B (Off mode with every live field set; 315 B if the 20-byte name is all `"`, each escaped to 2; ArduinoJson escapes `"`, `\` and `\b \f \n \r \t` to two bytes and passes other control bytes through raw; `test_status_only_worst_case_pinned`).
- **The 512 B characteristic.** The 576 B cap is the mesh frame's, and it is not the binding ceiling for a direct read: `CHAR_WISP_STATUS` is a single characteristic value that NimBLE caps at 512 B (`BLE_ATT_ATTR_MAX_LEN` in the vendored `ble_att.h`, which overrides a build flag; see the page-protocol comment in `ble_control.cpp`), and past it `setValue` stores a zero-length value that blanks an app's whole wisp view. The lamp's merge adds its own fields (`wispMac`, `wispVersion`, `helloFlags`, `helloPaletteIdPrefix`, `helloLastSeenMs`, `statusLastSeenMs`, `configLastSeenMs`, `controllingBase` / `controllingShade` and the wisp colours), up to 301 B, so neither the status with its copy nor the status without it is guaranteed to fit: the widest status without the copy merges to 596 B (`test_wisp_status_merge`). The app therefore reads the status through the `wispstatus` page section, which has no ceiling. For older apps the direct read sheds `helloPaletteIdPrefix`, `helloLastSeenMs`, `statusLastSeenMs` and `helloFlags`, in that order (`kShedOrder`), until the value fits, and so does every notify, since `notifyWispStatus` serves the same `kWispStatusCharMaxLen` merge. Of these the app acts only on `statusLastSeenMs`, and in an empty manual-palette save judges a status that arrives without it by the wisp's `lastSeenMs` alone (see `paletteIdPrefix`). That brings the widest copy-free status to 503 B and a typical fully configured wisp with its copy under 512 B; a status that still does not fit is served as before. `configLastSeenMs` never sheds.
- **Omit-when-default**: a field whose value equals the app parser's missing-field default is left off the wire — `hasPassword` false, `shuffleSeed` 0, `opSeq` 0, `driftIntervalMs` 120000, `driftFadePct` 50, `ledType` "GRB", `px` 30, `brightness` 100, `wifiChannelMismatch` false, `wifiApChannel` 0, `oscRole` "primary", empty `name` / `paletteIdPrefix` / `ethIp`. `sourceLive` is always sent, so an absent key reads as an older wisp.
- **`source`**: `"off"` | `"manual"` | `"osc"`. Consumed by the Flutter app to surface and round-trip the source-toggle state. A wisp that stored the retired Aurora mode boots as `"off"`; an app reading `"aurora"` from an older wisp maps it to off.
- **`sourceLive`**: `true` while `source == "osc"`, a non-blank palette has arrived on the role's address, and something from the Straylight feed (that palette or any other `/straylight/` message) arrived within the last 30 s; the same signal as HELLO flag bit 2. The app shows "Waiting for Straylight" while it is false in Osc mode.
- **`oscRole`**: `"primary"` | `"secondary"` | `"tertiary"` | `"fill"`, the Straylight palette the wisp follows. Present in every source mode when the role is not `"primary"` (the app default), so the app shows the stored role the moment Straylight is picked.
- **`offColor`**: packed lowercase-hex `"rrggbbww"` (8 chars, no `#`), the ring color used in Off mode (W = warm white). Present only when `source == "off"`. The app also accepts the legacy 3-/4-int RGBW array shape from older wisps, and defaults to warm amber when absent.
- **`paletteIdPrefix`**: first 8 chars of the live palette id. In Manual mode the id is an 8-char FNV-1a hash over the palette's canonical wire bytes (r,g,b per color, plus w only when w != 0), so the app confirms a `setManualPalette` write by matching this prefix against the hash it computes locally (`WispRepository.paletteIdHash`) on the reliable status NOTIFY from the wisp it wrote to (the same `wispMac`). The wisp clears the id (`CurrentPalette::clear`), so the status omits the prefix, in these cases: on the transition to Off; on entering Manual with an empty stored palette; on a `setManualPalette` with an empty `colors` list applied in Manual; and in Osc from the transition until the first non-blank palette, and again whenever a blank palette or a stale stream holds the lamps at Off (`WispController::holdOscStream`). The app confirms an emptied manual palette on a Manual status from that wisp that omits the prefix and that is newer than the baselines the save takes before sending. For an empty save with a known `wispMac`, `WispNotifier.setManualPalette` first waits out any read in flight (`_awaitReadsIdle`, up to `_readIdleTimeout`, 3 s) and re-reads the status (`_rereadStatusSection`, given up on after the same 3 s), so the status the lamp holds and its `statusLastSeenMs` join the baselines. It then passes `_confirmPaletteLanded` an empty expected hash, the `wispMac` it wrote to and `emittedAfter`, three baselines `WispNotifier._noteStamp` keeps from every status heard, notify or read: `_newestLampStamp`, the newest `statusLastSeenMs` heard on this connection (one value for every wisp, since every status comes from the one lamp's clock); that wisp's entry in `_newestStamps`, the newest wisp `lastSeenMs` heard from it (per wisp, since each wisp has its own clock); and `heard`, that wisp's entries among `_heardStamps`, the last `_heardStampCap` (4) distinct `(wispMac, lastSeenMs)` pairs heard. `WispNotifier._emittedSince` rejects a status whose `lastSeenMs` is in `heard`: that stamp names an emission heard before the save, whatever form it comes back in, shed or whole. Otherwise it judges by one stamp: when the status carries `statusLastSeenMs` and the lamp baseline is set, it needs a larger `statusLastSeenMs`; failing that it needs a `lastSeenMs` above the wisp baseline, skips the check when that baseline is unset, and never confirms a status with no `lastSeenMs` once it is set. A status from before the save can confirm it only if it already omitted the prefix, so the gaps that follow need a wisp whose manual palette was already empty. A status the lamp cached before the save that the app had not heard can still confirm it: one the lamp caches during the save's status read after the read has its copy, whose notify lands once the baselines are taken; any such status when the read is skipped because another read stays busy past the wait, or when the read times out and lands later; and any such status above the baselines heard before the read, when the read fails or comes back without `statusLastSeenMs` (shed on the direct read, or a lamp without the `wispstatus` section). An older wisp emission that reaches the lamp after a newer one (a millisecond-scale mesh reorder) can confirm it too: `LampRoster::cacheWispStatus` stamps every status it caches with the lamp's `millis()` in arrival order, so the late one carries a fresh `statusLastSeenMs`. A wisp reboot or a wisp `millis()` wrap (about 49.7 days of wisp uptime) restarts `lastSeenMs`, so it leaves unconfirmed only a save judged by the wisp stamp, until the stamp passes the newest one held: a save whose lamp baseline is unset (nothing heard on this connection carried `statusLastSeenMs`, the save's own status read included), or one answered only by statuses without it. After a wisp reboot a new emission can repeat a `lastSeenMs` in `heard`; that one does not confirm, and the next one does. `WispNotifier.build` clears `_newestLampStamp` when `watchConnected` reports a disconnect (`_connSub`), since a lamp reboot restarts its clock; the notifier is not rebuilt on reconnect, because `LampShell` watches it and stays mounted through one. `_noteStamp` only raises `_newestLampStamp`, and lamp `millis()` wraps at about 49.7 days of uptime, so a connection held across the wrap leaves every later empty save unconfirmed until it drops, unless a status without `statusLastSeenMs` answers it. Outside that confirm, a status without the prefix leaves the app's saved palette as it is: `WispNotifier._maybeRereadForPalette` re-reads the `wisppalette` section only on a changed, non-empty prefix. The saved palette changes only on a non-empty `wisppalette` read (`WispNotifier._ingestManualPalette` skips an empty one, since `WispRepository.readManualPalette` also returns empty when the read fails) or on the user's own confirmed save, an emptied palette included (`WispNotifier._applyPaletteConfirm`), so a second view of the same wisp keeps its old palette after another view empties it.
- **`driftIntervalMs`** / **`driftFadePct`**: drift cadence + fade depth, part of the config copy and present in every source mode when off their defaults. The app's drift form shows in every mode, so an Off status without them would read as the parser defaults.
- **`name`**: wisp display name, ≤ 20 characters.
- **`hasPassword`**: `true` when the wisp has a control password set; omitted when false (the app default).
- **`opSeq`**: monotonic counter the wisp bumps each time it accepts and applies a *sealed* wispOp (plaintext ops don't move it). Sealed ops carry no direct ACK, so the app confirms one landed by watching this advance — the password-change flow persists the new secret only after `opSeq` steps past the value it read before sending. RAM-only: resets to 0 on wisp reboot, so the app treats only a strict increase as confirmation. Omitted at 0.
- **`lastSeenMs`**: wisp-local `millis()` at emission, so it restarts from zero when the wisp reboots. A lamp serving the cached status passes it through unchanged (`mergeWispStatusJson` adds its own `helloLastSeenMs` / `statusLastSeenMs` beside it), so a re-served copy carries the stamp of the emission it caches. The app compares it only within one empty manual-palette save (see `paletteIdPrefix`). `WispNotifier._noteStamp` keeps the last few stamps heard and the newest stamp heard per wisp: a status whose stamp is among those heard before the save never confirms it, and a status the lamp's `statusLastSeenMs` cannot order confirms it only with a stamp above the newest.
- **`wifiChannelMismatch`** / **`wifiApChannel`**: set when the wisp's channel-6 STA guard (see below) dropped an association to an AP on the wrong channel. `wifiChannelMismatch` true explains a `wifiConnected` false the app would otherwise blame on bad creds; `wifiApChannel` is the offending channel. Both omitted in the normal case. Recovery is a `wifiReconnect` or fresh `setWifi` op, switching the source away from Straylight and back, or Ethernet going down while the source is Straylight, any of which clears the flag and re-emits.
- **`ethIp`**: the wisp's wired Ethernet address, present only while the W5500 has a link and a DHCP lease (absent on a wisp with none fitted). It explains a `wifiConnected` false in Osc mode, since the STA stays down while Ethernet is up, and it is the address to point Straylight's OSC feed at. Its up/down edge re-emits the status like a WiFi connect edge. A `wifiChannelMismatch` held from before Ethernet came up stays set (only the join becoming wanted again clears it), so while `ethIp` is present the app treats it as stale and shows the Ethernet line instead.
- `manualPalette` is intentionally NOT carried here, it ships via the separate `MSG_WISP_PALETTE` broadcast.
- **Lamp-side cache**: each lamp keeps the latest `wispStatus` for its display-slot wisp in `LampRoster`. `CHAR_WISP_STATUS` reads and the `wispstatus` page section serve the same merge (`mergeWispStatusJson` in `wisp_status_merge.cpp`) of this cache with the last `MSG_WISP_HELLO` snapshot, the cache's stamps and the lamp's own wisp-control state; the manual palette is served on its own `wisppalette` section. `statusLastSeenMs` is the lamp's own `millis()` when it cached the status (`LampRoster::cacheWispStatus`), served unchanged, so a re-served copy carries the same value, each newly cached status a larger one, and a wisp reboot leaves it alone. Adopting a wisp (`LampRoster::adoptWispLocked`) resets it to 0, and the merge omits it until a status from that wisp is cached. It restarts when the lamp reboots and wraps with the lamp's `millis()` at about 49.7 days of uptime.

`wispConfig`, wisp → fleet, the wisp's settings.

```json
{
  "char":"wispConfig",
  "ledType": "GRBW",
  "px": 60,
  "brightness": 80,
  "driftIntervalMs": 90000,
  "driftFadePct": 40,
  "shuffleSeed": 7,
  "opSeq": 3
}
```
- **Fields**: `ledType`, `px`, `brightness`, `driftIntervalMs`, `driftFadePct`, `shuffleSeed`, `opSeq`, each with the meaning it has in `wispStatus`. Every field rides every time, defaults included, so a received `wispConfig` is complete and an absent key never stands for a default. Worst case 148 B (`test_config_worst_case_pinned`). `opSeq` is not a setting, but it belongs here because it moves only when the config does (a sealed op), and the app's password flow confirms on it.
- **Envelope and relay**: the same plaintext `MSG_CONTROL_OP` broadcast as `wispStatus`, gossip-relayed by every lamp under the same rule. It is an additive JSON tag inside an opaque payload, so no protocol bump: a lamp that predates it relays it unchanged and drops it at apply (its control-op apply had no branch for it), and the app on such a lamp keeps reading settings from the status copy.
- **Cadence**: sent at boot, once per `pollConfig` (coalesced: at most one per 5 s, a poll inside the window deferred, never dropped), and after any status emit that finds the config changed, then again after every status emit for the rest of that change's ~20 s burst. "Changed" is a fingerprint of the serialized config taken at each status emit, so any sealed op re-sends it, `setSource` and `setName` included, because its `opSeq` bump changes the config; a plaintext op that changes no setting does not. Never within 250 ms of a status in either order (`ConfigCadence`, `test_config_cadence`): a lamp that predates `wispConfig` holds one inbound control op per loop pass, so of two back-to-back frames the first would be lost. Current lamps route both broadcasts to their own slots (`peekWispBroadcast`), and the wisp drops lamps' relays of its own broadcasts before its single op slot (`controlOpForWisp`), so an echo never overwrites a pending app op. It does not ride the 30 s heartbeat.
- **Lamp-side cache**: each lamp caches the display-slot wisp's latest `wispConfig` beside its `wispStatus`, under the same admission and stickiness, and clears both when a rival takes the slot. A payload whose bytes differ from the cached one notifies `CHAR_WISP_STATUS` and stamps `configLastSeenMs` into the merged status; an identical re-send through a burst does neither. The app re-reads the `wispconfig` page section when that stamp moves, and merges it only over a status that carries no settings of its own: during the changeover the status copy is the fresher source, since a skipped or lost config re-read cannot make it stale. The section serves the cached payload verbatim, `"{}"` until one arrives.

`wispOp`, app → wisp, control writes proxied through the paired lamp. The lamp unicasts the op as `MSG_CONTROL_OP` to its display-slot wisp's MAC (single-hop scope: the wisp acts on ops targeted at it, not a sibling). The C6 wisp's bursty RX-scan can drop every 802.11 MAC-ARQ attempt inside one scan gap, so this path arms its own `controlOpUnicastResend_` ring: `MeshLink::tick()` re-sends `kResends` more copies `kResendGapMs` apart, each copy also unicast (extracting the target MAC from the frame at `HEADER_SIZE`, individually MAC-acked) and collapsed by the wisp's dedup. With no display wisp in range the op is dropped rather than broadcast.

**Wire framing.** The payload written to `CHAR_WISP_OP` (and carried verbatim inside `MSG_CONTROL_OP`) is in one of three shapes:
- `0x02` prefix: AES-GCM sealed. `[0x02][12B nonce][16B tag][ciphertext]`. Used when the wisp has a password set. Key derivation: HKDF-SHA256, salt = `uuidSaltLE16(CHAR_WISP_OP)`, info = `"lamp-v1"\0wispOp`, IKM = wisp password. Same framing as lamp control-op sealing.
- `0x01` prefix: explicit plaintext marker. `[0x01][JSON]`. Accepted when no password is set.
- Bare `{`: plaintext JSON at offset 0. Accepted when no password is set.

When a password is set, the wisp rejects plaintext (both `0x01`-prefixed and bare) with three exceptions, all always accepted plaintext regardless of password state:
- `setManualPalette` — an **integrity exception**: palette colors are unauthenticated by design, because an attacker in BLE proximity can push override colors anyway; sealing adds nothing under the proximity threat model.
- `pollStatus` and `pollConfig` — **read-only**: they only trigger a re-broadcast of state every lamp already relays, so an app without the password can still refresh a password-protected wisp.

**Replay protection.** The wisp maintains a RAM-only bounded nonce ring (32 entries). A seen nonce is rejected; a forced reboot resets the ring. Config ops are low-value and idempotent, so reboot-replay is an accepted ceiling.

```json
{"char":"wispOp","op":"setSource","mode":"osc"}
{"char":"wispOp","op":"setOscRole","role":"secondary"}
{"char":"wispOp","op":"setWifi","ssid":"...","pw":"..."}
{"char":"wispOp","op":"setDrift","intervalMs":120000,"fadePct":50}
{"char":"wispOp","op":"setName","name":"living room"}
{"char":"wispOp","op":"setPassword","password":"newpass"}
{"char":"wispOp","op":"setManualPalette","colors":[[255,0,0],[0,255,0]]}
{"char":"wispOp","op":"pollStatus"}
{"char":"wispOp","op":"pollConfig"}
```
- **Sender**: the Flutter app writes the payload (sealed or plaintext) to `CHAR_WISP_OP` on any paired lamp; the lamp unicasts it as `MSG_CONTROL_OP` to its display-slot wisp's MAC (dropped when no display wisp is in range).
- **Receiver**: the addressed wisp only. Lamps relay a `wispOp` and never apply it (`applyInboundControlOp` takes only `wispStatus` / `wispConfig`).
- **Wisp dedup**: the wisp runs its own 64-slot `controlOpDedup_` ring keyed on `(sourceMac, msgType, seq)` so the unicast resend copies of the same op don't re-apply.
- **`setSource`**: `mode` is `"off"` | `"manual"` | `"osc"` (or numeric 0 | 1 | 3); anything else, `"aurora"` and 2 included, is `Malformed` and changes nothing. Persisted to NVS (key `srcMode`); a stored 2 or unknown value loads as Off. Selecting Manual or Osc turns paint mode on, which is the paint opt-in. Manual with an empty palette paints nothing: lamps ease home rather than keep the previous source's colours. In Osc the wisp listens for OSC 1.0 on USB serial (SLIP, sharing the console) and UDP port 9000 on every interface, reading flat RGB floats 0..1 (1..50 colours) from `/straylight/palette/<role>/colours`, bundles included. Every interface means the softAP, the STA join, and wired Ethernet when a W5500 is fitted: the socket binds `INADDR_ANY`, and lwIP delivers a datagram from any netif to it. Once a palette is held, any parsed packet carrying a `/straylight/` address keeps it live; with nothing from the feed for 30 s (`OscFreshness::kStaleMs`, three of Straylight's 10 s UDP restates) the wisp holds lamps and its ring at Off (STATE packs no entries, ArtNet stops) with paint mode kept, and the next palette resumes painting with no app action. A blank palette (no channel above zero, which Straylight sends for a role with nothing bound) counts as no palette and drops to that hold at once. The ring follows every palette change; lamp re-assignment, the out-of-cadence STATE and ArtNet follow at most once a second, trailing edge included, so a fast-moving palette cannot flood the mesh. A role change keeps painting the old role's palette until the new role's first one lands.
- **OSC transport**: USB serial or wired Ethernet are the recommended paths. Ethernet (a W5500 on SPI, `net/eth_link.cpp`, wiring in the wisp README) takes the feed off the radio entirely, and the wisp blocks IPv4 multicast at the W5500 (the core's `ETH.begin` turns all-multicast on) so multicast sACN on a lighting LAN never costs the C6 an SPI read or a heap buffer. lwIP's 6-datagram socket queue below still applies over the cable. Broadcast is not filtered by the chip, so each broadcast frame on the cable (ARP, Art-Net broadcast, ArtPoll) costs an SPI read and a full-frame heap buffer in the W5500 receive task, above the loop task in priority; the wisp belongs on a segment without broadcast Art-Net. The framework ships with IP forwarding on (`CONFIG_LWIP_IP_FORWARD=y`), so the wisp routes between the softAP's 192.168.4.0/24 and the Ethernet netif with no NAT, the same exposure as the STA join; the wisp README has the details. Over WiFi UDP the wisp receives Straylight's whole feed (every param, level and palette, not only its role's palette), all of it airtime on the shared channel-6 radio. lwIP queues at most 6 datagrams per socket and the loop drains it once per pass, behind a 5 ms `delay()`; a restate burst arrives faster than that and overflows the queue, and the palettes, sent last, are what drop. That can happen on every restate, so a palette change lost that way is not repaired by the next restate either: the lamps hold the old palette until the palette changes again and that datagram lands. The feed keepalive above stops this from also reading as stale. SLIP frames on serial share the text console: a frame that goes 200 ms without a byte is dropped and the console is back on text.
- **`setOscRole`**: `role` is `"primary"` | `"secondary"` | `"tertiary"` | `"fill"`; anything else is `Malformed`. Persisted to NVS (key `oscRole`, u8, unknown loads as primary). Gated like every config op: sealed when a password is set. In Osc mode a role change keeps the stream live and lamps on the old role's palette until the new role's first palette lands.
- **`setWifi`**: persists credentials to NVS and restarts the STA join schedule (an attempt at once while the source is Osc). The relay lamp passes the payload through opaque; the wisp handles the WiFi join.
- **`wifiReconnect`**: restarts the STA join with the stored creds. The recovery path after the channel-6 guard drops an association; clears the mismatch guard. Read-only w.r.t. NVS.

**When the STA joins.** The STA join runs only while the source is Osc, the one mode that uses the LAN, and Ethernet is not up (`staWanted()` in `net/sta_join.hpp`: link plus a DHCP lease); in Off and Manual, or while Ethernet carries the feed, the wisp never scans or joins, and either condition arriving drops any association. Ethernet going down in Osc makes the join wanted again, which arms an attempt at once and clears a channel-guard halt the same way re-entering Osc does. Ethernet itself never touches the radio: the W5500 is its own MAC and PHY on SPI, so the radio does only ESP-NOW and the channel-6 softAP. Stored credentials stay in NVS throughout. The policy is `StaJoin` (`net/sta_join.hpp`, pure, natively tested); `WifiLink` carries it out. Each attempt first runs an async active scan of channel 6 only (`WiFi.scanNetworks` with the channel and the stored SSID as filters, 120 ms dwell, bounded by a 5 s timeout; a result on another channel is ignored). Only when the SSID is found there does the wisp call `WiFi.begin(ssid, pass, 6, bssid)` with the strongest BSSID the scan saw; not found counts as a failure with no wider scan. The channel argument to `WiFi.begin` is only where the driver's own scan starts, so the pre-scan is what keeps a missing or off-channel AP from triggering an all-channel scan; the driver can still scan further if the AP vanishes between the two. Attempts are judged by WiFi events, never by `WiFi.status()`, which the Arduino core latches from the last event: an STA disconnect the wisp did not cause (any reason but `ASSOC_LEAVE`) or 15 s without `GOT_IP` fails a join, and a `GOT_IP` in any wanted state is adopted, the core's one-shot first-connect retry included, except while halted by the channel guard; a `GOT_IP` while halted or while the source does not want the network is dropped at once, since that retry ignores `setAutoReconnect(false)` and can link after the wisp has left Osc. Disconnects and `GOT_IP`s are applied in the order they happened, and any that arrived before the wisp's own last drop are ignored, so a `GOT_IP` for a link already dropped is never adopted; the channel guard checks the channel each adopted `GOT_IP` carried. While linked, the STA netif's connected bit (not latched like `WiFi.status()`) backs up the disconnect event. The core's auto-reconnect is off; failures retry on a backoff (`RetryBackoff`: 10 s, doubling to a 5 min cap), restarted on entering Osc and on `setWifi` / `wifiReconnect`, and a lost link retries after 10 s. An AP that is not there therefore costs one single-channel scan per backoff step rather than an all-channel scan every few seconds, which is what pulled the radio off the mesh long enough for lamps to age out of the roster. The status does not say which network the wisp is failing to find; `[wifi] <ssid> not on ch 6; retry in Ns` on the console does. A drop re-pins the radio to channel 6 only when it has actually left it, since the driver refuses a channel change while scanning, connecting or serving softAP stations.

**Channel-6 STA guard.** The wisp shares one radio between ESP-NOW (pinned to `LAMP_ESPNOW_CHANNEL` = 6) and its STA join. Associating to an AP on any other channel drags the radio off 6 and silently breaks the mesh. On a `GOT_IP` whose channel != 6 the wisp refuses to stay associated: it drops the association, re-pins the radio to channel 6, and halts retries (so association churn can't re-drag the radio). It does not retry on its own. Recovery is an explicit `wifiReconnect` or `setWifi`, re-entering Osc, or Ethernet going down in Osc (the join becomes wanted again, see When the STA joins); each clears the halt and the flag. The first three are operator acts; the Ethernet edge is not, so a wisp whose cable drops retries the join once per drop. With every join made to a BSSID found on channel 6 this fires only if the AP moves channel after the join. The drop flips `wifiChannelMismatch` on and triggers an immediate `wispStatus` re-emit (no waiting for the 30 s heartbeat). Only the recoveries above clear it, each re-emitting the status.
- **`setDrift`**: sets the color-drift cadence. `intervalMs` [30000..3600000]: how often each lamp is re-targeted in milliseconds. `fadePct` [0..100]: fade length as a percentage of the interval. Persisted to NVS; reflected in the next `wispStatus` broadcast under `driftIntervalMs`/`driftFadePct`.
- **`setName`**: sets the wisp's display name, clamped to 20 characters. Persisted to NVS (key `wispName`). Reflected in the next `wispStatus` broadcast under `name`.
- **`setPassword`**: sets or clears the wisp control password. Persisted to NVS (key `wispPassword`). When factory-fresh (no current password), a plaintext `setPassword` is accepted. When a password already exists, `setPassword` must arrive sealed under the OLD password; the decrypt step authenticates the caller before the password is replaced. An empty `"password"` field clears the password (removes the NVS key), reverting to open access.
- **`setManualPalette`**: always accepted plaintext, even when a password is set (integrity exception; see framing note above). `colors` entries are `[r,g,b]` or `[r,g,b,w]` tuples (missing `w` reads as 0). The app collapses `w == 0` colors to triples so a full 10-color palette stays inside the `CONTROL_MAX_PAYLOAD` relay cap; if the RGBW form still exceeds the cap it strips W from trailing colors, a truncated write over a relay-dropped one. Updates the wisp's stored palette; triggers a `MSG_WISP_PALETTE` broadcast.
- **`pollStatus`**: asks the wisp to re-broadcast `wispStatus` (+ `MSG_WISP_PALETTE`) now via the on-change path; changes no state. The app fires it on wisp-screen open (rate-limited app-side; coalesced wisp-side). Always accepted plaintext (read-only; see framing note above). Wisps that predate the op treat it as unknown → `Malformed` → no-op, so it degrades to the 30 s heartbeat.
- **`pollConfig`**: asks the wisp to re-broadcast `wispConfig` now; changes no state and does not re-emit the status. The app fires it beside `pollStatus` on wisp-screen open (rate-limited app-side; coalesced wisp-side, see `wispConfig` cadence). Always accepted plaintext. Wisps that predate the op treat it as unknown → no-op; their settings ride the status.

### Tier 3: Transient overrides

All four override/restore messages share the same prefix through `sourceKind` (bytes 0..19). `fadeDurationMs` follows at offset 20: **4 LE bytes** for `MSG_OVERRIDE_COLORS`, **2 LE bytes** for the other three. Only the tail after the fade differs.

**`MSG_OVERRIDE_COLORS` (0x21)**, **retired** as the wisp paint path (wisp paint is now declared in `MSG_WISP_STATE`); no live sender. Format + parser kept for backward-compat. Push transient colors onto a renderable surface.
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
colors[0] → base, colors[1] → shade.
```

**Paint hold (STATE-driven)**: the wisp no longer unicasts per-lamp paint or a
per-lamp keep-alive. `PaintDistributor` samples a per-lamp base/shade color
(deterministic per-MAC for a fresh paint, re-rolled per drift slot) and writes
it into the `WispRoster` claim — no wire send. `MSG_WISP_STATE` then broadcasts
the whole claimed set on its cadence, and a lamp holds paint while STATE keeps
naming it (see `MSG_WISP_STATE`). This drops the paint traffic from O(N) unicast
per cycle to one O(1) broadcast.

**`MSG_RESTORE_COLORS` (0x22)**, **retired** (STATE-absence eases a lamp home). Drop colors override, restore baseline.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE)
= 22 bytes

surface is Base/Shade/BaseAndShade (0x01/0x02/0x03); any other value,
0xFF included, is rejected by the parser. The wisp restores with
BaseAndShade (0x03).
targetMac == FF:FF:FF:FF:FF:FF means "restore on every lamp"
sourceKind == 0xFF (OverrideSource::Any) means "restore from any source"
```

**`MSG_OVERRIDE_BRIGHTNESS` (0x23)**, Push a transient relative-brightness factor 0..100.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE) + brightness(1)
= 23 bytes

surface is Base/Shade/BaseAndShade (0x01/0x02/0x03); any other value,
0xFF included, is rejected by the parser.
```

`brightness` is a relative factor: output = min(baseline, max(baseline × factor / 100, 20)), where baseline is the lamp's own current effective brightness. It scales, never raises: a lamp already below 20% is left as-is. The floor is a 20% output clamp enforced lamp-side at apply, so a forged factor=0 frame still lands at 20% output — nothing blacks out a space.

Source-gate: a lamp accepts a brightness override only from the wisp that currently claims it (fresh `MSG_WISP_CLAIM` in the display slot). The wisp broadcasts claims every other 2 s presence tick (~4 s) in every source mode incl Off, so the gate holds even when the wisp paints no colors. A rogue broadcaster cannot dim your lamps. The wisp re-asserts brightness by walking its claimed lamps with unicast `MSG_OVERRIDE_BRIGHTNESS` on a cadence under the 60 s override watchdog, plus an immediate walk when the operator moves the slider.

**`MSG_RESTORE_BRIGHTNESS` (0x24)**, Drop brightness override.
```
header(6) + sourceMac(6) + targetMac(6) +
surface(1) + sourceKind(1) + fadeDurationMs(2 LE)
= 22 bytes
```

**Fade behavior on receivers:**
- Fade happens in `ConfiguratorBehavior::draw` (single source of truth for per-pixel interpolation). Override modules set the target colors + `fadeDurationMs` once at apply-time.
- Mid-fade interrupt: a new OVERRIDE arriving during an in-flight fade snapshots the current interpolated buffer as the new "from", smooth handoff, no visual jerk.
- Watchdog auto-restore uses a sensible default fade (~1 s) since no sender supplied one. The colour `ColorOverride` watchdog is inert now that wisp colour rides `MSG_WISP_STATE` (no `MSG_OVERRIDE_COLORS` sender); STATE freshness is the paint hold. The brightness watchdog stays its own separate 60 s constant (`brightness_override.hpp`) — its 40 s unicast re-assert walk keeps it well inside that window.
- Brightness uses a change-driven callback: `BrightnessOverride::tick` invokes the registered callback only when the integer-rounded value actually changes, no per-frame `setBrightness` regression.

### Tier 4: Expression announce

**`MSG_EVENT` (0x30)**, shared-key-authenticated, nearby-scoped expression-fired announce.

```
[header(6)] [sourceMac(6)] [payload(1..230) — ExpressionInvocation JSON] [command_auth tag(8)]
```

- **Sender**: any lamp that fires an expression locally, via `MeshLink::sendEvent()`. Emitted for every local fire of a triggered (non-`continuous`) expression; not gated on `cascadeEnabled`. Continuous descriptors never announce — they retrigger at boot, settings upsert, and wisp release, and observers (the expression mirror) would spuriously replay those.
- **Receiver**: delivers to the `ExpressionObserverRegistry` fan-out on Core 1. No auto-action in the mesh layer; observers react if registered. The `SocialEchoObserver` (expression mirror) is the current consumer: it probabilistically replays a warm peer's triggered expression ~0.5 s later, weighted by disposition + social mode. The replay goes through `triggerInvocation` (cascade-suppressed), so two lamps never echo into a loop.
- **No relay**: nearby-scoped by design — lamps only observe expressions they can physically hear.
- **Auth**: `command_auth::verify()` runs before dedup-record, so an unauthenticated frame is dropped before it consumes a dedup slot. See the command_auth section below.
- **Dedup**: `eventDedup_` 32-slot ring per `(sourceMac, seq)`. Originator pre-records its own seq so the broadcast echo does not re-deliver via observers.
- **Drain**: Core 1 loop via `PendingEvent` slot → `Lamp::drainEvent()` → `deliverEvent()` (`core/moments.cpp`): a payload carrying `mo` goes to the Moment director, anything else to `ExpressionObserverRegistry::fanOut()`.
- **Payload**: `ExpressionInvocation` JSON (cascade keys stripped, colors packed — see MSG_COMMAND). `delayMs` is carried but not acted on by the receiver; observers interpret it as they see fit.
- **Moment frames**: `{"type":"moment","mo":{...}}`, a sentinel invocation carrying one extra top-level object, `mo`: `k` kind (1 Fireflies, 2 Whisper; any other is ignored), `s` uint32 seed, `a` age in ms since the sender's reference instant (0 to 2000; the Fireflies `t0`, or the Whisper sender's glow, which the next hop follows by 3 s), and for a Whisper `h` hops (1 to 8), `to` the next hop's MAC, `v` the visited MACs (12 uppercase hex each, no separators, exactly `12*h` chars, origin first) and, optionally, `c` the origin's base colour (`rrggbbww`, packed lowercase hex like `colors`; any other form drops the frame). Every frame is sent twice 200 ms apart. The worst case is 198 B. A frame carrying `mo` never reaches the mirror. An older lamp parses it as an invocation of the unknown type `moment` and renders nothing (its mirror may spend a roll on it); no variant registers that id. No protocol bump: the key is additive and `parseInvocation` ignores unknown keys. Contract: [`moments.md`](moments.md).

**`MSG_COMMAND` (0x31)**, shared-key-authenticated, targeted expression invocation from one lamp to a specific nearby lamp.

```
[header(6)] [sourceMac(6)] [targetMac(6)] [payload(1..1444) — ExpressionInvocation JSON] [command_auth tag(8)]
```

- **Sender**: any lamp, via `MeshLink::sendCommand()`.
- **Receiver**: applies locally only when `targetMac == myMac || broadcast`. No gossip relay.
- **Auth**: `command_auth::verify()` runs before dedup-record. See the command_auth section below.
- **Dedup**: `commandDedup_` 64-slot ring per `(sourceMac, seq)`.
- **Drain**: Core 1 loop via `PendingCommand` slot → `Lamp::drainCommand()`.
- **Payload**: `ExpressionInvocation` JSON. `delayMs` in the invocation is honored (enqueued to `pendingTriggers` if non-zero). `sourceMac` propagates to `triggerInvocation` for cascade coalescing.
- **Payload ceiling**: `COMMAND_MAX_PAYLOAD` is 1444 B, derived off the ESP-NOW v2 frame (`ESPNOW_V2_FRAME_MAX` = 1470) less the 18 B fixed head and 8 B tag. MSG_COMMAND is a physical broadcast, so a frame over the classic 250 B limit reaches only v2-capable peers; a v1/classic peer drops the oversized frame per the ESP-NOW contract and silently misses that cascade (graceful, no crash). Big cascades therefore reach only the v2 fleet until every peer runs the v2 firmware; a mixed-fleet capability gate is owed before public beta.
- **Colors encoding**: `colors` is a single packed lowercase-hex string, 8 chars per color (`"rrggbbww…"`), no `#`, no separators; the key is omitted when empty. A receiver drops a malformed `colors` string whole (length not a multiple of 8, or a non-hex char) and still applies the invocation with its configured palette. A receiver keeps at most the first 8 colours and 12 `parameters` keys it knows (`kMaxExpressionColors` / `kMaxExpressionParams` in `config/config_types.hpp`) and ignores the rest, the same caps a stored expression carries. Example:

```json
{"type":"glitchy","target":3,"delayMs":0,"colors":"ff00000000ff0000","parameters":{"durationMin":250,"durationMax":900}}
```
- **Known senders**: cascade fan-out (`ExpressionManager::maybeCascade`); snafu `Greeting::control()` on peer arrival — sends a fast glitch (`type="glitchy"`, `durationMin=durationMax=12`, `target=SHADE`) colored with the sender's stem first color (`config.base.colors[0]`). Only sent when the peer has a known ESP-NOW MAC (`hasMac=true`).

### command_auth: shared-key tag on EVENT + COMMAND

MSG_EVENT and MSG_COMMAND are the two "force another lamp to do a thing" types, so both carry an 8-byte trailer: `HMAC-SHA256(key, frame_body)[:8]`, where `frame_body` is everything before the tag. `command_auth::appendTag()` writes it on send; `command_auth::verify()` checks it on receive, before the dedup-record, so a bad tag is dropped before it consumes a dedup slot. `MSG_CONTROL_OP` carries wisp control (a sealed wispOp the wisp decrypts) and does not use this.

The 32-byte key is baked in at build time from `LAMP_COMMAND_KEY_HEX` (a 64-char hex `-D` define). Key injection tracks the channel: `scripts/inject_command_key.py` injects nothing on a **dev** build (keyless/bench), and on **beta**/**stable** resolves the key from `LAMP_COMMAND_KEY_HEX` or `~/.lamp-os-command-key.hex`, FATALing if it's missing. Signed CI builds get the key via the `LAMP_COMMAND_KEY_HEX` secret.

- **Keyless build** (no key compiled in): `appendTag()` writes 8 zero bytes so the frame stays a uniform size that keyed peers still parse, and `verify()` returns true (can't verify => permissive). Dev builds mesh and cascade normally.
- **Keyed build**: `verify()` accepts only a constant-time match. A keyed lamp drops EVENT/COMMAND from a keyless dev build because its tag is zeros.
- **Not secrecy**: secure boot is off and the signed binary is public, so the key is extractable by a determined binary-reverser. This stops firmware built from source without the key, not a reverser.

**No PROTOCOL_VERSION bump.** The tag rides as an additive frame extension — older parsers ignore the trailing bytes, and a bump would instead make mismatched peers invisible to each other, splitting the mesh for no benefit. A keyed lamp only ever drops a dev build's EVENT/COMMAND, which isn't trusted to force other lamps anyway.
**`MSG_COLOR_QUERY` (0x32)**, Directed request for a peer's full color info (base + shade gradient stops).

```
[header(6)] [sourceMac(6)] [targetMac(6)]
```

Fixed 18 bytes; no payload. No gossip relay; addressedToUs filter on recv.

**`MSG_COLOR_INFO` (0x33)**, Directed reply to MSG_COLOR_QUERY carrying base + shade gradient stops.

```
[header(6)] [sourceMac(6)] [targetMac(6)] [baseCount(1)] [base RGBW stops (baseCount*4)] [shadeCount(1)] [shade RGBW stops (shadeCount*4)]
```

- Each stop is 4 bytes RGBW (R, G, B, W). baseCount and shadeCount are each ≤ 8. Maximum frame size is 84 bytes.
- No gossip relay; addressedToUs filter on recv.

## BLE GATT characteristics (lamp ↔ phone)

Service UUID `5f64f4d0-d6d9-4a44-9b3f-3a8d6f7e6b40`. UUIDs share that base; the `(0xNN)` shorthand below is the distinguishing 4th byte (`5f64f4NN-…`), except `commit` which uses an unrelated UUID. AES-GCM-gated characteristics require a successful `CHAR_AUTH` write within the connection lifetime.

**The layout is a frozen, positional contract.** `kGattLayout` in [`gatt_layout.hpp`](../../software/lamp-os/src/components/network/ble/gatt_layout.hpp) is the single source of truth and lists characteristics in **registration order** — handles are positional, so that order *is* the contract (`ble_control.cpp` registers by iterating the table, so order and props cannot drift; a boot-time coverage check flags a callback binding that misses the table). Grow append-only at the tail or evolve a payload; never insert/remove/reorder/add-a-CCCD without bumping `kGattSchemaVersion` (currently **4**) and re-pinning the hash in `test_ble_gatt_layout`. See the frozen-layout lock-in in `CLAUDE.md`. The table below is in layout order.

**Auth + state notify:**
- `CHAR_AUTH` (0xd1), write: lamp password (ciphertext or plaintext). Gates every auth-gated characteristic for the connection's lifetime.
- `CHAR_STATE_NOTIFY` (0xd8), notify: lamp-driven state-changed notifications. JSON payload includes `previewActive` (bool), a `greeting` object, and `nearbyRev` (uint32). `greeting.active=false` when idle; `greeting.active=true` carries `peer` (the greeted peer's `lampId`, mesh mac) and `kind` (`"warm"` / `"reserved"` / `"snub"` on standard lamps; `"glitch"` on snafu). Edge-triggered: fires on greeting start and stop. `quiet` (bool) is `ota_quiet_mode::isQuiet()`, the predicate every live-control write gate tests: true exactly while the lamp drops every write on this service except `CHAR_FW_CONTROL` / `CHAR_FW_CHUNK` (page-protocol reads still work). The lamp notifies on each change of it while a client is connected, and whenever a client subscribes; the subscription dies with the link, so the app re-subscribes after every reconnect. Firmware before the field omits it. After an app push that ends without a reboot, the app keeps editing frozen until `quiet` reads false, then sends its session flags again (`CHAR_HOME_MODE_FOCUS`, each `CHAR_EDIT_SESSION` selector it set) and re-reads the control sections (`lamp`, `base`, `shade`, `home`, `expr`). The freeze ends after `ControlNotifier.quietAfterPushBound` (75 s) regardless: the receiver's no-progress abort (`kNoProgressAbortMs`, 60 s) plus its inter-session hold (`InterSessionQuietHold::kHoldMs`, 10 s), plus slack. The same flags go again once the lamp reports `quiet: false` on a new link. A lamp without the field gets the re-read when the push ends and the focus ahead of each `CHAR_BRIGHTNESS` write until the next reconnect. `nearbyRev` bumps when the `nearby` section's app-visible fields change (everything the section emits except `lastSeenMs` and `rssi`, which churn ~30 Hz on every BLE adv), coalesced to the same ≤1 Hz roster-rebuild cadence; the app re-reads `nearby` on a bump instead of polling. Commit results (`{"commit":"ok"|"err"}`) also arrive on this characteristic.

**Slider-rate write channels (write / no-response, ~30 Hz):**
- `CHAR_BRIGHTNESS` (0xd2), write+wnr: 1 byte 0..100. Routed to `homeMode.brightness` while a Home-Mode edit session is focused.
- `CHAR_SHADE_COLORS` (0xd3), write+wnr: a JSON array of hex color strings (the broadcast segment), `{"seg":k,"colors":[...]}` (segment `k`), or `{"colors":[...],"rotate":{"on":true,"periodS":40,"dir":"up"}}`. Live preview into RAM config; nothing reaches NVS until `CHAR_COMMIT`.
- `CHAR_BASE_COLORS` (0xd4), write+wnr: a JSON array of hex color strings, or `{"colors":[...],"rotate":{...}}`. Same preview and commit rules.

  In the object forms `colors` is required and must be a non-empty array: an object without one is dropped whole, `rotate` included. `rotate` and each key in it are optional, and a key sets its value only when it has the right type: `on` a boolean, `periodS` an integer (read as 32-bit signed, clamped to 5..120 seconds per loop), `dir` the string `"up"` or `"down"` (physically up or down on that surface). A missing key, or one of another type or value, leaves that value as it was, and a write without `rotate` (the bare array, the `seg` form, the web config preview) leaves rotation alone. `rotate` applies only to a role with exactly one segment, and on the shade only with `seg` 0 or no `seg`; any other write applies its colours and ignores it. A shade `seg` that is not an integer 0-255 or names no segment the role has, or a base `seg` other than 0 (the base has no per-segment form), drops the write whole. Whether the variant renders rotation plays no part in what the write stores.
- `CHAR_BASE_KNOCKOUT` (0xd5), write+wnr: 2 bytes `[pixelIndex, brightness 0..100]`.
- `CHAR_HOME_MODE_FOCUS` (0xe5), write+wnr: 1 byte 0/1 — app sets 1 while the user is on the Home-Mode setup page (forces preview + reroutes brightness). Auto-cleared on connect and disconnect. Dropped in OTA quiet mode like every live-control write; the app sends it again once the lamp reports `quiet: false` (see `CHAR_STATE_NOTIFY`).
- `CHAR_EDIT_SESSION` (0xe9), write+wnr: 2 bytes `[selector, state]`, `state` != 0 opens, all cleared on disconnect. `selector` 0x01 base / 0x02 shade / 0x04 brightness are the operator-priority lockout against wisp overrides. `selector` 0x08 is the Social/Network view flag: while set (app on the Social page), the lamp reopens a low-duty passive BLE scan during the connected session — short windows (~30 ms scan / 300 ms gap, ~10% duty), skipped for `idleGuardMs` after any throughput-sensitive write and bounded by a safety timeout — so BLE-only (non-mesh) peers keep being sighted into the roster and surface in the `nearby` section. The connect-time scan pause (which protects write throughput) otherwise stops those sightings for the whole session.

**Commit:**
- `CHAR_COMMIT` (`48537d49-…`), write+wnr: parameterless commit signal — the *arrival* of the write is the signal (bytes ignored). Flushes pending slider/color writes to NVS off the NimBLE host task; result returns via `CHAR_STATE_NOTIFY`. Also force-flushed on disconnect.

**Op channels (write):**
- `CHAR_EXPRESSION_TEST` (0xd6), write+wnr: utf-8 expression type, empty = complete.
- `CHAR_EXPRESSION_OP` (0xd9), write: `{op: upsert/remove, ...}` JSON.
- `CHAR_WIFI_OP` (0xda), write: `{op: scan/forget}` JSON.
- `CHAR_REMOTE_OP` (0xe4), write: encrypted JSON carrying a top-level `targetMac`. Applied on this lamp (`brightness`, `shadeColors`, `baseColors`, `knockout`, `expressionOp`, render only) when `targetMac` is this lamp's MAC or `"broadcast"`; any other target is logged and ignored, never forwarded. No current app writes it; the characteristic stays because the GATT layout is frozen.
- `CHAR_SOCIAL_DISPOSITIONS` (0xe6), read+write: per-peer disposition map (1..5).
- `CHAR_SETTINGS_BLOB` (0xd7), write: full settings JSON (write path only; reads go through the page protocol below).

**Section reads — page protocol (replaces the former per-section read characteristics):**
- `CHAR_PAGE_CTRL` (0xdc), write: section name (`lamp` | `base` | `shade` | `expr` | `home` | `nearby` | `exprcat` | `wispclaims` | `wisppalette` | `wispstatus` | `wispconfig`). Snapshots that section's cached JSON (binary for `wispclaims` and `wisppalette`) into a per-connection buffer and resets the read cursor; `exprcat` is the exception — it points the read cursor straight at the flash-resident catalog (`kExprCatalog`), no per-connection copy. An optional trailing byte caps the chunk MTU.
- `CHAR_PAGE_DATA` (0xdd), read: returns the next chunk of the snapshot and advances the cursor. The app reads repeatedly until an empty chunk signals end-of-section (`readPagesUntilEmpty`); a short chunk is not the end, since the chunk size follows the link MTU. Per-connection cursor state, so concurrent phones don't collide.

An unknown section name clears the snapshot, so its first DATA read is empty; so is any read before the auth handshake. The wisp sections lean on that: `wispstatus` serves the `CHAR_WISP_STATUS` merge whole (no 512 B ceiling, nothing shed) and `wispconfig` the cached `wispConfig`, and both answer at least `"{}"` on a lamp that has them. The app reads `wispstatus` first and falls back to the direct `CHAR_WISP_STATUS` read when it comes back empty; one non-empty read marks the lamp as serving both, and so does any notify carrying `configLastSeenMs`, which only such a lamp merges, so a build whose reads all landed before the auth handshake still finds the sections. One residual: a status still over 512 B after shedding notifies an empty value, which carries no `configLastSeenMs`, so an app that has not yet marked the sections cannot recover that update from them. The app keeps showing the last good status meanwhile, since it drops an empty notify as transient; the next status that fits recovers it, or reconnecting to the lamp, whose fresh notifier re-runs the section read. Reopening the wisp screen does not: the lamp shell keeps the notifier alive, and the screen's polls only bring more empty notifies. `wisppalette` serves the cached manual palette raw (interleaved R,G,B,W), off the notify-clobbered status. `wispstatus` builds its JSON on the NimBLE host task, as the direct `CHAR_WISP_STATUS` read always has; the others copy a cached payload out under the roster mutex.

The `lamp` section is a JSON object of the lamp's own identity + settings: `name`, `brightness`, `password`? (only when set), `hasPassword`, `advancedEnabled`, `webappEnabled`, `brightnessCeiling`, `socialMode`, `fwVersion`, `fwChannel`, `catalogHash` (8-char hex FNV-1a over the flash expr catalog bytes, precomputed at build time; the app caches the `exprcat` payload per-hash and re-reads only when it changes), `lampType`, `lampId`? (this lamp's own mesh mac, canonical uppercase colon-hex via `formatBdAddr`, the same bytes peers store for it as `lampId` in their `nearby` section; omitted until the mesh link has come up), `otaState`? (this lamp's own OTA state, 0/1/2 = idle/sending/receiving, omitted when idle; same values as the `nearby` section's per-peer `otaState`), `otaSendingTo`? (the distribution target's mesh mac, canonical uppercase colon-hex; present only while `otaState` is sending, same field/semantics as the `nearby` section's per-peer `otaSendingTo`), `resetReason` (this boot's `esp_reset_reason_t` ordinal), `lastCrash`? (the persisted crash record, omitted until the lamp has crashed once; see [`embedded-heap.md`](embedded-heap.md#reading-a-crash-record)). `lampId` lets the app match a lamp against how its peers observe it. The webapp's `GET /api/settings` serves a different document: the full persisted config (`Config::asJsonDocument()`, with `lamp` / `base` / `shade` / `expressions` / `homeMode`), password stripped. Its `base` and `shade` objects carry `rotate` in the stored shape: for any one-segment role whose rotation differs from that role's default, whether or not the variant renders rotation, so the PUT carries it back. Its `lamp` object carries the stored settings plus `hasPassword`, `lampId`, and one webapp-only field, `apSecured`, whether the setup softAP came up WPA2-keyed with the lamp password (which needs 8-63 characters). The firmware-identity and runtime fields above (`fwVersion`, `fwChannel`, `catalogHash`, `lampType`, `otaState`, `otaSendingTo`, `resetReason`, `lastCrash`) are BLE-only. `apSecured` and `hasPassword` are both GET-only there; a PUT saves through the config model, which has no such fields. The PUT takes `application/json` (415 otherwise) of at most 16 KB (413 over it; 400 with no body), gathered into one block (500 when the heap cannot spare it, when the heap check before the parse refuses it, or when the parse runs out of memory) and parsed once, on the loop: a body that is not strict JSON (a single-quoted string, an unquoted key or bare word), has no `lamp` object, or carries a password or SSID over its cap is answered 400, and one sent while an earlier save waits for its reboot 409. The loop takes no web save while a firmware update is being received. `GET /api/dispositions` answers 503 when the lamp cannot spare the heap to build the map, never an empty `{}`: the editor PUTs the whole map back, so an empty one would erase every stored disposition. The BLE social-dispositions read answers `!` for the same reason, and the app writes no map back after a read that was not a JSON object. Those answers come while the handler waits for the loop (3 s); a save still running then is answered 202 and any refusal after that is only logged. When a password is set that cannot key the AP, the webapp's write routes and `/api/unlock` answer 403: an open network must not hand out config writes or the password itself.

The `base` and `shade` sections carry `rotate` (`{"on":…,"periodS":…,"dir":"up"|"down"}`, every key present) exactly when the role offers colour rotation: the variant sets `Config::Defaults::rotationSupported` and the role has one segment. Otherwise the key is absent, even when the role holds a stored rotation that is on, so the section never reports what the lamp will not render. Its presence is the app's whole signal that the role can rotate, as `colorsEditable` is for the colour picker. The section is rebuilt on commit, like the colours.

The `nearby` section is a JSON array of the lamp's roster, one flat object per peer: `name`, `lastSeenMs` (max of the two transport timestamps), `viaBle`, `viaEspNow`, `near` (`lastSeenNearMs` set and within `LAMP_PRUNE_TIME_MS`, the same test `LampRoster::snapshotNear` uses), `lampId`? (peer's mesh mac, canonical uppercase colon-hex; omitted when the roster entry has no mac yet), `rssi`? (omitted at the -127 sentinel), `shade`, `base`, `fwVersion`? (omitted when 0), `fwChannel` (peer's `{type}-{channel}` slot, empty string on legacy peers that never sent it), `otaState`? (omitted when idle). `shade`/`base` are 8 lowercase hex chars `rrggbbww`; the app also accepts the legacy 4-int RGBW array shape from deployed firmware. The array is capped to the `kNearbyMaxLamps` (25) most-relevant peers — near before mesh-only, then most-recently-seen, then strongest RSSI — so the contiguous build stays bounded regardless of fleet size; a crowd past that drops the farthest and stalest first (the app surfaces "there may be more"). The string is materialized on Core 1 (`ble_control::tick`) only while an app or webapp client is connected, rebuilt when the roster's generation counter changed (at most once per second), and freed the moment no client is connected — so a crowded lamp with no app open holds zero nearby-JSON heap. The Core 0 page snapshot copies the string under a mutex, matching the Config section caches.

**Mesh state mirrors:**
- `CHAR_WIFI_STATE` (0xdb), read+notify: JSON snapshot of WiFi/scan state.

**Firmware OTA (phone → lamp):**
- `CHAR_FW_CONTROL` (0xe7), write+notify: app writes `MSG_FW_*` control frames (OFFER/DONE) in lamp_protocol wire format; the lamp's receiver replies via notify on this same characteristic.
- `CHAR_FW_CHUNK` (0xe8), write+wnr: high-frequency `MSG_FW_CHUNK` stream. The app sizes the chunk payload to the negotiated BLE ATT MTU (`min(mtu-3-26, FW_CHUNK_SIZE_MAX)`, floored at 200 when the MTU is low or unreported) and carries the chosen size in the OFFER's `chunkSize`, matching the receiver's per-session `offerChunkSize_`.

**Schema + claims:**
- `CHAR_SCHEMA_VERSION` (0xea), read: single byte = `kGattSchemaVersion`. Absent on legacy lamps that predate it (app falls back to legacy behavior). No app consumer gates on it yet.
- `CHAR_WISP_CLAIMS` (0xeb), read, auth-gated (schema v2, v4 tail): binary blob `[count(1)][lampMac(6)]×count[colorPair(6)]×count[painted(1)+baseW(1)+shadeW(1)]×count`, `WispFleetCache::kBlobEntrySize` (15) bytes per lamp, built by `WispFleetCache::buildClaimsBlob`. Membership is the de-duplicated union of the fresh accumulated `MSG_WISP_CLAIM` entries and the fresh accumulated `MSG_WISP_STATE` entries (same 60 s per-entry staleness window for both, capacity 100 each); claim macs come first, paint-only macs follow. This means a lamp that arrived via paint before its claim message is not invisible. `count = 0` when everything is stale. Split from `CHAR_WISP_STATUS` because that payload is already near the MTU budget.
  - **Sections**: `colorPair` is `baseRGB(3)+shadeRGB(3)` and the paint section is `painted, baseW, shadeW`, both positionally aligned to the macs. `painted = 1` when the mac has fresh paint from `MSG_WISP_STATE`; its colours are then exact, black included, with W from the STATE W plane. `painted = 0` when it has none, with all colour and W bytes zero.
  - **Compatibility**: every section is appended after the ones before it, so the GATT layout is unchanged. A reader that checks `length >=` for the sections it knows ignores the rest. An app that knows only `[count][mac][colorPair]` treats an all-zero pair as "no live colour, predict instead". `parseWispClaims` (app) uses the paint section when the whole of it is present (`length >= 1 + count×15`): a painted mac maps to its RGBW colours, black included, and an unpainted mac is omitted so the preview falls back to its predicted colour. Without a complete paint section (an older lamp, or a truncated read) it applies the older rule: RGB with W = 0, an all-zero pair omitted. A partial or absent colour section reads as the mac-only blob.
  - **Size**: a characteristic value over the vendored 512 B ATT ceiling reads back empty, so the direct read (`WispClaimsCallback`, what `WispRepository.readClaims` reads) builds into a 512 B buffer. `buildClaimsBlob` fits as many entries as the `[count][mac][colorPair]` layout holds (42) and adds the paint section only when it fits too (up to 34 entries); past 34 the read carries no paint section and the app applies the older rule. The `wispclaims` page section serves the same format from a buffer sized for `WispFleetCache::kCapacity` (100) entries with the paint section, and the same fit rule applies past that.

**Wisp proxy**:
- `CHAR_WISP_OP` (`5f64f4e1-...`), write, auth-gated. Sealed or plaintext payload unicast verbatim as `MSG_CONTROL_OP` to the paired lamp's display-slot wisp. The relay lamp passes the payload through opaque; the wisp handles decryption. No new wire-format msg type was added; the routing rides the existing `char`-tagged JSON envelope. Lamps do not apply it locally. See wispOp framing details above.
- `CHAR_WISP_STATUS` (`5f64f4e2-...`), read+notify, auth-gated. Returns the lamp's cached `wispStatus` JSON (sourced from gossip-relayed `MSG_CONTROL_OP` broadcasts the wisp emits) merged with the last `MSG_WISP_HELLO` data on file (`wispMac`, `wispVersion`, `helloFlags`, `helloPaletteIdPrefix`, `helloLastSeenMs`), `statusLastSeenMs`, and `configLastSeenMs` once a `wispConfig` is cached, capped at 512 B by shedding the diagnostics (see the wispStatus budget); the `wispstatus` page section serves it whole. Notify fires whenever a new wispStatus, a changed wispConfig or a palette lands on the lamp, and on the presence edge when a `MSG_WISP_HELLO` first establishes (or a rival takes over) the display slot, so a wisp that comes online after the app connected surfaces live without an app restart. Steady-state 2 s keepalive hellos do not re-notify. The lamp caches exactly one wisp; admission is sticky-first-heard: a rival wisp's hello/status/palette/paint broadcasts are rejected until the current wisp misses 6 of its 2 s hellos (12 s window), except a `MSG_WISP_CLAIM` naming this lamp, which takes the slot immediately when the current wisp does not claim it. Adoption itself stamps the 12 s window, so a just-adopted wisp (claim takeover, first paint) is sticky before its first hello is cached and the displaced wisp's next broadcast cannot re-take the slot. This keeps two overlapping wisps from flip-flopping the app's wisp view at zone boundaries.

## Sequence diagrams

### Lamp A triggers an expression (MSG_COMMAND cascade fan-out)

```
lamp A                       mesh                            lamp B (in range)
  │                            │                              │
  │── expression auto-fires    │                              │
  │── ExpressionManager        │                              │
  │   .maybeCascade()          │                              │
  │── build ExpressionInvocation                              │
  │── sort LampRoster by RSSI desc                           │
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

### Wisp paints a lamp's base + shade (declarative broadcast)

```
wisp                                mesh                    lamp
  │                                   │                       │
  │── OSC / manual palette change     │                       │
  │── TupleSampler.assign(macA, p)    │                       │
  │   → {baseColor, shadeColor}       │                       │
  │── roster.setLampPaint(A, base, shade)  (no wire send)     │
  │                                   │                       │
  │── MSG_WISP_STATE broadcast ──────►│── ESP-NOW broadcast ─►│  (whole claimed set)
  │   sourceMac=wisp                  │                       │
  │   entries[...]: {A, base, shade}, │                       │
  │                 {B, ...}, ...      │                       │
  │                                   │                       │── parse, pending slot
  │                                   │                       │── Core 1 drainWispState:
  │                                   │                       │   sourceMac == display wisp?
  │                                   │                       │   find own MAC in entries →
  │                                   │                       │   compositor.applyWispState(
  │                                   │                       │     base, shade)
  │                                   │                       │── wisp LayerStack eases in;
  │                                   │                       │   presence holds while fresh
  │                                   │                       │
  │── source→Off, or OSC stream stale:│                       │── own MAC absent in fresh
  │   MSG_WISP_STATE with zero entries│                       │   frame → clearWispState,
  │   (event-driven)                  │                       │   ease home now
```

In steady manual mode STATE re-covers the whole claimed set every ~2 s, well
inside the `kWispStateFreshMs` (60 s) failsafe. The failsafe firing is the
silent-wisp path — a wisp that has actually gone away — not the normal hold.
An Off (or unclaim) eases the lamp home the moment the first fresh STATE omits
it, and a paint-mode edge emits STATE out of cadence so that lands promptly.

The combined-frame model (`surface=BaseAndShade, numColors=2`) packs
both surfaces into a single ESP-NOW frame. Delivery is atomic: both
colors land or neither does, so base and shade can't diverge from
per-surface frame loss under BLE coex.

**Observability**: the lamp's `[wispcoex]` meter
(`components/network/mesh/wisp_coex.hpp`, fed from `mesh_link.cpp`) logs a
periodic per-wisp `recv`/`maxgap` line on `LAMP_DEBUG` builds. `maxgap`
(wall-clock between hellos) climbing toward the `kWispStateFreshMs` (60 s)
failsafe is the signal that the STATE paint stream itself (not just hellos) is
at risk of a drop; see `qa/coex.md`. There is no `loss%`/`missed`: the wisp
shares one seq counter across all its message types, so a per-message-type
seq gap is not loss, and `maxgap` is the real presence-stability signal. The
STATE stream is measured by the `[wispstate]` meter
(`components/network/mesh/wisp_state.hpp`, fed from the `MSG_WISP_STATE` recv
arm and scoped to the display wisp): a 30 s
`[wispstate] win=30s recv=N adopts=A releases=R selfPresent=P` line
(piggybacked on the `[meshmix]` window) plus an edge-only
`[wispstate] ADOPT|RELEASE src=… seq=N` line on each paint transition (seq is
just the frame identifier there). Whether the lamp reliably sees the wisp
take / release control reads directly off the adopt/release edges and counts.

### Phone picks the wisp's Straylight role via mesh proxy

```
phone        lamp A (paired)             mesh                    wisp
  │              │                        │                       │
  │── BLE write ─►│                        │                       │
  │  CHAR_WISP_OP│                        │                       │
  │  {char:wispOp│                        │                       │
  │   op:setOscRole                       │                       │
  │   role:fill} │                        │                       │
  │              │── MSG_CONTROL_OP ──────►│── ESP-NOW unicast ───►│
  │              │   payload: same JSON   │                       │
  │              │                        │                       │── controlOpDedup_.record
  │              │                        │                       │── WispOpDispatcher.dispatch
  │              │                        │                       │── WispConfig
  │              │                        │                       │   .setOscRole(Fill)
  │              │                        │                       │   (NVS: "wisp"/oscRole)
  │              │                        │                       │── listen on
  │              │                        │                       │   /straylight/palette/
  │              │                        │                       │   fill/colours
  │              │                        │                       │── StatusEmitter
  │              │                        │                       │   .triggerOnChange()
  │              │                        │── ESP-NOW broadcast ◄─│
  │              │                        │   MSG_CONTROL_OP
  │              │                        │   {char:wispStatus,
  │              │                        │    source:"osc",
  │              │                        │    oscRole:"fill",
  │              │                        │    ...}
  │              │── peekWispBroadcast ◄──│                       │
  │              │   char=="wispStatus"  │                       │
  │              │── LampRoster           │                       │
  │              │   .cacheWispStatus(    │                       │
  │              │     wispMac, json)     │                       │
  │              │── ble_control          │                       │
  │              │   .notifyWispStatus()  │                       │
  │── BLE notify ◄│                        │                       │
  │  CHAR_WISP_  │                        │                       │
  │  STATUS      │                        │                       │
  │  {oscRole:"fill",                    │                       │
  │   sourceLive:...,...}                 │                       │
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

`MSG_FW_OFFER` is 56 bytes fixed body plus an **additive 96-byte auth trailer** (`FW_OFFER_AUTH_SIZE == 152`): `[56] digest[32]` / `[88] signature[64]`. The signed image's full 32-byte SHA-256 digest + the 64-byte ed25519 signature from the LSIG footer. The receiver ed25519-verifies sig-over-digest against `kFirmwarePubkey` **at offer time, before accepting or erasing** — an unsigned / foreign-key / tampered offer (or a legacy offer with no trailer) is declined with `FwAcceptStatus::DeclineUnverified` and nothing streams. This stops the loop where an unverifiable image streamed in full only to fail end-of-stream verify. The trailer is additive (no `PROTOCOL_VERSION` bump): a pre-trailer sender emits only the 56-byte body and a pre-trailer receiver stops parsing at byte 56, keeping its end-of-stream verify as the gate. The distributor refuses to enable OTA unless its own running image self-verifies against `kFirmwarePubkey` at boot, so an unsigned lamp never offers. Post-stream, `verifyAndApply` binds the streamed image's computed digest to the offered (verified) digest, so a source can't offer one image and stream another.

The **FS-image OTA** (`MSG_FS_*`, the SPIFFS web-UI image) reuses this receiver/distributor engine and every MSG_FW_* frame layout, distinguished only by its own msgType IDs: `MSG_FS_OFFER` (0x46), `MSG_FS_ACCEPT` (0x47), `MSG_FS_CHUNK` (0x48), `MSG_FS_REQ` (0x49), `MSG_FS_DONE` (0x4A), `MSG_FS_RESULT` (0x4B). An older lamp that doesn't recognize these drops them as an unknown msgType (no `PROTOCOL_VERSION` bump). It carries the **same 152-byte auth trailer**: the offer's `digest` is the FS manifest digest and `signature` is the `fw.lsig` ed25519 signature over it (same key as firmware). The receiver ed25519-verifies the offer **before** it unmounts SPIFFS and erases the live web UI, so a forged FS offer can't wipe the UI ahead of the post-write verify — the signature is the FS erase-DoS boundary. A pre-trailer (56-byte) FS offer has no signature and is declined. Post-write, `fsVerify` binds the recomputed manifest digest to the offered digest before trusting the embedded signature. The FS OFFER channel field is the unauthenticated wire channel (`fw.lsig` has no channel), so it guards accidental cross-variant seeding, not forgery. Like firmware, FS offer-auth is a static signature over the digest with no replay nonce; a replayed authentic FS offer just re-offers the real image.

See `software/lamp-os/src/components/firmware/` (distributor + receiver) and [`../../scripts/README.md`](../../scripts/README.md) for the signed-image OTA model.

### A/B slots and USB re-flash

The lamp partitions two app slots (`ota_0`/app0, `ota_1`/app1) plus an `otadata` partition that selects which one boots. Each mesh OTA writes the *inactive* slot and flips `otadata` (app0↔app1 ping-pong). The USB flash tasks (`lamp:flash`, `lamp:flash:release`) only ever write **app0** — so a lamp that last OTA-booted app1 keeps booting the stale app1 and the flash lands invisibly in app0. Both tasks therefore erase `otadata` after the write (offset/size read from `partitions.csv`), which makes the 2nd-stage bootloader default back to `ota_0`. `otadata` is separate from `nvs`, so name/config survive. The **web installer** (update.lamplit.ca / `manifest_*.json`) is immune without a reset: it flashes the whole merged image at offset 0, and `esptool merge-bin` leaves the `otadata` region 0xFF, which the bootloader reads as "boot `ota_0`". It also carries `new_install_prompt_erase`, so the web path wipes NVS/name — unlike `lamp:flash:release`, which preserves it. The **wisp** never receives mesh OTA, so its `otadata` never flips and its USB flash needs no reset.

### RSSI gate

`MSG_FW_OFFER`/`MSG_FS_OFFER` are single-hop unicast (no relay), so a weak direct link thrashes or fails a transfer regardless of chunk size. Both sides gate on `kOtaMinRssiDbm` (`components/network/protocol/fw_ota.hpp`, currently -80 dBm): the distributor (`considerPeerForOta`) skips offering below the floor using the peer's ESP-NOW HELLO RSSI (`LampRoster::espnowRssi`, distinct from the BLE-scan `lastRssi`); the receiver (`MeshLink::handleRecv`) independently drops an inbound OFFER whose frame RSSI is below the floor. The -127 "unknown RSSI" sentinel is never gated (allow). This is local, not on the wire — no `PROTOCOL_VERSION` involvement. Gating a weak direct hop doesn't strand a peer: cascade OTA still reaches it later via a nearer already-upgraded lamp.

### Serve-once coordination

A legacy receiver erases + accepts on every fresh `MSG_FW_OFFER`, so two distributors offering the same peer concurrently stomp each other and the transfer never completes. Before offering, the distributor skips a peer the mesh already shows as being served: the peer's own `HELLO_TLV_OTA_STATE` reads receiving, or some roster peer reports `HELLO_TLV_OTA_STATE` = sending with `HELLO_TLV_OTA_SENDING_TO` == that peer. The caller (`tickOtaOffers`, `components/firmware/ota_offer.cpp`) computes this from the roster (which holds peers only, so a lamp never counts its own in-progress send) and passes it to `considerPeerForOta`. Local, roster-derived — no wire change.

### Offer scan and the inter-session hold

`tickOtaOffers` (`components/firmware/ota_offer.cpp`) runs in `Lamp::tick` on every variant once the compositor's startup phase has ended, at most once a second (`OtaOfferThrottle::kIntervalMs`). It walks the ESP-NOW roster and calls `considerPeerForOta` and `fs_ota::considerPeer` per peer, unless an OTA is moving in either direction for either image, a reachable peer runs newer firmware (receive-first), or OTA quiet mode is held. One quiet holder is exempt: after a session that entered quiet ends, the distributor keeps its quiet for `InterSessionQuietHold::kHoldMs` (10 s, `components/firmware/inter_session_quiet_hold.hpp`). When that session ended Done, the scan runs during the hold, so the next offer arms inside it and inherits its quiet, and the indicator stays up through a successful wave instead of flashing back to normal between sessions. The exemption needs the distributor to be the sole holder (`ota_quiet_mode::holders()` is 1 and `FirmwareDistributor::holdingIdleQuiet()`, which is true only for a hold armed by a Done session); quiet from a received OTA, a BLE-pushed OTA or an FS session still blocks the scan. A session that failed or timed out arms a hold the scan does not run through, so its quiet ends 10 s after it as before, and a crowd of peers that never accept cannot keep the lamp quiet. While the exempt hold runs the scan skips the FS walk, since an FS session armed inside it would inherit the firmware session's visible indicator. The gates are `offerBusy` and `OtaOfferBusy::fsWalkAllowed` in `ota_offer.hpp`, pure and natively tested, including a simulated wave of peers that never accept.

### Channel promotion

A beta lamp graduates to stable via OTA when a stable-channel distributor with a version >= the beta peer's version is present. The decision is one-directional: beta accepts stable, stable never accepts beta. The `{type}` prefix (`standard`, `snafu`) must match exactly at every checkpoint; cross-variant OTA is always rejected.

Three checkpoints gate each transfer:

1. **Distributor offer** (`considerPeerForOta`): calls `otaAcceptable(peerChannel, peerVersion, ourChannel, ourVersion)` — answers "would the peer accept our firmware?" Stable distributor at equal version offers to beta peer; beta distributor never offers to stable peer.
2. **Receiver accept** (`onOfferOnLoop`): calls `otaAcceptable(ourChannel, ourVersion, offerChannel, offerVersion)` — answers "should we accept this offer?" Beta lamp accepts stable offer at `offerVersion >= ourVersion`; stable lamp rejects beta offer unconditionally.
3. **Boot-flip type-gate** (`verifyAndApply`): re-checks the verified LSIG footer channel before setting the boot partition. Uses `otaAcceptable(ours, 0, footerChannel, 1)`; the `0 < 1` version pair satisfies both the intra-channel (`>`) and promotion (`>=`) rules, so a same-channel or beta→stable footer passes while a cross-variant or wrong-suffix footer is rejected.

No protocol bump is required: channel (`HELLO_TLV_FW_CHANNEL`) and version (`firmwareVersion` in the HELLO body) already ride existing v0x05 HELLO fields. The `MSG_FW_OFFER` wire format already carries the 16-byte channel field. The promotion decision is purely local gating on already-available information.

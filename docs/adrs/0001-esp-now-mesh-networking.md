# ADR 0001 — ESP-NOW broadcast mesh for lamp-to-lamp networking

*Supersedes the old `main`-branch system's networking.*

## Context

The lamp fleet (~22 lamps) is an **unconducted fleet**: there is no central
coordinator, no guaranteed router/AP, and lamps power on/off and move around a
venue freely. Lamps must talk to each other to:

- announce presence + identity (who's nearby, what version, what colour),
- synchronise expressions / cascade events across the fleet,
- carry authenticated control commands,
- distribute firmware (OTA) lamp-to-lamp,
- subscribe to the wisp's palette + status beacons.

Forces:

- **No infrastructure dependency.** A venue may have no WiFi, or flaky WiFi.
  The fleet must self-organise on its own radio.
- **Low latency + low join cost.** A lamp arriving should be "on the mesh"
  within a beacon interval, with no association/handshake.
- **Single radio, shared with BLE.** Each lamp also runs a BLE GATT control
  service for the Flutter app. ESP-NOW and BLE coexist on one antenna.
- **Small fleet, broadcast-natural traffic.** Presence + events are inherently
  one-to-many.

## Decision

Use **ESP-NOW broadcast** as the lamp-to-lamp transport, with a custom wire
protocol layered on top.

- **Connectionless broadcast.** Frames are broadcast to `FF:FF:FF:FF:FF:FF`;
  there is no pairing, association, or per-peer connection. A lamp joins the
  mesh simply by powering on and broadcasting `MSG_HELLO`.
- **Custom framed protocol** (`lamp_protocol.hpp`), one `lamp_protocol.hpp`
  mirrored on the wisp. Message families: `HELLO`/`WISP_HELLO` (presence),
  `EVENT` (cascades), `CONTROL_OP` (authenticated commands), `WISP_*`
  (palette/claim), `FW_*` (OTA). Every frame carries a 2-byte magic + a
  protocol-version byte + a message-type byte.
- **Gossip relay** for the message types that need fleet-wide reach beyond a
  single hop: a lamp re-broadcasts a relayable frame on first sight, deduped by
  **per-message-type** 64-slot `DedupRing`s keyed on `(sourceMac, msgType,
  seq)`. Relayed: `MSG_HELLO`, `MSG_CONTROL_OP`, `MSG_WISP_HELLO`,
  `MSG_WISP_PALETTE`, `MSG_EVENT` — so presence and beacons reach lamps beyond
  direct radio range, not just immediate neighbours. *Not* relayed (single-hop):
  the transient unicast `OVERRIDE_*`/`RESTORE_*` paint frames (addressed, with
  driver-level retries) and `WISP_CLAIM`.
- **Receive-range protocol versioning** (see `networking.md` and the protocol
  lock-in in `CLAUDE.md`): we broadcast `PROTOCOL_VERSION_EMIT` but parse a *range*
  `[RX_MIN, RX_MAX]`, so the fleet can receive a newer wire format before it
  emits one — the safe path for a multi-version OTA wave.

## Alternatives considered

- **WiFi AP-based mesh (ESP-WIFI-MESH / ESP-MESH-LITE / painlessMesh).**
  Self-forms a routed tree over WiFi APs. Rejected: heavier (each node is an
  AP+STA), routing + association overhead, higher join latency, and it fights
  harder with BLE coex. We don't need multi-hop routing for a room-scale fleet
  — single-hop + selective gossip covers it.
- **BLE mesh.** Rejected: lower throughput (a problem for OTA), more complex
  provisioning, and BLE is already committed to the app-control role.
- **A central hub / router (e.g. the wisp as the only relay).** Rejected as the
  *primary* transport: reintroduces an infrastructure single-point. The wisp
  participates in the mesh but the fleet does not depend on it to talk to
  itself.

## Consequences

**Good:**

- Zero infrastructure: the fleet works in an empty field.
- Instant join, low latency, broadcast-natural for presence/events.
- One protocol, code-shared (mirrored) between lamp and wisp.

**Costs we live with (and which shape later decisions):**

- **250-byte ESP-NOW MTU.** Anything larger (firmware images) must be chunked +
  reassembled — see ADR 0002.
- **Broadcast is unreliable + unacknowledged.** There is no per-frame ACK, so
  any reliable transfer (OTA) needs its own recovery (REQ/NAK) + dedup. Loss is
  scattered and silent.
- **No routing.** Reach beyond one hop is *only* via explicit gossip relay
  (dedup-ringed), applied per message type — presence, control ops, wisp
  beacons, and events relay; the transient unicast paint frames do not.
- **BLE coexistence on one radio.** ESP-NOW airtime competes with BLE; during
  OTA we explicitly pause the BLE radio (quiet mode) to give the transfer the
  antenna.
- **Wire format is a frozen contract.** Because there's no negotiation,
  changing fixed offsets/semantics is a breaking change requiring a protocol
  version bump; additive fields ride as TLVs instead (TLV-first rule).

## References

- `docs/dev/networking.md` — authoritative wire-format reference.
- `software/lamp-os/src/components/network/protocol/lamp_protocol.hpp` (+ wisp mirror).
- ADR 0002 — OTA over this mesh.

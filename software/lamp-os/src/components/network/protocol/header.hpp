#pragma once

#include <cstdint>
#include <cstring>

// =============================================================================
// header.hpp — the 6-byte frame prefix every message shares, plus the
// version scheme and the MsgType registry.
// =============================================================================
//
// ON-WIRE FRAME HEADER (HEADER_SIZE == 6 bytes), prepended to EVERY message:
//
//   off  size  field
//    0    1    MAGIC_0 ('L')
//    1    1    MAGIC_1 ('M')
//    2    1    protocol version (PROTOCOL_VERSION_EMIT on emit; inspect()
//              accepts [PROTOCOL_VERSION_RX_MIN, PROTOCOL_VERSION_RX_MAX])
//    3    1    msgType (MsgType enum here; FW/FS types live in fw_ota.hpp)
//    4    1    seq low byte  (uint16 little-endian)
//    5    1    seq high byte
//
// Entry points: detail::writeHeader (emit), inspect() (validate magic +
// version, returns the msgType byte or 0). Covers no MSG_* type on its own —
// it is the shared prefix + the discriminator table the other families use.

namespace lamp_protocol {

constexpr uint8_t MAGIC_0 = 'L';
constexpr uint8_t MAGIC_1 = 'M';

// =============================================================================
// Protocol versioning
// =============================================================================
//
// The version byte at `data[2]` of every frame discriminates wire
// formats. inspect() gates incoming frames against this byte before any
// field read so an older parser running against a newer-format frame
// can't underflow.
//
// Two constants intentionally — not one. Splitting "what we emit" from
// "what we accept on receive" is what makes cross-version rollouts
// possible at all. With a single PROTOCOL_VERSION constant a fleet
// migration becomes impossible: old lamps drop the new version, new
// lamps drop the old version, and there's no path from any one to any
// other except a manual USB re-flash of every device.
//
//   PROTOCOL_VERSION_EMIT   — what we put on the wire (broadcast HELLO,
//                             default outbound frames). Held at the
//                             OLDEST supported version so older fleet
//                             members can still see us. New features
//                             landed via TLV trailers ride along
//                             regardless.
//
//   PROTOCOL_VERSION_RX_MAX — newest version we know how to parse.
//                             inspect() accepts data[2] in
//                             [EMIT, RX_MAX] inclusive. We can grow our
//                             receive range ahead of our emit range so
//                             new-version peers see us responsive
//                             before we start emitting the new version
//                             ourselves.
//
// -----------------------------------------------------------------------------
// Upgrade workflow
// -----------------------------------------------------------------------------
//
// Bumps come in two shapes — TLV-additive (most of them, by design) and
// genuine layout-breaking. Treat them differently:
//
// (a) TLV-additive bump — adding a new field that fits inside an
//     existing HELLO/WISP_HELLO TLV trailer:
//       - Bump PROTOCOL_VERSION_RX_MAX to the new value.
//       - That's it. Old parsers ignore unknown TLV types by length;
//         new parsers handle the new type. No new code paths needed.
//
// (b) Layout-breaking bump — a fixed-offset field changes width,
//     position, or meaning in any of these messages: MSG_FW_OFFER,
//     MSG_FW_CHUNK, MSG_FW_DONE, MSG_FW_ACCEPT, MSG_FW_REQ,
//     MSG_FW_RESULT, MSG_HELLO (fixed part), MSG_WISP_HELLO (fixed
//     part). Steps:
//       1. Bump PROTOCOL_VERSION_RX_MAX. Don't yet touch EMIT.
//       2. Add a `legacy_v0xNN::` namespace next to lamp_protocol's
//          top-level builders/parsers (NN = the OLD version we're
//          leaving behind). Move the existing implementation of the
//          7 OTA-related functions into it (see below). The new
//          version's implementation becomes the top-level one.
//       3. Modify the top-level builders for the 7 OTA messages to
//          take an explicit `version` argument; default it to
//          PROTOCOL_VERSION_EMIT. The distributor passes the target
//          peer's version when known (learned from their HELLO).
//       4. Ship the transitional release. Fleet receives + propagates
//          OTAs. Old peers stay reachable because OFFER/CHUNK/DONE
//          are routed through `legacy_v0xNN::` for them.
//       5. After the fleet is fully migrated, the NEXT release bumps
//          PROTOCOL_VERSION_EMIT and deletes the `legacy_v0xNN::`
//          namespace.
//
// -----------------------------------------------------------------------------
// Legacy OTA support — required surface
// -----------------------------------------------------------------------------
//
// A `legacy_v0xNN::` namespace only needs the messages required to OTA
// an older-version peer. Everything else can be skipped — once the OTA
// completes the peer reboots into the new firmware and speaks the new
// protocol natively. The required surface, in both directions:
//
//   parse legacy_v0xNN::parseHello       (incoming, learn peer + version)
//   parse legacy_v0xNN::parseFwAccept    (incoming, OTA handshake)
//   parse legacy_v0xNN::parseFwReq       (incoming, chunk retransmit)
//   parse legacy_v0xNN::parseFwResult    (incoming, OTA outcome)
//   build legacy_v0xNN::buildFwOffer     (outgoing, initiate OTA)
//   build legacy_v0xNN::buildFwChunk     (outgoing, send bytes)
//   build legacy_v0xNN::buildFwDone      (outgoing, signal end of stream)
//
// What the legacy namespace does NOT need:
//   - Override colors / paint distribution / cascade gossip relays
//   - Wisp claims / wisp paint / wisp HELLO
//   - Event messages / dedup ring entries
//   - Any non-OTA message type
//
// All of those just don't get sent to the legacy peer in their old
// format. The old peer never sees them. After the OTA they land at the
// new version and the new code paths handle everything natively.
//
// -----------------------------------------------------------------------------
// Current state
// -----------------------------------------------------------------------------
//
// EMIT=0x05, RX 0x04..0x05. v0x05 added a TLV trailer to HELLO/WISP_HELLO.
// We accept v0x04 frames on receive so old-fleet peers remain visible
// (NearbyLamp.protocolVersion drives per-peer OTA-OFFER routing at their
// version). No `legacy_v0x04::` namespace is needed — the v0x04 and v0x05
// wire formats are byte-identical for every message other than the TLV
// trailer appended in v0x05.

// What we emit by default on broadcast frames (HELLO etc).
constexpr uint8_t PROTOCOL_VERSION_EMIT   = 0x05;

// inspect() accepts frames in [RX_MIN, RX_MAX]. RX_MIN is the oldest
// version we still know how to parse on receive. Set BELOW EMIT to
// keep seeing old-fleet HELLOs — that lets us populate
// NearbyLamp.protocolVersion for those peers, which then drives
// per-peer OTA-OFFER routing at THEIR version. When a future release
// drops legacy receive support, raise RX_MIN to current EMIT.
constexpr uint8_t PROTOCOL_VERSION_RX_MIN = 0x04;
constexpr uint8_t PROTOCOL_VERSION_RX_MAX = 0x05;

// Alias for callers that only need the emit version.
constexpr uint8_t PROTOCOL_VERSION = PROTOCOL_VERSION_EMIT;

enum MsgType : uint8_t {
  MSG_HELLO               = 0x01,
  // Forwarded BLE control write. Payload is JSON tagged with a `char` field
  // naming the local control surface to invoke (brightness, shadeColors,
  // baseColors, knockout, expressionOp, settings, ...). The local
  // pending-slot post functions handle the routing; downstream drain in
  // loop() runs unchanged.
  MSG_CONTROL_OP          = 0x03,
  // --- Wisp + transient overrides + events ---
  MSG_WISP_HELLO          = 0x20,  // wisp presence beacon
  MSG_OVERRIDE_COLORS     = 0x21,
  MSG_RESTORE_COLORS      = 0x22,
  MSG_OVERRIDE_BRIGHTNESS = 0x23,
  MSG_RESTORE_BRIGHTNESS  = 0x24,
  // Wisp-to-wisp claim broadcast. Carries `[(lampMac, rssi)]` entries
  // for every lamp the sending wisp currently claims, at the RSSI the
  // wisp hears that lamp. Gossip-relayed by lamps the same way
  // MSG_WISP_HELLO is, so the shared claim view propagates across the
  // mesh regardless of whether two wisps can directly hear each other.
  // Lamps don't otherwise act on this message — it's purely wisp-
  // coordination.
  MSG_WISP_CLAIM          = 0x25,
  // Wisp's manualPalette broadcast. Carries up to kMaxWispPaletteColors
  // RGB triples packed binary. Lamps cache the latest, gossip-relay once
  // per (mac, seq), and serve it back to apps inside the wispStatus BLE
  // characteristic JSON as a base64 blob. The wisp is the single source
  // of truth for the palette so all apps see the same value regardless of
  // which lamp they're connected to. Cadence: piggybacked on the 30 s
  // emitStatus() tick, plus an on-change emit from WispOpDispatcher.
  MSG_WISP_PALETTE        = 0x26,
  // Per-lamp paint colors broadcast by the wisp. Each entry carries the lamp's
  // current base + shade RGB so the app preview reflects drift, not just the
  // deterministic newcomer prediction.
  MSG_WISP_PAINT          = 0x27,
  MSG_EVENT               = 0x30,
  // Targeted expression invocation. Lamp directs a specific nearby lamp to
  // run an expression. No gossip relay; addressedToUs filter on recv.
  MSG_COMMAND             = 0x31,
};

// High bit on msgType. inspect() does not mask it; any frame that sets it
// surfaces as an unrecognised type, making future reuse diagnosable.
constexpr uint8_t kReservedMsgTypeHighBit = 0x80;

constexpr size_t HEADER_SIZE = 6;

namespace detail {

// Write the 6-byte header (magic + version + msgType + seq LE) to `buf`.
// `wireVersion` defaults to PROTOCOL_VERSION_EMIT; OTA-specific builders
// (FW_OFFER/CHUNK/DONE/ACCEPT/REQ/RESULT) pass an explicit version to
// emit at the peer's protocol version — see the doc block above
// PROTOCOL_VERSION_EMIT for the per-peer-version OTA model.
inline void writeHeader(uint8_t* buf, uint8_t msgType, uint16_t seq,
                        uint8_t wireVersion = PROTOCOL_VERSION_EMIT) {
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = wireVersion;
  buf[3] = msgType;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
}

}  // namespace detail

// Validate magic + version. Returns the msg type byte verbatim or 0 if
// invalid. The high bit is not masked; any future reuse surfaces
// immediately as an unrecognised msgType.
inline uint8_t inspect(const uint8_t* data, size_t len) {
  if (!data || len < HEADER_SIZE) return 0;
  if (data[0] != MAGIC_0 || data[1] != MAGIC_1) return 0;
  // Accept any version in [RX_MIN, RX_MAX]. See the doc block above
  // PROTOCOL_VERSION_EMIT for the legacy receive model.
  if (data[2] < PROTOCOL_VERSION_RX_MIN || data[2] > PROTOCOL_VERSION_RX_MAX) {
    return 0;
  }
  return data[3];
}

}  // namespace lamp_protocol

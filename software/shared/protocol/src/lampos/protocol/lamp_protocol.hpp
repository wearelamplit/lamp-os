#pragma once

// Shared wire format for the ESP-NOW mesh (lamp + wisp). This umbrella
// aggregates the per-family headers of the shared wire core. Both projects
// pull it via `#include <lampos/protocol/lamp_protocol.hpp>`. The lamp
// firmware layers its own families (OTA/FS, directed command/color, event) on
// top in its own umbrella shim; the wisp needs only this core. Each family
// header opens with an on-wire byte-map for its frame(s).
//
// Which header owns which message types:
//   header.hpp      the shared 6-byte frame header, MsgType registry,
//                   version scheme, inspect()/writeHeader.
//   presence.hpp    MSG_HELLO (0x01), lamp beacon.
//   control_op.hpp  MSG_CONTROL_OP (0x03), forwarded BLE write.
//   wisp.hpp        MSG_WISP_HELLO (0x20), MSG_WISP_CLAIM (0x25),
//                   MSG_WISP_PALETTE (0x26), MSG_WISP_PAINT (0x27).
//   override.hpp    MSG_OVERRIDE_COLORS (0x21) / RESTORE_COLORS (0x22) /
//                   OVERRIDE_BRIGHTNESS (0x23) / RESTORE_BRIGHTNESS (0x24).
//   dedup_ring.hpp  DedupRing (gossip-relay dup suppressor; not a message).
//
// The version byte at data[2] is a receive range, not a single number.
// PROTOCOL_VERSION_EMIT goes on the wire; inspect() accepts
// [PROTOCOL_VERSION_RX_MIN, PROTOCOL_VERSION_RX_MAX]. Splitting emit from
// receive lets the fleet receive a newer version before it emits one, the
// safe path through a multi-version OTA wave. Additive fields ride as
// HELLO/WISP_HELLO TLV trailers (unknown types are skipped by length) so most
// changes need no version bump.
//
// Include order is a dependency chain. header first (writeHeader / inspect /
// MsgType); control_op + presence before MAX_PACKET_SIZE below (it aggregates
// their max sizes). Each header also includes its own deps, so correctness
// doesn't rely on this order alone.

#include <cstdint>
#include <cstring>

#include <lampos/protocol/header.hpp>
#include <lampos/protocol/presence.hpp>
#include <lampos/protocol/control_op.hpp>
#include <lampos/protocol/wisp.hpp>
#include <lampos/protocol/override.hpp>
#include <lampos/protocol/dedup_ring.hpp>

namespace lamp_protocol {

// ESP-NOW v2 payload ceiling (matches ESP_NOW_MAX_DATA_LEN_V2). A named
// literal, not an esp_now.h include, so this header stays transport-agnostic
// for the native test build.
constexpr size_t ESPNOW_V2_FRAME_MAX = 1470;

// Scratch-buffer size for building/parsing CONTROL_OP or HELLO frames (the
// two families that share this sizing); NOT a receive buffer, no production
// code copies an incoming frame into a MAX_PACKET_SIZE-sized buffer. The mesh
// RX path (MeshLink::handleRecv, FW chunk included) reads straight out of the
// driver-owned buffer the transport hands it. max() over the candidates so
// shrinking either family doesn't silently shrink callers still sized off
// this constant.
constexpr size_t MAX_PACKET_SIZE =
    CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE;

}  // namespace lamp_protocol

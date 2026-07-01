#pragma once

// =============================================================================
// lamp_protocol — wire format for lamp <-> lamp ESP-NOW broadcast.
// =============================================================================
//
// This umbrella aggregates the per-family headers below; every existing
// consumer still includes THIS path (`.../protocol/lamp_protocol.hpp`)
// unchanged. Each family header opens with an on-wire byte-map for its
// frame(s) — start with the family map here, then jump to the header that
// owns the MSG_* you're chasing and read its byte-map.
//
// FAMILY MAP (which header owns which message types):
//   header.hpp      the shared 6-byte frame header + MsgType registry +
//                   version scheme + inspect()/writeHeader.
//   presence.hpp    MSG_HELLO (0x01)                     — lamp beacon.
//   control_op.hpp  MSG_CONTROL_OP (0x03)                — forwarded BLE write.
//   wisp.hpp        MSG_WISP_HELLO (0x20), MSG_WISP_CLAIM (0x25),
//                   MSG_WISP_PALETTE (0x26)              — wisp coordination.
//   override.hpp    MSG_OVERRIDE_COLORS (0x21) / RESTORE_COLORS (0x22) /
//                   OVERRIDE_BRIGHTNESS (0x23) / RESTORE_BRIGHTNESS (0x24).
//   event.hpp       MSG_EVENT (0x30)                     — cascade gossip.
//   fw_ota.hpp      MSG_FW_* (0x40..0x45) + MSG_FS_* (0x46..0x4B) — OTA.
//   dedup_ring.hpp  DedupRing (gossip-relay dup suppressor; not a message).
//
// VERSION SCHEME (defined in header.hpp): the version byte at data[2] is a
// RECEIVE RANGE, not a single number. PROTOCOL_VERSION_EMIT is what we put on
// the wire; inspect() accepts [PROTOCOL_VERSION_RX_MIN, PROTOCOL_VERSION_RX_MAX].
// Splitting emit from receive is what lets the fleet *receive* a newer version
// before it *emits* one — the safe path through a multi-version OTA wave.
// Additive fields ride as HELLO/WISP_HELLO TLV trailers (unknown types are
// skipped by length) so most changes need no version bump.
//
// INCLUDE ORDER is a dependency chain, not cosmetic: header first (everyone
// needs writeHeader/inspect/MsgType); presence before fw_ota (fw_ota's
// FW_CHANNEL_LEN == HELLO_FW_CHANNEL_LEN static_assert reads a presence
// constant); control_op + presence + event before MAX_PACKET_SIZE below
// (it aggregates their max sizes). Each header also includes its own deps,
// so correctness doesn't rely on this order alone.

#include <cstdint>
#include <cstring>

#include "header.hpp"
#include "presence.hpp"
#include "control_op.hpp"
#include "wisp.hpp"
#include "override.hpp"
#include "event.hpp"
#include "fw_ota.hpp"
#include "dedup_ring.hpp"

namespace lamp_protocol {

// MAX_PACKET_SIZE: receiver buffer sizing. CONTROL_MAX_SIZE has historically
// been the biggest (250); EVENT_MAX_SIZE is also 250; the override family
// is well under. Use a max() over the candidates so a future shrink of any
// one doesn't silently shrink the buffer.
constexpr size_t MAX_PACKET_SIZE =
    (CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE) >
            EVENT_MAX_SIZE
        ? (CONTROL_MAX_SIZE > HELLO_MAX_SIZE ? CONTROL_MAX_SIZE : HELLO_MAX_SIZE)
        : EVENT_MAX_SIZE;

}  // namespace lamp_protocol

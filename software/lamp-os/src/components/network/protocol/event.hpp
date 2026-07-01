#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"

// =============================================================================
// event.hpp — MSG_EVENT (0x30), the gossip-relayed cascade/expression event.
// build: buildEvent, parse: parseEvent.
// =============================================================================
//
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    1    eventKind (EventKind for built-ins; raw byte for user-defined)
//   13    1    numStaggerEntries (0..kMaxStaggerEntries=12; high bit
//              kStaggerCountReservedHighBit is reserved — parseEvent rejects it)
//   14   8*n   stagger entries: each EVENT_STAGGER_ENTRY = mac(6) + delayMs(2 LE)
//  14+8n  2    payloadLen (LE)
//  16+8n  P    payload (opaque; JSON in practice, ≤ maxEventPayloadFor(n))
//
// Fixed prefix (n=0, P=0) is EVENT_FIXED_SIZE (16); whole frame ≤
// EVENT_MAX_SIZE (250). maxEventPayloadFor(n) gives the dynamic payload budget.

namespace lamp_protocol {

// v0x03 mesh-deploy lock-in: parallel reservation of the high bit on the
// numStaggerEntries field (data[13] of MSG_EVENT). Plausible future use:
// a "scope flag" on the stagger semantics (e.g., room-scope vs. fleet-
// scope cascades, or "broadcast-everyone-fires-tail-jittered" vs the
// current "fires per its delayMs"). parseEvent rejects any frame that
// sets this bit so a future receiver gains an unambiguous escape hatch:
// v0x03 peers loudly drop the frame, not silently reinterpret it.
// why: forward-compat reservation per validated plan §"Layer 3".
constexpr uint8_t kStaggerCountReservedHighBit = 0x80;

constexpr size_t kMaxStaggerEntries         = 12;  // ESP-NOW 250-byte cap math

// MSG_EVENT discriminator. 0x02..0x0F are reserved for built-ins so the
// firmware can add new well-known events without colliding with
// user-defined ones in the 0x10..0xFF range.
enum class EventKind : uint8_t {
  ExpressionTriggered = 0x01,
};

// MSG_EVENT fixed prefix when numStaggerEntries=0 and payloadLen=0:
//   header(6) + sourceMac(6) + eventKind(1) + numStaggerEntries(1)
//   + payloadLen(2)
// = 16 bytes minimum. (Spec'd as 17 in the plan; the math here is the
// authoritative one — header is 6, not 7.)
constexpr size_t EVENT_FIXED_SIZE      = HEADER_SIZE + 6 + 1 + 1 + 2;  // 16
constexpr size_t EVENT_STAGGER_ENTRY   = 6 + 2;                        // mac + delayMs
// Worst-case JSON tail when the wire frame carries a fully-populated
// stagger list (12 peers): 250 - 16 - 12*8 = 138. Kept as a hard upper
// bound the rest of the codebase can rely on for buffer sizing.
// Callers building or validating an event with a known stagger count
// should use maxEventPayloadFor(n) instead — small meshes get a larger
// payload window because fewer bytes are reserved for stagger entries.
constexpr size_t EVENT_MAX_PAYLOAD     = 250 - EVENT_FIXED_SIZE -
                                         (kMaxStaggerEntries * EVENT_STAGGER_ENTRY);
constexpr size_t EVENT_MAX_SIZE        = EVENT_FIXED_SIZE +
                                         kMaxStaggerEntries * EVENT_STAGGER_ENTRY +
                                         EVENT_MAX_PAYLOAD;  // 250

// Dynamic payload budget. The stagger list only occupies n * 8 bytes on
// the wire, so on small meshes (typical home setup: 1–4 lamps) the
// payload window is much larger than the worst-case 138 B. Clamps at the
// 12-peer ceiling so a stale numStagger can't grant a larger-than-frame
// budget. Before this helper, ExpressionManager::maybeCascade dropped any
// invocation > 138 B even on a 1-peer mesh — which silently broke glitchy
// (~151 B real payload) in the field.
constexpr size_t maxEventPayloadFor(uint8_t numStaggerEntries) {
  const size_t n = numStaggerEntries > kMaxStaggerEntries
                       ? kMaxStaggerEntries
                       : static_cast<size_t>(numStaggerEntries);
  return 250 - EVENT_FIXED_SIZE - (n * EVENT_STAGGER_ENTRY);
}

struct ParsedEvent {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  EventKind      eventKind;            // typed for built-ins
  uint8_t        eventKindRaw;         // raw byte for forward-compat / user-defined
  uint8_t        numStaggerEntries;
  struct StaggerEntry {
    uint8_t  mac[6];
    uint16_t delayMs;
  } staggerEntries[kMaxStaggerEntries];
  uint16_t       payloadLen;
  const uint8_t* payload;  // points into the recv buffer; caller must not retain past this call
};

// Build a MSG_EVENT frame. `eventKind` is the raw byte so callers can emit
// user-defined kinds in 0x10..0xFF without re-extending the enum. Stagger
// entries are `numStaggerEntries * (mac(6) + delayMs(2 LE))`; capped at
// kMaxStaggerEntries. Payload is opaque (JSON in practice); capped at
// maxEventPayloadFor(numStaggerEntries) so the full frame stays within
// ESP-NOW's 250-byte limit. Dynamic in the actual stagger count, not the
// worst case, so small meshes can carry larger invocations.
inline size_t buildEvent(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         uint8_t eventKind,
                         const uint8_t* staggerMacs,    // numStaggerEntries * 6
                         const uint16_t* staggerDelays, // numStaggerEntries
                         uint8_t numStaggerEntries,
                         const uint8_t* payload, uint16_t payloadLen) {
  if (!buf || !sourceMac) return 0;
  if (numStaggerEntries > kMaxStaggerEntries) return 0;
  if (numStaggerEntries > 0 && (!staggerMacs || !staggerDelays)) return 0;
  if (payloadLen > maxEventPayloadFor(numStaggerEntries)) return 0;
  const size_t total = EVENT_FIXED_SIZE +
                       static_cast<size_t>(numStaggerEntries) * EVENT_STAGGER_ENTRY +
                       payloadLen;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_EVENT, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  buf[12] = eventKind;
  buf[13] = numStaggerEntries;
  size_t off = 14;
  for (uint8_t i = 0; i < numStaggerEntries; ++i) {
    std::memcpy(&buf[off], &staggerMacs[i * 6], 6);
    off += 6;
    buf[off]     = static_cast<uint8_t>(staggerDelays[i] & 0xFF);
    buf[off + 1] = static_cast<uint8_t>((staggerDelays[i] >> 8) & 0xFF);
    off += 2;
  }
  buf[off]     = static_cast<uint8_t>(payloadLen & 0xFF);
  buf[off + 1] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
  off += 2;
  if (payloadLen && payload) std::memcpy(&buf[off], payload, payloadLen);
  return total;
}

inline bool parseEvent(const uint8_t* data, size_t len, ParsedEvent& out) {
  if (inspect(data, len) != MSG_EVENT) return false;
  if (len < EVENT_FIXED_SIZE) return false;
  // v0x03 lock-in: reject if the reserved high bit on numStaggerEntries is
  // set. EXPLICIT check (not relying on the > kMaxStaggerEntries clamp
  // below to catch it incidentally) so a future refactor that masks low
  // bits first can't silently start accepting the bit. See
  // kStaggerCountReservedHighBit declaration for the rationale.
  // why: forward-compat reservation per validated plan §"Layer 3".
  if (data[13] & kStaggerCountReservedHighBit) return false;
  const uint8_t numStaggerEntries = data[13];
  if (numStaggerEntries > kMaxStaggerEntries) return false;
  const size_t staggerBytes =
      static_cast<size_t>(numStaggerEntries) * EVENT_STAGGER_ENTRY;
  // Need room for the stagger block + the 2-byte payloadLen.
  if (len < EVENT_FIXED_SIZE + staggerBytes) return false;
  const size_t payloadLenOff = HEADER_SIZE + 6 + 1 + 1 + staggerBytes;  // 14 + staggerBytes
  const uint16_t payloadLen =
       static_cast<uint16_t>(data[payloadLenOff])
     | (static_cast<uint16_t>(data[payloadLenOff + 1]) << 8);
  if (payloadLen > maxEventPayloadFor(numStaggerEntries)) return false;
  const size_t expected = EVENT_FIXED_SIZE + staggerBytes + payloadLen;
  if (len != expected) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.eventKindRaw = data[12];
  out.eventKind = static_cast<EventKind>(out.eventKindRaw);
  out.numStaggerEntries = numStaggerEntries;
  size_t off = 14;
  for (uint8_t i = 0; i < numStaggerEntries; ++i) {
    std::memcpy(out.staggerEntries[i].mac, &data[off], 6);
    off += 6;
    out.staggerEntries[i].delayMs =
         static_cast<uint16_t>(data[off])
       | (static_cast<uint16_t>(data[off + 1]) << 8);
    off += 2;
  }
  out.payloadLen = payloadLen;
  out.payload    = payloadLen ? &data[payloadLenOff + 2] : nullptr;
  return true;
}

}  // namespace lamp_protocol

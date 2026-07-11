#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"

// event.hpp — MSG_EVENT (0x30).
// Unauthenticated, nearby-scoped (no relay) expression-fired announce.
// build/parse: buildEvent / parseEvent.
//
// Frame layout:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac (who fired)
//   12    N    payload (ExpressionInvocation JSON)

namespace lamp_protocol {

// header(6) + sourceMac(6) = 12 fixed bytes.
constexpr size_t EVENT_FIXED_SIZE  = HEADER_SIZE + 6;  // 12
constexpr size_t EVENT_MAX_PAYLOAD = 250 - EVENT_FIXED_SIZE;  // 238

static_assert(EVENT_FIXED_SIZE == 12,   "EVENT fixed size lock");
static_assert(EVENT_MAX_PAYLOAD == 238, "EVENT max payload lock");

struct ParsedEvent {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  const uint8_t* payload;
  size_t         payloadLen;
};

// Build a MSG_EVENT frame. `payload` is the ExpressionInvocation JSON.
// Returns total bytes written on success, 0 on error.
inline size_t buildEvent(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         const uint8_t* payload, size_t payloadLen) {
  if (!buf || !sourceMac || !payload) return 0;
  if (payloadLen == 0 || payloadLen > EVENT_MAX_PAYLOAD) return 0;
  const size_t total = EVENT_FIXED_SIZE + payloadLen;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_EVENT, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[EVENT_FIXED_SIZE], payload, payloadLen);
  return total;
}

// Parse a MSG_EVENT frame. `out.payload` points into `data`; valid only
// while `data` is live.
inline bool parseEvent(const uint8_t* data, size_t len, ParsedEvent& out) {
  if (inspect(data, len) != MSG_EVENT) return false;
  if (len <= EVENT_FIXED_SIZE) return false;
  if (len > EVENT_FIXED_SIZE + EVENT_MAX_PAYLOAD) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.payload    = &data[EVENT_FIXED_SIZE];
  out.payloadLen = len - EVENT_FIXED_SIZE;
  return true;
}

}  // namespace lamp_protocol

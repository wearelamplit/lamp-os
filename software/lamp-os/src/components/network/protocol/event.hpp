#pragma once

#include <cstdint>
#include <cstring>

#include "command_auth.hpp"
#include <lampos/protocol/header.hpp>

// MSG_EVENT (0x30).
// Nearby-scoped (no relay) expression-fired announce, authenticated by an
// 8-byte command_auth trailer (a keyed lamp drops a bad/absent tag).
// build/parse: buildEvent / parseEvent.
//
// Frame layout:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac (who fired)
//   12    N    payload (ExpressionInvocation JSON)
//  12+N   8    command_auth tag (HMAC-SHA256 trailer; see command_auth.hpp)

namespace lamp_protocol {

// header(6) + sourceMac(6) = 12 fixed bytes; an 8-byte auth tag trails payload.
constexpr size_t EVENT_FIXED_SIZE  = HEADER_SIZE + 6;  // 12
constexpr size_t EVENT_TAG_SIZE    = command_auth::TAG_SIZE;  // 8
constexpr size_t EVENT_MAX_PAYLOAD = 250 - EVENT_FIXED_SIZE - EVENT_TAG_SIZE;  // 230

static_assert(EVENT_FIXED_SIZE == 12,   "EVENT fixed size lock");
static_assert(EVENT_MAX_PAYLOAD == 230, "EVENT max payload lock");

struct ParsedEvent {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  const uint8_t* payload;
  size_t         payloadLen;
  const uint8_t* tag;  // the 8-byte trailer; verify via command_auth::verify
};

// Build a MSG_EVENT frame body (header+sourceMac+payload). Returns the body
// length WITHOUT the auth tag, 0 on error. The caller appends EVENT_TAG_SIZE
// tag bytes (command_auth::appendTag) before sending; parseEvent requires them.
inline size_t buildEvent(uint8_t* buf, size_t bufLen, uint16_t seq,
                         const uint8_t sourceMac[6],
                         const uint8_t* payload, size_t payloadLen) {
  if (!buf || !sourceMac || !payload) return 0;
  if (payloadLen == 0 || payloadLen > EVENT_MAX_PAYLOAD) return 0;
  const size_t total = EVENT_FIXED_SIZE + payloadLen;
  if (bufLen < total + EVENT_TAG_SIZE) return 0;
  detail::writeHeader(buf, MSG_EVENT, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[EVENT_FIXED_SIZE], payload, payloadLen);
  return total;
}

// Parse a MSG_EVENT frame. `out.payload` / `out.tag` point into `data`; valid
// only while `data` is live. The 8-byte auth tag is stripped from the payload.
inline bool parseEvent(const uint8_t* data, size_t len, ParsedEvent& out) {
  if (inspect(data, len) != MSG_EVENT) return false;
  if (len <= EVENT_FIXED_SIZE + EVENT_TAG_SIZE) return false;
  if (len > EVENT_FIXED_SIZE + EVENT_MAX_PAYLOAD + EVENT_TAG_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  out.payload    = &data[EVENT_FIXED_SIZE];
  out.payloadLen = len - EVENT_FIXED_SIZE - EVENT_TAG_SIZE;
  out.tag        = &data[len - EVENT_TAG_SIZE];
  return true;
}

}  // namespace lamp_protocol

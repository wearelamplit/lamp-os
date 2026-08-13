#pragma once

#include <cstdint>
#include <cstring>

#include "command_auth.hpp"
#include <lampos/protocol/header.hpp>
#include <lampos/protocol/lamp_protocol.hpp>

// MSG_COMMAND (0x31).
// Targeted expression invocation, authenticated by an 8-byte command_auth
// trailer (a keyed lamp drops a bad/absent tag).
// build/parse: buildCommand / parseCommand.
//
// Frame layout:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    6    targetMac
//   18    N    payload (ExpressionInvocation JSON)
//  18+N   8    command_auth tag (HMAC-SHA256 trailer; see command_auth.hpp)
//
// No gossip relay. addressedToUs filter on recv (single-hop, physically nearby).

namespace lamp_protocol {

// header(6) + sourceMac(6) + targetMac(6) = 18 fixed; 8-byte auth tag trails.
constexpr size_t COMMAND_FIXED_SIZE  = HEADER_SIZE + 6 + 6;  // 18
constexpr size_t COMMAND_TAG_SIZE    = command_auth::TAG_SIZE;  // 8
// MSG_COMMAND broadcasts at the v2 frame ceiling, so a >250 B cascade reaches
// only v2-capable peers. A v1/classic peer drops the oversized frame per the
// ESP-NOW contract and silently misses that cascade (graceful, no crash). A
// mixed-fleet capability gate is owed before public beta.
constexpr size_t COMMAND_MAX_PAYLOAD =
    ESPNOW_V2_FRAME_MAX - COMMAND_FIXED_SIZE - COMMAND_TAG_SIZE;  // 1444

static_assert(COMMAND_FIXED_SIZE == 18, "COMMAND fixed size lock");
static_assert(COMMAND_MAX_PAYLOAD == 1444, "COMMAND max payload lock");

struct ParsedCommand {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  const uint8_t* payload;
  size_t         payloadLen;
  const uint8_t* tag;  // the 8-byte trailer; verify via command_auth::verify
};

// Build a MSG_COMMAND frame body (header+sourceMac+targetMac+payload). Returns
// the body length WITHOUT the auth tag, 0 on error. The caller appends
// COMMAND_TAG_SIZE tag bytes (command_auth::appendTag) before sending;
// parseCommand requires them.
inline size_t buildCommand(uint8_t* buf, size_t bufLen, uint16_t seq,
                           const uint8_t sourceMac[6],
                           const uint8_t targetMac[6],
                           const uint8_t* payload, size_t payloadLen) {
  if (!buf || !sourceMac || !targetMac || !payload) return 0;
  if (payloadLen == 0 || payloadLen > COMMAND_MAX_PAYLOAD) return 0;
  const size_t total = COMMAND_FIXED_SIZE + payloadLen;
  if (bufLen < total + COMMAND_TAG_SIZE) return 0;
  detail::writeHeader(buf, MSG_COMMAND, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  std::memcpy(&buf[COMMAND_FIXED_SIZE], payload, payloadLen);
  return total;
}

// Parse a MSG_COMMAND frame. `out.payload` / `out.tag` point into `data`; valid
// only while `data` is live. The 8-byte auth tag is stripped from the payload.
inline bool parseCommand(const uint8_t* data, size_t len, ParsedCommand& out) {
  if (inspect(data, len) != MSG_COMMAND) return false;
  if (len <= COMMAND_FIXED_SIZE + COMMAND_TAG_SIZE) return false;
  if (len > COMMAND_FIXED_SIZE + COMMAND_MAX_PAYLOAD + COMMAND_TAG_SIZE) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.payload    = &data[COMMAND_FIXED_SIZE];
  out.payloadLen = len - COMMAND_FIXED_SIZE - COMMAND_TAG_SIZE;
  out.tag        = &data[len - COMMAND_TAG_SIZE];
  return true;
}

}  // namespace lamp_protocol

#pragma once

#include <cstdint>
#include <cstring>

#include "header.hpp"

// =============================================================================
// command.hpp — MSG_COMMAND (0x31).
// build/parse: buildCommand / parseCommand.
// =============================================================================
//
// Frame layout:
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    sourceMac
//   12    6    targetMac
//   18    N    payload (ExpressionInvocation JSON)
//
// No gossip relay. addressedToUs filter on recv (single-hop, physically nearby).

namespace lamp_protocol {

// header(6) + sourceMac(6) + targetMac(6) = 18 fixed bytes.
constexpr size_t COMMAND_FIXED_SIZE  = HEADER_SIZE + 6 + 6;  // 18
constexpr size_t COMMAND_MAX_PAYLOAD = 250 - COMMAND_FIXED_SIZE;  // 232

static_assert(COMMAND_FIXED_SIZE == 18, "COMMAND fixed size lock");
static_assert(COMMAND_MAX_PAYLOAD == 232, "COMMAND max payload lock");

struct ParsedCommand {
  uint16_t       seq;
  uint8_t        sourceMac[6];
  uint8_t        targetMac[6];
  const uint8_t* payload;
  size_t         payloadLen;
};

// Build a MSG_COMMAND frame. `payload` is the ExpressionInvocation JSON.
// Returns total bytes written on success, 0 on error.
inline size_t buildCommand(uint8_t* buf, size_t bufLen, uint16_t seq,
                           const uint8_t sourceMac[6],
                           const uint8_t targetMac[6],
                           const uint8_t* payload, size_t payloadLen) {
  if (!buf || !sourceMac || !targetMac || !payload) return 0;
  if (payloadLen == 0 || payloadLen > COMMAND_MAX_PAYLOAD) return 0;
  const size_t total = COMMAND_FIXED_SIZE + payloadLen;
  if (bufLen < total) return 0;
  detail::writeHeader(buf, MSG_COMMAND, seq);
  std::memcpy(&buf[6], sourceMac, 6);
  std::memcpy(&buf[12], targetMac, 6);
  std::memcpy(&buf[COMMAND_FIXED_SIZE], payload, payloadLen);
  return total;
}

// Parse a MSG_COMMAND frame. `out.payload` points into `data`; valid only
// while `data` is live.
inline bool parseCommand(const uint8_t* data, size_t len, ParsedCommand& out) {
  if (inspect(data, len) != MSG_COMMAND) return false;
  if (len <= COMMAND_FIXED_SIZE) return false;
  if (len > COMMAND_FIXED_SIZE + COMMAND_MAX_PAYLOAD) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.sourceMac, &data[6], 6);
  std::memcpy(out.targetMac, &data[12], 6);
  out.payload    = &data[COMMAND_FIXED_SIZE];
  out.payloadLen = len - COMMAND_FIXED_SIZE;
  return true;
}

}  // namespace lamp_protocol

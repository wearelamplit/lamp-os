#pragma once

#include <cstdint>
#include <cstring>

#include <lampos/protocol/header.hpp>

// MSG_CONTROL_OP (0x03), a forwarded BLE control write.
// build: buildControlOp, parse: parseControlOp.
//
//   off  size  field
//    0    6    header (see header.hpp)
//    6    6    targetMac
//   12    6    sourceMac
//   18    2    payloadLen (LE)
//   20    N    payload (opaque; JSON in practice, ≤ CONTROL_MAX_PAYLOAD = 576)
//
// Fixed prefix is CONTROL_FIXED (20); whole frame ≤ CONTROL_MAX_SIZE (596).

namespace lamp_protocol {

// MSG_CONTROL_OP frame: header(6) + targetMac(6) + sourceMac(6) + payloadLen(2) + payload(N).
constexpr size_t CONTROL_FIXED       = HEADER_SIZE + 6 + 6 + 2;
// Sized to the wispStatus JSON worst case (568B: every field, 16 observed
// zones; see test_status_json's test_true_worst_case_untruncated_length),
// rounded up to the next 32.
constexpr size_t CONTROL_MAX_PAYLOAD = 576;
constexpr size_t CONTROL_MAX_SIZE    = CONTROL_FIXED + CONTROL_MAX_PAYLOAD;

struct ParsedControlOp {
  uint16_t seq;
  uint8_t targetMac[6];
  uint8_t sourceMac[6];
  uint16_t payloadLen;
  const uint8_t* payload;  // points into the recv buffer; caller must not retain past this call
};

// Build a CONTROL_OP frame. Payload is opaque (JSON in practice). Returns
// total bytes written on success, 0 on bad args / oversize.
inline size_t buildControlOp(uint8_t* buf, size_t bufLen, uint16_t seq,
                             const uint8_t targetMac[6],
                             const uint8_t sourceMac[6],
                             const uint8_t* payload, size_t payloadLen) {
  if (!buf || !targetMac || !sourceMac) return 0;
  if (payloadLen > CONTROL_MAX_PAYLOAD) return 0;
  const size_t total = CONTROL_FIXED + payloadLen;
  if (bufLen < total) return 0;
  buf[0] = MAGIC_0;
  buf[1] = MAGIC_1;
  buf[2] = PROTOCOL_VERSION;
  buf[3] = MSG_CONTROL_OP;
  buf[4] = static_cast<uint8_t>(seq & 0xFF);
  buf[5] = static_cast<uint8_t>((seq >> 8) & 0xFF);
  std::memcpy(&buf[6], targetMac, 6);
  std::memcpy(&buf[12], sourceMac, 6);
  buf[18] = static_cast<uint8_t>(payloadLen & 0xFF);
  buf[19] = static_cast<uint8_t>((payloadLen >> 8) & 0xFF);
  if (payloadLen && payload) std::memcpy(&buf[CONTROL_FIXED], payload, payloadLen);
  return total;
}

inline bool parseControlOp(const uint8_t* data, size_t len, ParsedControlOp& out) {
  if (inspect(data, len) != MSG_CONTROL_OP || len < CONTROL_FIXED) return false;
  out.seq = static_cast<uint16_t>(data[4]) | (static_cast<uint16_t>(data[5]) << 8);
  std::memcpy(out.targetMac, &data[6], 6);
  std::memcpy(out.sourceMac, &data[12], 6);
  out.payloadLen = static_cast<uint16_t>(data[18]) | (static_cast<uint16_t>(data[19]) << 8);
  if (out.payloadLen > CONTROL_MAX_PAYLOAD) return false;
  if (len < CONTROL_FIXED + out.payloadLen) return false;
  out.payload = &data[CONTROL_FIXED];
  return true;
}

}  // namespace lamp_protocol

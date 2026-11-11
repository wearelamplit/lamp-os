#include "net/control_op_inbox.hpp"

namespace wisp {

void ControlOpInbox::offer(const lamp_protocol::ParsedControlOp& op,
                           uint32_t nowMs) {
  if (!controlOpForWisp(op.targetMac, myMac_)) return;
  if (!dedup_.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP, op.seq, nowMs)) {
    return;
  }
  if (op.payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return;
  LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
  std::memcpy(buf_, op.payload, op.payloadLen);
  len_ = op.payloadLen;
  valid_ = true;
  LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
}

bool ControlOpInbox::take(uint8_t* out, uint16_t& len) {
  bool have = false;
  LAMP_PROTOCOL_PORTMUX_ENTER(&mux_);
  if (valid_) {
    std::memcpy(out, buf_, len_);
    len = len_;
    valid_ = false;
    len_ = 0;
    have = true;
  }
  LAMP_PROTOCOL_PORTMUX_EXIT(&mux_);
  return have;
}

}  // namespace wisp

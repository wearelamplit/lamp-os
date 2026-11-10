#pragma once

#include <cstdint>
#include <cstring>

#include "wire/lamp_protocol.hpp"

namespace wisp {

// A CONTROL_OP applies here only when addressed to this wisp. Current lamps
// unicast every app op to their display wisp. A broadcast op, such as a peer
// wisp's status or config or this wisp's own relayed back, would otherwise
// displace a pending app op whose unicast resends the dedup then swallows.
inline bool controlOpForWisp(const uint8_t targetMac[6],
                             const uint8_t myMac[6]) {
  return std::memcmp(targetMac, myMac, 6) == 0;
}

// The one CONTROL_OP this wisp acts on next. offer() runs on the WiFi recv
// task, take() on the loop task. Admits an op addressed to this wisp once per
// (sourceMac, seq); a newly admitted op replaces one not yet taken.
class ControlOpInbox {
 public:
  // Set once the radio is up; until then no real target is admitted.
  void setSelfMac(const uint8_t mac[6]) { std::memcpy(myMac_, mac, 6); }

  void offer(const lamp_protocol::ParsedControlOp& op);

  // Moves the pending payload into `out` (CONTROL_MAX_PAYLOAD bytes) and
  // sets `len`. False when nothing is pending.
  bool take(uint8_t* out, uint16_t& len);

 private:
  uint8_t myMac_[6] = {0};

  // Gossip relay delivers CONTROL_OP multiple times by design; 64-slot ring
  // keyed on (sourceMac, msgType, seq) drops re-arrivals before the slot.
  lamp_protocol::DedupRing<64> dedup_;

  LAMP_PROTOCOL_PORTMUX_TYPE mux_ = LAMP_PROTOCOL_PORTMUX_INIT;
  uint8_t buf_[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
  uint16_t len_ = 0;
  bool valid_ = false;
};

}  // namespace wisp

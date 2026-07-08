// MeshRouter — ESP-NOW recv routing + cross-task op inbox.
//
// onPacket() fires on the WiFi recv task: parse HELLO/CLAIM/CONTROL_OP, feed
// inventory + roster, and hand a CONTROL_OP payload to the single-slot portMUX
// inbox. drainPendingOps() runs on the loop task, dispatches the queued op via
// WispOpDispatcher, and reports the DispatchResult to the result callback.
// Business logic stays in the callback; the router only parses and hands off.

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

#include "wire/lamp_protocol.hpp"
#include "config/wisp_op_dispatcher.hpp"

namespace wisp {

class LampInventory;
class WispRoster;

// RSSI is trustworthy only from a direct reception. Lamps gossip-relay HELLOs
// verbatim, so a relayed frame pairs the relayer's radio RSSI with the
// originator's payload MAC; record INT8_MIN (the never-measured sentinel
// recordHello preserves) for those so a relay can't corrupt claim decisions.
inline int8_t helloRssiForRecord(const uint8_t* radioSrcMac,
                                 const uint8_t sourceMac[6], int8_t rssi) {
  const bool direct =
      radioSrcMac && std::memcmp(radioSrcMac, sourceMac, 6) == 0;
  return direct ? rssi : INT8_MIN;
}

class MeshRouter {
 public:
  using OpResultFn = std::function<void(DispatchResult)>;

  MeshRouter(LampInventory& inventory, WispRoster& roster,
             WispOpDispatcher& dispatcher, OpResultFn onOpResult)
      : inventory_(inventory),
        roster_(roster),
        dispatcher_(dispatcher),
        onOpResult_(std::move(onOpResult)) {}

  // MeshLink recv handler. WiFi task: protocol parse + bounded memcpy only.
  // No logging, no Preferences, no ArduinoJson.
  void onPacket(const uint8_t srcMac[6], const uint8_t* data, size_t len,
                int8_t rssi);

  // Loop task. Drains the pending op (if any) and dispatches it.
  void drainPendingOps();

 private:
  // Recv-task safe: bounded memcpy + flag under portMUX. If a previous op is
  // still pending, the new one wins. Single-slot, latest intent matters most.
  void postPendingWispOp(const uint8_t* payload, uint16_t payloadLen);

  LampInventory& inventory_;
  WispRoster& roster_;
  WispOpDispatcher& dispatcher_;
  OpResultFn onOpResult_;

  // Gossip relay delivers CONTROL_OP multiple times by design; 64-slot ring
  // keyed on (sourceMac, msgType, seq) drops re-arrivals before the dispatcher.
  lamp_protocol::DedupRing controlOpDedup_;

  // MSG_CONTROL_OP recv handler fires on the WiFi task (Core 0). ArduinoJson
  // and Preferences are not safe there; fixed-size memcpy under portMUX,
  // drain in loop().
  LAMP_PROTOCOL_PORTMUX_TYPE pendingMux_ = LAMP_PROTOCOL_PORTMUX_INIT;
  uint8_t pendingWispOpBuf_[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
  uint16_t pendingWispOpLen_ = 0;
  bool pendingWispOpValid_ = false;
};

}  // namespace wisp

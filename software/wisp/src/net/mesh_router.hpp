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

// Relayed HELLO: the radio src differs from payload MAC; RSSI belongs to the
// relayer, not the originator.
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
  lamp_protocol::DedupRing<64> controlOpDedup_;

  // Pending op from WiFi task; drained on loop task.
  LAMP_PROTOCOL_PORTMUX_TYPE pendingMux_ = LAMP_PROTOCOL_PORTMUX_INIT;
  uint8_t pendingWispOpBuf_[lamp_protocol::CONTROL_MAX_PAYLOAD] = {0};
  uint16_t pendingWispOpLen_ = 0;
  bool pendingWispOpValid_ = false;
};

}  // namespace wisp

#pragma once

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>

#include "wire/lamp_protocol.hpp"
#include "config/wisp_op_dispatcher.hpp"
#include "net/control_op_inbox.hpp"

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

  // Cache this wisp's own STA MAC so onPacket can filter CONTROL_OP by
  // target. Set once at setup, after MeshLink::begin() brings the radio up.
  void setSelfMac(const uint8_t mac[6]) { inbox_.setSelfMac(mac); }

  // MeshLink recv handler. WiFi task: protocol parse + bounded memcpy only.
  // No logging, no Preferences, no ArduinoJson.
  void onPacket(const uint8_t srcMac[6], const uint8_t* data, size_t len,
                int8_t rssi);

  // Loop task. Drains the pending op (if any) and dispatches it.
  void drainPendingOps();

 private:
  LampInventory& inventory_;
  WispRoster& roster_;
  WispOpDispatcher& dispatcher_;
  OpResultFn onOpResult_;
  ControlOpInbox inbox_;
};

}  // namespace wisp

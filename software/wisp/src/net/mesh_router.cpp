#include "net/mesh_router.hpp"

#include <Arduino.h>

#include <cstring>

#include "fleet/lamp_inventory.hpp"
#include "fleet/wisp_roster.hpp"

namespace wisp {

void MeshRouter::postPendingWispOp(const uint8_t* payload, uint16_t payloadLen) {
  if (payloadLen > lamp_protocol::CONTROL_MAX_PAYLOAD) return;
  LAMP_PROTOCOL_PORTMUX_ENTER(&pendingMux_);
  std::memcpy(pendingWispOpBuf_, payload, payloadLen);
  pendingWispOpLen_ = payloadLen;
  pendingWispOpValid_ = true;
  LAMP_PROTOCOL_PORTMUX_EXIT(&pendingMux_);
}

void MeshRouter::onPacket(const uint8_t* srcMac, const uint8_t* data,
                          size_t len, int8_t rssi) {
  const uint8_t msgType = lamp_protocol::inspect(data, len);
  if (msgType == lamp_protocol::MSG_HELLO) {
    lamp_protocol::ParsedHello h;
    if (!lamp_protocol::parseHello(data, len, h)) return;
    inventory_.recordHello(h.sourceMac, h.name, h.base, h.shade,
                           h.firmwareVersion, millis(),
                           helloRssiForRecord(srcMac, h.sourceMac, rssi));
    return;
  }
  if (msgType == lamp_protocol::MSG_WISP_CLAIM) {
    lamp_protocol::ParsedWispClaim wc;
    if (!lamp_protocol::parseWispClaim(data, len, wc)) return;
    roster_.recordPeerClaim(wc.sourceMac, wc.entries, wc.count, millis());
    return;
  }
  if (msgType == lamp_protocol::MSG_CONTROL_OP) {
    lamp_protocol::ParsedControlOp op;
    if (!lamp_protocol::parseControlOp(data, len, op)) return;
    if (!controlOpDedup_.record(op.sourceMac, lamp_protocol::MSG_CONTROL_OP,
                                op.seq)) {
      return;
    }
    postPendingWispOp(op.payload, op.payloadLen);
    return;
  }
}

void MeshRouter::drainPendingOps() {
  uint8_t localBuf[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t localLen = 0;
  bool have = false;
  LAMP_PROTOCOL_PORTMUX_ENTER(&pendingMux_);
  if (pendingWispOpValid_) {
    std::memcpy(localBuf, pendingWispOpBuf_, pendingWispOpLen_);
    localLen = pendingWispOpLen_;
    pendingWispOpValid_ = false;
    pendingWispOpLen_ = 0;
    have = true;
  }
  LAMP_PROTOCOL_PORTMUX_EXIT(&pendingMux_);
  if (!have) return;

  const DispatchResult res = dispatcher_.dispatch(localBuf, localLen);
  if (onOpResult_) onOpResult_(res);
}

}  // namespace wisp

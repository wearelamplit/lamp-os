#include "net/mesh_router.hpp"

#include <Arduino.h>

#include <cstring>

#include "fleet/lamp_inventory.hpp"
#include "fleet/wisp_roster.hpp"

namespace wisp {

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
    inbox_.offer(op, millis());
    return;
  }
}

void MeshRouter::drainPendingOps() {
  uint8_t buf[lamp_protocol::CONTROL_MAX_PAYLOAD];
  uint16_t len = 0;
  if (!inbox_.take(buf, len)) return;
  const DispatchResult res = dispatcher_.dispatch(buf, len);
  if (onOpResult_) onOpResult_(res);
}

}  // namespace wisp

#include "core/behavior_context.hpp"

#include "components/network/mesh/lamp_roster.hpp"
#include "util/bd_addr.hpp"

namespace lamp {

PeerView PeerView::make(const char* name, const uint8_t* mac, bool hasMac,
                        Color baseColor, Color shadeColor, int8_t rssi,
                        lamp_protocol::LampVariant variant) {
  PeerView v;
  v.name = name;
  v.mac = mac;
  v.hasMac = hasMac;
  v.baseColor = baseColor;
  v.shadeColor = shadeColor;
  v.rssi = rssi;
  v.variant = variant;
  if (hasMac) formatBdAddr(mac, v.lampId);
  return v;
}

PeerView PeerView::from(const RosterEntry& e) {
  return make(e.name, e.mac, e.hasMac, e.baseColor, e.shadeColor, e.lastRssi,
              e.variant);
}

}  // namespace lamp

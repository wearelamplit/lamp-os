#include "core/behavior_context.hpp"

#include "components/network/mesh/lamp_roster.hpp"
#include "util/bd_addr.hpp"

namespace lamp {

PeerView PeerView::from(const RosterEntry& e) {
  PeerView v;
  v.name = e.name;
  v.mac = e.mac;
  v.hasMac = e.hasMac;
  v.baseColor = e.baseColor;
  v.shadeColor = e.shadeColor;
  v.rssi = e.lastRssi;
  if (e.hasMac) formatBdAddr(e.mac, v.lampId);
  return v;
}

}  // namespace lamp

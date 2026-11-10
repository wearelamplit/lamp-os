#include "state_notify.hpp"

#include <cstdio>

#include "util/json_escape.hpp"

namespace ble_control {

void buildStateNotifyJson(const StateNotifyFields& f, std::string& out) {
  out = "{\"previewActive\":";
  out += f.previewActive ? "true" : "false";
  if (f.greeting) {
    if (f.greeting->active) {
      out += ",\"greeting\":{\"active\":true,\"peer\":\"";
      lamp::appendJsonEscaped(out, f.greeting->peerLampId.c_str());
      out += "\",\"kind\":\"";
      lamp::appendJsonEscaped(out, f.greeting->kind.c_str());
      out += "\"}";
    } else {
      out += ",\"greeting\":{\"active\":false}";
    }
  }
  char buf[48];
  std::snprintf(buf, sizeof(buf), ",\"nearbyRev\":%lu,\"quiet\":%s}",
                static_cast<unsigned long>(f.nearbyRev),
                f.quiet ? "true" : "false");
  out += buf;
}

bool StateNotifyTrigger::step(bool clientConnected, bool subscribed,
                              bool quiet) {
  const bool due = clientConnected &&
                   (!wasConnected_ || subscribed || quiet != wasQuiet_);
  wasConnected_ = clientConnected;
  wasQuiet_     = quiet;
  return due;
}

}  // namespace ble_control

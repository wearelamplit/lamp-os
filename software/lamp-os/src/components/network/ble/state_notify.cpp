#include "state_notify.hpp"

#include <cstdio>

#include "components/firmware/ota_quiet_mode.hpp"
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
                lamp::ota_quiet_mode::isQuiet() ? "true" : "false");
  out += buf;
}

bool StateNotifyTrigger::step(bool clientConnected, bool subscribed, uint32_t nowMs) {
  const bool quiet = lamp::ota_quiet_mode::isQuiet();
  const bool retry = owed_ && static_cast<int32_t>(nowMs - retryAtMs_) >= 0;
  const bool due = clientConnected &&
                   (!wasConnected_ || subscribed || quiet != wasQuiet_ || retry);
  if (due || !clientConnected) owed_ = false;
  wasConnected_ = clientConnected;
  wasQuiet_     = quiet;
  return due;
}

void StateNotifyTrigger::failed(uint32_t nowMs) {
  owed_      = true;
  retryAtMs_ = nowMs + kRetryMs;
}

}  // namespace ble_control

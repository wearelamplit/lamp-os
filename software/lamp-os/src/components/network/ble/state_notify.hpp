#pragma once

#include <cstdint>
#include <string>

#include "behaviors/greetable.hpp"

namespace ble_control {

// One CHAR_STATE_NOTIFY payload. Wire format: docs/dev/networking.md.
struct StateNotifyFields {
  bool previewActive = false;
  // Null when no greeting source is wired; the payload then omits `greeting`.
  const lamp::GreetingState* greeting = nullptr;
  uint32_t nearbyRev = 0;
};

// Adds `quiet` from ota_quiet_mode::isQuiet(), the predicate every
// live-control write gate tests.
void buildStateNotifyJson(const StateNotifyFields& f, std::string& out);

// When tick() sends CHAR_STATE_NOTIFY on its own: on a new client connection,
// when the client subscribes, and on every quiet-mode change while a client is
// connected, so the app learns when its writes land again.
class StateNotifyTrigger {
 public:
  bool step(bool clientConnected, bool subscribed);

 private:
  bool wasConnected_ = false;
  bool wasQuiet_     = false;
};

}  // namespace ble_control

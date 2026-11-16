#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

#include "behaviors/greetable.hpp"
#include "util/heap_guard.hpp"

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

// A greeting peer id and kind at their longest, with the fixed fields.
inline constexpr size_t kStateNotifyBuildBytes = 256;

// Builds the payload from the fields `fill(fields, greeting)` sets, `fill`
// inside the guard since the greeting it reads allocates. False, with `out`
// empty, when the heap cannot spare the build or an allocation fails.
template <class Fill>
bool buildStateNotifyGuarded(Fill&& fill, std::string& out) {
  out.clear();
  if (!lamp::heapCanSpare(kStateNotifyBuildBytes)) return false;
  try {
    StateNotifyFields fields;
    lamp::GreetingState greeting;
    fill(fields, greeting);
    buildStateNotifyJson(fields, out);
    return true;
  } catch (const std::bad_alloc&) {
    std::string().swap(out);
    return false;
  }
}

// When tick() sends CHAR_STATE_NOTIFY on its own: on a new client connection,
// when the client subscribes, and on every quiet-mode change while a client is
// connected, so the app learns when its writes land again. A due notify that
// did not go out is reported with failed() and is due again kRetryMs later
// while a client is connected, until one goes out.
class StateNotifyTrigger {
 public:
  // Spaces the retries so a send that keeps failing does not build a payload
  // every loop pass.
  static constexpr uint32_t kRetryMs = 100;

  bool step(bool clientConnected, bool subscribed, uint32_t nowMs);
  void failed(uint32_t nowMs);

 private:
  bool     wasConnected_ = false;
  bool     wasQuiet_     = false;
  bool     owed_         = false;
  uint32_t retryAtMs_    = 0;
};

}  // namespace ble_control

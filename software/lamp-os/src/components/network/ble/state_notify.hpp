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

enum class StateNotifySend : uint8_t { Sent, NotSubscribed, NoHeap, SendFailed };

// Builds the payload and hands it to `send(data, len, connHandle)`, NimBLE's
// notify to that one connection. That send skips NimBLE's subscription check,
// so `subscribed` (the client turned notifications on) gates it. Sent only
// when `send` reports the bytes went out.
template <class Fill, class Send>
StateNotifySend sendStateNotify(bool subscribed, uint16_t connHandle, Fill&& fill, Send&& send) {
  if (!subscribed) return StateNotifySend::NotSubscribed;
  std::string payload;
  if (!buildStateNotifyGuarded(fill, payload)) return StateNotifySend::NoHeap;
  return send(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(), connHandle)
             ? StateNotifySend::Sent
             : StateNotifySend::SendFailed;
}

// When tick() sends CHAR_STATE_NOTIFY on its own: when a client becomes
// reachable, when it subscribes, and on every quiet-mode change while it is
// reachable, so the app learns when its writes land again. clientConnected is
// "a client is connected with notifications on". A due notify that did not go
// out is reported with failed() and is due again kRetryMs later while the
// client stays reachable, until one goes out.
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

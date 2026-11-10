#pragma once

#include <atomic>
#include <cstdint>

namespace wisp {

// Decides when wispStatus goes out. A request sends one status, coalesced to
// one per kMinIntervalMs: a request inside the window is deferred until it
// opens, never dropped. A change also opens a kBurstMs burst, timed from the
// next takeEmit call, that re-sends the status every kBurstIntervalMs so an
// op's confirmation outlasts a multi-second coex reception gap on the lamps.
// A request inside a burst neither extends nor shortens it.
// request() and change() are safe from any task; takeEmit() runs on one task.
class StatusCadence {
 public:
  static constexpr uint32_t kMinIntervalMs = 5000;
  static constexpr uint32_t kBurstMs = 20000;
  static constexpr uint32_t kBurstIntervalMs = 2000;

  void request();
  void change();
  // True when a status goes out now, and records it as sent. statusClear is
  // false while a status would land too close behind the last wispConfig.
  bool takeEmit(uint32_t nowMs, bool statusClear);

 private:
  std::atomic<bool> due_{false};
  std::atomic<bool> burstRequested_{false};
  bool bursting_ = false;
  uint32_t burstUntilMs_ = 0;
  bool haveEmitted_ = false;
  uint32_t lastEmitMs_ = 0;
};

}  // namespace wisp

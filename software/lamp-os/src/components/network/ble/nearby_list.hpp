#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "components/network/mesh/lamp_roster.hpp"

namespace ble_control {

// The app-facing `nearby` list (docs/dev/networking.md): the roster rendered as
// JSON, held only while the app reads it. A read keeps it built for kHoldMs;
// with no reader it holds no list and no roster copy.
class NearbyList {
 public:
  static constexpr uint32_t kHoldMs = 30000;
  // The longest a read waits for the loop to build the list.
  static constexpr uint32_t kReadWaitMs = 100;
  // The cut keeps the most-relevant peers, so the contiguous build stays
  // bounded regardless of fleet size; the app surfaces "there may be more".
  static constexpr size_t kMaxLamps = 25;

  NearbyList();

  // Any task but the loop's. Records a read at `nowMs`, waits up to
  // kReadWaitMs for the list, then copies it out. False, with `out` empty,
  // when it is not built. May throw std::bad_alloc.
  bool read(std::string& out, uint32_t nowMs);
  // Any task. Copies the list out without keeping it alive; same results.
  bool copy(std::string& out);
  // Any task. Ends the hold, so the next tick frees the list unless a read
  // comes first.
  void endHold() { readAtMs_.store(0); }

  // Loop task only. Builds the list while a client is connected and a read
  // came within kHoldMs, rebuilds it on a roster change (at most once a
  // second), and frees it otherwise. True when rev() moved.
  bool tick(lamp::LampRoster& roster, bool clientConnected, uint32_t nowMs);
  // Loop task only. Frees the list and the roster copy.
  void release(lamp::LampRoster& roster);

  // Moves when the served list's app-visible fields change, and each time
  // the list is built afresh.
  uint32_t rev() const { return rev_.load(); }
  bool built() const { return built_.load(); }

  // The most bytes one peer's entry can render to, so a build sized from it
  // never reallocates.
  static size_t entryBound(const lamp::ListedPeer& p);
  // Renders `lamps` into `out`, which the caller reserved from entryBound.
  static void render(const std::vector<lamp::ListedPeer>& lamps, uint32_t nowMs,
                     std::string& out);

 private:
  bool wanted(bool clientConnected, uint32_t nowMs);
  bool rebuild(lamp::LampRoster& roster, uint32_t gen, uint32_t nowMs);

  SemaphoreHandle_t     mutex_;
  std::string           json_;  // written under mutex_, loop task only
  std::atomic<bool>     built_{false};
  std::atomic<uint32_t> readAtMs_{0};
  std::atomic<uint32_t> rev_{0};
  uint32_t builtGen_  = 0;
  uint32_t lastTryMs_ = 0;
  uint32_t lastHash_  = 0;
  bool     tried_     = false;
  bool     holding_   = false;
};

}  // namespace ble_control

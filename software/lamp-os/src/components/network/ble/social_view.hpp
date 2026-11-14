#pragma once

#include <atomic>
#include <cstdint>

#include "components/network/ble/nearby_list.hpp"

namespace ble_control {

// The app on its Social page, from one link's CHAR_EDIT_SESSION 0x08 writes.
// An open waits out OTA quiet like the edit surfaces. A close ends the nearby
// list's hold only after an open on this link, seen in quiet or not, so the
// app replaying a close it already sent keeps a mesh-lamps screen's hold.
// Call write and clear from one task.
class SocialView {
 public:
  void write(uint8_t surface, bool open, bool quiet, NearbyList& nearby) {
    if (!(surface & 0x08)) return;
    if (!open) {
      if (opened_) nearby.endHold();
      opened_ = false;
      active_.store(false, std::memory_order_relaxed);
      return;
    }
    opened_ = true;
    if (!quiet) active_.store(true, std::memory_order_relaxed);
  }

  // The link closed.
  void clear() {
    opened_ = false;
    active_.store(false, std::memory_order_relaxed);
  }

  // Whether the Social scan runs. Any task may ask.
  bool active() const { return active_.load(std::memory_order_relaxed); }

 private:
  std::atomic<bool> active_{false};
  bool              opened_ = false;
};

}  // namespace ble_control

#pragma once

#include <cstdint>

namespace lamp {

enum class WispStateEdge : uint8_t { kNone, kAdopt, kRelease };

// Windowed control-plane instrument for MSG_WISP_STATE, the per-lamp paint
// frame that drives adopt / release. Feeds the [wispstate] LAMP_DEBUG line:
// recv/adopt/release/selfPresent plus adopt/release edges of this lamp's own
// paint entry. No loss/seq-gap math: the wisp shares one seq across all its
// message types, so a per-type seq gap is not loss. The caller scopes it to
// the display wisp. Not thread-safe; the caller (handleRecv) runs
// single-threaded on the ESP-NOW recv task.
class WispStateMeter {
 public:
  // selfPresent = this lamp's MAC appears in the frame's entry set. Returns
  // the adopt/release edge (kNone on a held state) so the caller can log it
  // with the source MAC + seq.
  WispStateEdge record(bool selfPresent) {
    recv_++;
    if (selfPresent) selfPresent_++;
    if (selfPresent && !adopted_) {
      adopted_ = true;
      adopts_++;
      return WispStateEdge::kAdopt;
    }
    if (!selfPresent && adopted_) {
      adopted_ = false;
      releases_++;
      return WispStateEdge::kRelease;
    }
    return WispStateEdge::kNone;
  }

  uint32_t recv() const { return recv_; }
  uint32_t adopts() const { return adopts_; }
  uint32_t releases() const { return releases_; }
  uint32_t selfPresent() const { return selfPresent_; }

  // Zero the per-window counters. Adopt state persists so a paint held across
  // a window boundary is not re-counted as a new adopt.
  void resetWindow() {
    recv_ = 0;
    adopts_ = 0;
    releases_ = 0;
    selfPresent_ = 0;
  }

 private:
  bool adopted_ = false;
  uint32_t recv_ = 0;
  uint32_t adopts_ = 0;
  uint32_t releases_ = 0;
  uint32_t selfPresent_ = 0;
};

}  // namespace lamp

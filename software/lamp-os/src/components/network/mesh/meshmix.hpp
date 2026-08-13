#pragma once

#include <cstdint>

#include "components/network/protocol/lamp_protocol.hpp"

namespace lamp {

// Diagnostic-only ESP-NOW traffic-mix counter: buckets received frames by
// broad category and counts gossip re-broadcasts, to measure the wisp + relay
// share of airtime against lamp-to-lamp traffic. Fixed counters, no heap, no
// per-peer state. Not thread-safe; the caller (handleRecv) runs single-threaded
// on the ESP-NOW recv task.
struct MeshMix {
  enum Bucket : uint8_t {
    kHello,
    kWispHello,
    kPaint,
    kControl,
    kEvent,
    kClaim,
    kStatus,
    kOther,
    kBucketCount,
  };

  uint32_t rx[kBucketCount] = {0};
  uint32_t relayedOut = 0;
  uint32_t windowStartMs = 0;

  static Bucket bucketFor(uint8_t msgType) {
    switch (msgType) {
      case lamp_protocol::MSG_HELLO:           return kHello;
      case lamp_protocol::MSG_WISP_HELLO:      return kWispHello;
      case lamp_protocol::MSG_WISP_PAINT:
      case lamp_protocol::MSG_OVERRIDE_COLORS: return kPaint;
      case lamp_protocol::MSG_CONTROL_OP:      return kControl;
      case lamp_protocol::MSG_EVENT:           return kEvent;
      case lamp_protocol::MSG_WISP_CLAIM:      return kClaim;
      case lamp_protocol::MSG_WISP_PALETTE:    return kStatus;
      default:                                 return kOther;
    }
  }

  void countRx(uint8_t msgType) { rx[bucketFor(msgType)]++; }

  uint32_t totalRx() const {
    uint32_t t = 0;
    for (uint32_t v : rx) t += v;
    return t;
  }

  void reset(uint32_t nowMs) {
    for (uint32_t& v : rx) v = 0;
    relayedOut = 0;
    windowStartMs = nowMs;
  }
};

}  // namespace lamp

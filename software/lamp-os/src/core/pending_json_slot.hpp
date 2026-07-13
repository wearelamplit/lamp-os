#pragma once

// PendingJsonSlot<N> — fixed-capacity single-slot byte buffer shared between
// the BLE/WiFi/ESP-NOW callbacks (Core 0 or the WiFi task) and the loop drain
// (Core 1). Consolidates the per-characteristic "post → memcpy under portMUX
// → drain → parse → dispatch" pattern into one reusable type.
//
// Contract:
//   - post(mux, data, len)   : called from any core. Returns false when
//                              len > N (defense-in-depth; the BLE callback
//                              already bounds-checks against the
//                              characteristic's max-size). On success the
//                              valid bit + length + buffer are mutated
//                              under portENTER_CRITICAL(mux).
//   - drain(mux, buf)        : called from the loop task on Core 1. Copies
//                              up to N bytes into `buf` under the same mux,
//                              clears valid, NUL-terminates buf[length],
//                              returns length. Caller must size buf >= N+1.
//                              Returns 0 (and does NOT touch buf) when the
//                              slot is empty.
//
// Single-slot semantics: post() overwrites any pending-but-undrained value.
// Newest writer wins. The BLE side intentionally relies on this — slider
// drag at 60 Hz hammers post() faster than the loop drains, and the user
// only cares about the latest value.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace lamp {

template <size_t N>
struct PendingJsonSlot {
  bool valid = false;
  uint16_t length = 0;
  char json[N];

  // Returns false when the caller's payload exceeds the slot capacity.
  // The mux is held only across the memcpy + bit flip — no parsing,
  // no allocation. Safe to call from the NimBLE host task (Core 0) and
  // from the WiFi task.
  bool post(portMUX_TYPE& mux, const char* data, size_t len) {
    if (len > N) return false;
    portENTER_CRITICAL(&mux);
    length = static_cast<uint16_t>(len);
    if (len > 0) memcpy(json, data, len);
    valid = true;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  // Returns 0 when the slot is empty; otherwise copies length bytes into
  // buf, clears valid, NUL-terminates buf[length], returns length.
  // `buf` MUST be sized >= N+1 (the +1 is the trailing NUL).
  uint16_t drain(portMUX_TYPE& mux, char* buf) {
    if (!valid) return 0;
    portENTER_CRITICAL(&mux);
    uint16_t len = length;
    if (len > 0) memcpy(buf, json, len);
    valid = false;
    portEXIT_CRITICAL(&mux);
    buf[len] = '\0';
    return len;
  }
};

// Variant carrying a 6-byte source MAC alongside the JSON payload. Used
// for the CONTROL_OP ingest path (ESP-NOW receiver memcpys the sender's
// MAC alongside the payload so the loop's applyRemoteOpLocal() can
// coalesce same-sender cascades). The BLE-only slots don't need this —
// they pass the lamp's own MAC at the drain site.
template <size_t N>
struct PendingJsonSlotWithMac {
  bool valid = false;
  uint16_t length = 0;
  uint8_t srcMac[6] = {0};
  char json[N];

  // Two-arg post: payload + source MAC. Same overflow / mux semantics
  // as PendingJsonSlot::post.
  bool post(portMUX_TYPE& mux, const char* data, size_t len,
            const uint8_t mac[6]) {
    if (len > N) return false;
    portENTER_CRITICAL(&mux);
    length = static_cast<uint16_t>(len);
    if (len > 0) memcpy(json, data, len);
    memcpy(srcMac, mac, 6);
    valid = true;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  // drain emits the MAC alongside the payload length. `buf` MUST be
  // sized >= N+1, `outMac` MUST be sized >= 6.
  uint16_t drain(portMUX_TYPE& mux, char* buf, uint8_t outMac[6]) {
    if (!valid) return 0;
    portENTER_CRITICAL(&mux);
    uint16_t len = length;
    if (len > 0) memcpy(buf, json, len);
    memcpy(outMac, srcMac, 6);
    valid = false;
    portEXIT_CRITICAL(&mux);
    buf[len] = '\0';
    return len;
  }
};

}  // namespace lamp

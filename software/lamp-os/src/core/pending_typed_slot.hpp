#pragma once

// PendingTypedSlot<T> — single-slot zero-allocation post/drain for POD
// payloads. Mirrors PendingJsonSlot's discipline (mux-guarded write,
// single-slot semantics, newest writer wins) but takes a copyable typed
// payload instead of a byte buffer.
//
// Used by transient override + wisp-hello ingest paths:
//   - MeshLink::handleRecv (WiFi recv task / Core 0) does a single
//     T copy under portMUX, no heap, no parsing.
//   - The loop task on Core 1 drains the typed payload via drain() and
//     dispatches into the override / wisp-cache modules.
//
// Contract:
//   - post(mux, src) : copies src into payload under portMUX, sets valid.
//                      Returns true (always — the bounds check is on T
//                      itself, which is required to be sized at compile
//                      time). Newest writer wins.
//   - drain(mux, out): if valid, copies payload into out, clears valid,
//                      returns true. If not valid, returns false without
//                      touching out.
//
// T MUST be trivially copyable so the memcpy semantics are well-defined
// across the portMUX boundary. We don't static_assert that here so this
// template stays usable from the native test rig where TestMux replaces
// portMUX_TYPE; the production wire-payload types (PendingOverrideColors,
// PendingWispHello, etc.) are all POD-by-construction.

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#endif

namespace lamp {

template <typename T>
struct PendingTypedSlot {
  bool valid = false;
  T payload{};

  bool post(portMUX_TYPE& mux, const T& src) {
    portENTER_CRITICAL(&mux);
#ifdef LAMP_DEBUG
    const bool overwriting = valid;
#endif
    payload = src;
    valid = true;
    portEXIT_CRITICAL(&mux);
#ifdef LAMP_DEBUG
    // Overwrite diagnostic — fires when a new post() lands on a still-valid
    // slot, meaning the previous event will be silently lost. Added
    // 2026-06-04 to measure how often this happens in practice during
    // rapid cascade triggers. sizeof(T) discriminates which slot
    // (PendingWispHello small, PendingCommand large, etc.) without needing
    // a per-slot tag parameter that would touch every forwarder.
    if (overwriting) {
      Serial.printf("[slot.overwrite] size=%u\n", (unsigned)sizeof(T));
    }
#endif
    return true;
  }

  bool drain(portMUX_TYPE& mux, T& out) {
    if (!valid) return false;
    portENTER_CRITICAL(&mux);
    out = payload;
    valid = false;
    portEXIT_CRITICAL(&mux);
    return true;
  }
};

}  // namespace lamp

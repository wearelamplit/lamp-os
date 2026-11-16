#pragma once

// PendingTypedQueue<T, N> hands per-peer POD messages from the WiFi recv task
// to the loop task without losing one that lands before the loop drains the
// last: a fixed FIFO of N, zero heap, portMUX-guarded like PendingTypedSlot.
//
// Contract:
//   - post(mux, src): appends a copy of src. Returns false and keeps the queue
//                     as it was when all N entries are waiting.
//   - drainWhile(mux, ready, fn): while ready() and an entry waits, pops the
//                     oldest and calls fn(entry) outside the critical section.
//                     Entries left when ready() turns false wait for the next
//                     call. fn may throw; the popped entry is then gone and the
//                     rest stay queued.
//
// Plain C++ so the native tests build it, as PendingTypedSlot. One task posts
// and one task drains. T must be trivially copyable (see
// PendingTypedSlot).

#include <cstddef>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <portmacro.h>
#endif

namespace lamp {

template <typename T, size_t N>
class PendingTypedQueue {
 public:
  bool post(portMUX_TYPE& mux, const T& src) {
    portENTER_CRITICAL(&mux);
    const bool full = count_ == N;
    if (!full) {
      entries_[(head_ + count_) % N] = src;
      ++count_;
    }
    portEXIT_CRITICAL(&mux);
#ifdef LAMP_DEBUG
    if (full) Serial.printf("[queue.full] size=%u\n", (unsigned)sizeof(T));
#endif
    return !full;
  }

  template <typename Ready, typename Fn>
  void drainWhile(portMUX_TYPE& mux, Ready&& ready, Fn&& fn) {
    while (count_ != 0 && ready()) {
      T out;
      portENTER_CRITICAL(&mux);
      out = entries_[head_];
      head_ = (head_ + 1) % N;
      --count_;
      portEXIT_CRITICAL(&mux);
      fn(out);
    }
  }

 private:
  T entries_[N]{};
  size_t head_ = 0;
  size_t count_ = 0;
};

}  // namespace lamp

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "config_store.hpp"

namespace lamp {

// Pure clock-only "should I flush?" helper. NO NVS, no I/O, just a dirty flag
// and the timestamp of the most recent mutation.
//
// Persisting on every disposition write is a flash-wear hazard: a single
// slider drag is ~20 writes per peer. The store marks dirty + records now()
// instead, and the Core 1 loop drain flushes once the slider goes idle.
//
// Tested in test/test_disposition_debounce; that test mirrors this shape
// inline to keep the native env free of Arduino/NVS deps. If you change the
// API here, mirror the test class.
class DispositionDebouncer {
 public:
  explicit DispositionDebouncer(uint32_t idleMs) : idleMs_(idleMs) {}

  void markDirty(uint32_t nowMs) {
    dirty_ = true;
    lastMarkMs_ = nowMs;
  }

  bool dirty() const { return dirty_; }

  // True iff dirty AND the idle window has elapsed since the most recent
  // markDirty. Caller does the flush then calls clear(). Subtraction-based so
  // millis() wraparound (~49 days) doesn't strand the dirty flag forever.
  bool shouldFlush(uint32_t nowMs) const {
    if (!dirty_) return false;
    return (nowMs - lastMarkMs_) >= idleMs_;
  }

  void clear() {
    dirty_ = false;
    lastMarkMs_ = 0;
  }

 private:
  bool dirty_ = false;
  uint32_t lastMarkMs_ = 0;
  uint32_t idleMs_;
};

// Per-peer social dispositions (BD_ADDR -> 1..5). Kept as a key-sorted vector
// for O(log N) lookup and a stable on-disk byte shape; persisted to its own
// NVS key via the injected ConfigStore, debounced to spare flash wear. The
// caller passes the clock (millis) in so this stays pure and native-testable
// against an InMemoryConfigStore.
class DispositionStore {
 public:
  static constexpr uint8_t kDefault = 3;
  static constexpr size_t kMax = 100;

  explicit DispositionStore(uint32_t flushIdleMs) : debouncer_(flushIdleMs) {}

  void attachStore(ConfigStore* store) { store_ = store; }

  // Read + parse the "dispositions" NVS key into the sorted vector.
  void load();

  // kDefault when the peer isn't present. bdAddr is canonical colon-hex.
  uint8_t get(const std::string& bdAddr) const;
  // Clamp to [1,5], insert/update preserving sort order, evict lowest-by-key
  // at capacity. Marks dirty; the actual write is deferred to a flush.
  void set(const std::string& bdAddr, uint8_t value, uint32_t nowMs);

  std::string asJson() const;
  // Bulk replace from a JSON object; parse + clamp + mark dirty. Defers the
  // NVS commit to the next flush. Returns true on a valid parse.
  bool setFromJson(const char* json, size_t len, uint32_t nowMs);

  // Core 1 loop drain: flush once idle past the window, clearing dirty only on
  // a successful write so a failed write retries next tick.
  void maybeFlush(uint32_t nowMs);
  // Synchronous flush (BLE disconnect); no-op when not dirty.
  void flushNow();

 private:
  bool persist_();

  std::vector<std::pair<std::string, uint8_t>> entries_;
  DispositionDebouncer debouncer_;
  ConfigStore* store_ = nullptr;
};

}  // namespace lamp

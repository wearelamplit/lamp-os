#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
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

// Per-peer social dispositions (mesh MAC -> 1..5). Keys are the raw 6-byte
// MAC held inline, so an entry is trivially copyable and never spawns a
// per-peer heap block (a 17-char colon-hex string would defeat SSO; see
// docs/dev/embedded-heap.md rule 3). Kept sorted by MAC for O(log N) lookup;
// the byte order matches the canonical colon-hex string order, so the on-disk
// JSON shape is stable across the packing. Persisted to its own NVS key via
// the injected ConfigStore, debounced to spare flash wear. The caller passes
// the clock (millis) in so this stays pure and native-testable against an
// InMemoryConfigStore.
//
// Read from the NimBLE host task (Core 0) via asJson() while the Core 1 loop
// drain mutates via setFromJson(); a SemaphoreHandle_t mutex serialises all
// entries_ access. JSON parse/serialize and NVS writes stay outside the lock.
class DispositionStore {
 public:
  static constexpr uint8_t kDefault = 3;
  static constexpr size_t kMax = 100;

  explicit DispositionStore(uint32_t flushIdleMs);

  void attachStore(ConfigStore* store) { store_ = store; }

  // Read + parse the "dispositions" NVS key into the sorted vector.
  void load();

  // kDefault when the peer isn't present. mac is the raw 6-byte mesh MAC.
  uint8_t get(const uint8_t mac[6]) const;
  // Clamp to [1,5], insert/update preserving sort order, evict lowest-by-key
  // at capacity. Marks dirty; the actual write is deferred to a flush.
  void set(const uint8_t mac[6], uint8_t value, uint32_t nowMs);

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

  std::vector<std::pair<std::array<uint8_t, 6>, uint8_t>> entries_;
  DispositionDebouncer debouncer_;
  ConfigStore* store_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace lamp

#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "config_store.hpp"
#include "util/retry_backoff.hpp"

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

// What every consumer reads about one peer; an unknown peer reads as these
// defaults. Ages count on-time only: the lamp has no wall clock.
struct PeerRecord {
  uint8_t disp = 3;        // 1 Salty .. 5 Smitten
  int8_t score = 0;        // drift toward the next step, 127 = one full step
  uint8_t meets = 0;       // counted meetings, saturating
  uint8_t meetAgeH = 255;  // on-hours since the last counted meeting, saturating
  uint16_t unseenMin = 0;  // on-minutes since last seen, saturating
};

// What a nudge did to the disposition.
struct NudgeResult {
  bool stepped = false;
  uint8_t from = 3;
  uint8_t to = 3;
  int8_t scoreFrom = 0;
  int8_t scoreTo = 0;
};

// Per-peer social rows (mesh MAC -> disposition, score, meetings, ages), the
// only owner of them. Keys are the raw 6-byte MAC held inline, so a row is
// trivially copyable and never spawns a per-peer heap block (see
// docs/dev/embedded-heap.md rule 3). Rows live in storage reserved once at
// kMax and never reallocated. Kept sorted by MAC for O(log N) lookup; the byte
// order matches the canonical colon-hex string order, so the JSON served to
// the app stays sorted. Persisted to one NVS blob through the injected
// ConfigStore. The caller passes the clock (uptimeMs) in so this stays pure
// and native-testable against an InMemoryConfigStore.
//
// Read from the NimBLE host task (Core 0) via asJson() and peer(); every
// mutation and flush runs on Core 1. A mutex serialises row access; JSON
// parse and NVS I/O stay outside it.
//
// Three flush tiers: a user edit (set, setFromJson,
// setEngineEnabled) flushes once idle for flushIdleMs; an engine change
// (nudge, meet, decayTowardZero) flushes at most once per kEngineFlushFloorMs
// and an age-only change (markSeen, tickHour) at most once per
// kAgeFlushFloorMs, both counted from the last flush. Engine and age calls are
// no-ops while the engine is off; any other gate on them (home mode) is the
// caller's, who also holds the unseen clock through holdClocks. Layout,
// migration and prune: docs/dev/social.md.
class DispositionStore {
 public:
  static constexpr uint8_t kDefault = 3;
  static constexpr size_t kMax = 100;

  explicit DispositionStore(uint32_t flushIdleMs);
  // A copy would shrink the reserved rows to their count; a move keeps them.
  DispositionStore(const DispositionStore&) = delete;
  DispositionStore& operator=(const DispositionStore&) = delete;
  DispositionStore(DispositionStore&&) = default;
  DispositionStore& operator=(DispositionStore&&) = default;

  void attachStore(ConfigStore* store) { store_ = store; }

  // Read the blob (or the legacy JSON key) into the rows and drop neutral rows
  // unseen for kPruneMin on-minutes. Writes only to move a legacy JSON key.
  void load(uint64_t nowMs);

  // The one read of a row. mac is the raw 6-byte mesh MAC.
  PeerRecord peer(const uint8_t mac[6], uint64_t nowMs) const;

  // A user edit: clamps to [1,5]; a changed value sets the disposition and
  // zeroes the score, an equal one changes nothing. A new peer always gets a
  // row, displacing the weakest at capacity.
  void set(const uint8_t mac[6], uint8_t value, uint64_t nowMs);

  std::string asJson() const;
  // Merges a JSON object of user edits, each as set(); unlisted rows stay.
  // Returns true on a valid parse.
  bool setFromJson(const char* json, size_t len, uint64_t nowMs);

  // Moves the score by delta + familiarityDelta; positive familiarity is
  // dropped from disposition 4 up, so familiarity alone stops at Fond. A score
  // reaching +-127 steps the disposition and restarts at 0, clamping at 1 and
  // 5. A new peer is admitted only when stronger than the weakest row. With
  // mayStep false the score stops one short of a step instead.
  NudgeResult nudge(const uint8_t mac[6], int16_t delta, int16_t familiarityDelta,
                    uint64_t nowMs, bool mayStep = true);
  // Counts a meeting when at least meetEveryH on-hours passed since the last
  // counted one. True when counted.
  bool meet(const uint8_t mac[6], uint8_t meetEveryH, uint64_t nowMs);
  // Restarts an existing row's unseen clock.
  void markSeen(const uint8_t mac[6], uint64_t nowMs);
  // Moves the score of every row unseen for an hour or more toward 0 by
  // `step`; never moves a disposition.
  void decayTowardZero(uint8_t step, uint64_t nowMs);
  // Advances every row's meetAgeH by one on-hour. Call once per on-hour.
  void tickHour(uint64_t nowMs);

  // The persisted master switch, on by default.
  bool engineEnabled() const;
  void setEngineEnabled(bool on, uint64_t nowMs);
  // Stops every row's unseen clock while held or while the engine is off, so
  // time not tracked never counts toward an absence, a prune or a reunion.
  void holdClocks(bool held, uint64_t nowMs);

  size_t peerCount() const;
  // Row `index` in key order; false past the end.
  bool peerAt(size_t index, uint8_t mac[6], PeerRecord& out, uint64_t nowMs) const;

  // Core 1 loop drain: flushes whichever tier is due. A failed write (NVS
  // full) keeps the rows dirty and backs off, 5 s doubling to 60 s.
  void maybeFlush(uint64_t nowMs);
  // Synchronous flush of a pending user edit (BLE disconnect); held to the
  // same backoff.
  void flushNow(uint64_t nowMs);

  // Native-test seam: the reserved row storage, fixed for the store's life.
  size_t rowCapacity() const { return entries_.capacity(); }
  const void* rowStorage() const { return entries_.data(); }

  // Format 2: [format][flags] then a 12-byte row per peer in key order,
  // [mac:6][disp:1][score:1][meets:1][meetAgeH:1][unseenMin:2 LE]. Format 1
  // ([format][mac:6][disp:1] per peer, up to 1.3.3) and the older JSON key
  // load; the JSON is rewritten at load, format 1 on the first flush.
  static constexpr uint8_t kBlobFormat = 2;
  static constexpr uint8_t kLegacyBlobFormat = 1;
  static constexpr uint8_t kFlagEngineOn = 0x01;
  static constexpr size_t kBlobHeaderLen = 2;
  static constexpr size_t kBlobRowLen = 12;
  static constexpr size_t kLegacyBlobRowLen = 7;
  static constexpr size_t kMaxBlobLen = kBlobHeaderLen + kMax * kBlobRowLen;
  static constexpr const char* kBlobKey = "disp";
  static constexpr const char* kLegacyJsonKey = "dispositions";
  static constexpr uint32_t kRetryBaseMs = 5000;
  static constexpr uint32_t kRetryMaxMs = 60000;
  static constexpr uint64_t kEngineFlushFloorMs = 10 * 60 * 1000;
  static constexpr uint64_t kAgeFlushFloorMs = 60 * 60 * 1000;
  static constexpr uint16_t kPruneMin = 14 * 24 * 60;
  // Eviction strength per counted meeting, up to kRegularMeetsCap meetings.
  static constexpr uint16_t kRegularWeight = 8;
  static constexpr uint8_t kRegularMeetsCap = 16;
  // A row unseen this long is absent: its score decays, and a newcomer as
  // strong as it may take its place.
  static constexpr uint16_t kAbsentMin = 60;

 private:
  struct Row {
    std::array<uint8_t, 6> mac;
    uint8_t disp;
    int8_t score;
    uint8_t meets;
    uint8_t meetAgeH;
    uint32_t seenAtMin;  // minute on the boot clock, see nowMin()
  };

  static constexpr size_t kBdAddrLen = 17;
  // `"AA:BB:CC:DD:EE:FF":5` per entry, a comma between, and the braces.
  static size_t jsonLen(size_t entries);
  // Minutes on a clock that starts at UINT16_MAX each boot, so a row loaded
  // with any stored age has a seenAtMin at or above 0. clockLocked_ is it
  // with the held stretches taken out, and never runs backwards.
  static uint32_t nowMin(uint64_t nowMs);
  uint32_t clockLocked_(uint64_t nowMs) const;
  void syncClockLocked_(uint64_t nowMs);
  static uint16_t unseenMin(const Row& row, uint32_t now);
  static uint16_t strength(const Row& row);
  static Row freshRow(const uint8_t mac[6], uint32_t now);

  // Caller holds mutex_ for the *Locked_ helpers.
  Row* findLocked_(const std::array<uint8_t, 6>& key);
  // Inserts `fresh` in key order; at capacity it displaces the weakest row
  // (ties to the longest unseen) when `force`, or when `fresh` is stronger,
  // or as strong and the weakest is absent. Null when not admitted.
  Row* admitLocked_(const Row& fresh, bool force, uint32_t now);
  bool userSetLocked_(const uint8_t mac[6], uint8_t value, uint32_t now);
  size_t encodeLocked_(uint8_t* out, uint32_t now) const;
  bool matchesLocked_(const uint8_t* blob, size_t len, uint32_t now) const;
  void writeJsonLocked_(char* out) const;

  bool persist_(uint64_t nowMs);
  void flushIfDue_(uint64_t nowMs);

  std::vector<Row> entries_;
  uint8_t flags_ = kFlagEngineOn;
  bool held_ = false;
  bool frozen_ = false;
  uint32_t frozenAtMin_ = 0;
  uint32_t clockOffsetMin_ = 0;
  DispositionDebouncer debouncer_;
  bool engineDirty_ = false;
  bool ageDirty_ = false;
  uint64_t lastFlushMs_ = 0;
  RetryBackoff retry_{kRetryBaseMs, kRetryMaxMs};
  ConfigStore* store_ = nullptr;
  SemaphoreHandle_t mutex_ = nullptr;
};

}  // namespace lamp

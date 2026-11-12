#pragma once

#include <ArduinoJson.h>
#include <esp_system.h>

#include <cstddef>
#include <cstdint>

// Crash forensics that survive into released builds. See
// docs/dev/embedded-heap.md "Reading a crash record".
namespace lamp::crash_record {

constexpr size_t kReasonLen = 32;
constexpr size_t kBacktraceLen = 4;
// uptimeMs past 49.7 days.
constexpr uint32_t kUptimeSaturated = UINT32_MAX;

// Snapshot::flags bit set once the panic handler filled reason/pc/backtrace/
// core and the fault's uptime.
constexpr uint8_t kPanicked = 1u << 0;
// Snapshot::flags bit set from the re-arm until the loop's first sample, while
// the heap and roster figures are the re-arm's, taken early in setup.
constexpr uint8_t kUnsampled = 1u << 1;

// Last-known state of the running boot, held in RTC memory across a reset.
struct Snapshot {
  char reason[kReasonLen];
  uint32_t pc;
  uint32_t backtrace[kBacktraceLen];
  uint32_t uptimeMs;
  uint32_t minLargestBlock;
  uint32_t minFreeHeap;
  uint16_t rosterPeak;
  int8_t core;  // -1 until the panic handler fills reason/pc/backtrace
  uint8_t flags;
};

// Persisted to NVS as a raw blob; a size mismatch on load reads as no record.
struct Record {
  uint32_t count;
  uint32_t fwVersion;
  uint8_t resetReason;
  uint8_t snapshotValid;
  Snapshot snap;
};

bool isCrashReset(esp_reset_reason_t reason);

// Uptime in ms for a Snapshot, saturating at kUptimeSaturated.
uint32_t uptimeMsFrom(int64_t microseconds);

// Folds this boot into the stored record. `rtc` is null when the RTC state
// failed its magic check. Returns true when `rec` changed and must be written.
bool foldBoot(Record& rec, esp_reset_reason_t reason, const Snapshot* rtc,
              uint32_t fwVersion);

// Call once, early in setup and before the panic handler is registered:
// persists a crash left by the previous boot, then re-arms the RTC state.
void boot(uint32_t fwVersion);

// Loop task, every iteration; self-throttles to 1 Hz and calls rosterCount
// only when it samples. Allocation-free.
void tick(uint32_t nowMs, size_t (*rosterCount)());

// True when the loop sampled the boot's heap and roster figures. The re-arm's
// uptime is 0, so a zero uptime also reads unsampled.
inline bool loopSampled(const Snapshot& s) {
  return (s.flags & kUnsampled) == 0 && s.uptimeMs != 0;
}

// Starts `s` for a new boot with the heap figures at the re-arm.
void rearm(Snapshot& s, uint32_t largestBlock, uint32_t minFreeHeap);

// Folds one loop sample into `s`.
void takeSample(Snapshot& s, uint32_t uptimeMs, uint32_t largestBlock, uint32_t minFreeHeap,
                size_t rosterCount);

// The reason to record for an abort's message. abort()'s message ends in its
// caller's address, which does not fit kReasonLen and which backtrace[3]
// holds, so it records without it.
const char* abortReason(const char* details);

// Fills `s` with the fault: reason (truncated), pc, the first backtrace
// addresses, core, and the uptime at the fault. Plain stores only.
void recordPanic(Snapshot& s, const char* reason, uint32_t pc,
                 const unsigned int* backtrace, unsigned int backtraceLen, int core,
                 uint32_t uptimeMs);

// Panic context only: recordPanic into the RTC snapshot. No heap, no NVS.
void capturePanic(const char* reason, const void* pc,
                  const unsigned int* backtrace, unsigned int backtraceLen,
                  int core);

// Writes `lastCrash` into the BLE lamp section; nothing when no crash counted.
void emitLastCrash(JsonDocument& doc, const Record& rec);

const Record& last();

// The RTC snapshot when it passed its magic check, else null. Before boot() it
// is the previous boot's; after boot(), this boot's.
const Snapshot* rtcSnapshot();

}  // namespace lamp::crash_record

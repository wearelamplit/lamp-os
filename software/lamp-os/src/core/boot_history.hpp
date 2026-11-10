#pragma once

#include <esp_system.h>

#include <cstddef>
#include <cstdint>

#include "core/crash_record.hpp"

// The last few boots, kept in NVS on every build and printed to serial at boot.
// See docs/dev/embedded-heap.md "Reading the boot history".
namespace lamp::boot_history {

constexpr size_t kBoots = 5;
constexpr uint16_t kVersion = 1;
constexpr uint32_t kUptimeExact = 1u << 31;

// One boot. `uptime` holds seconds; kUptimeExact marks a figure taken from the
// RTC snapshot at the next boot rather than from the last milestone.
struct BootEntry {
  uint32_t uptime;
  uint16_t minFreeHeap;
  uint16_t minLargestBlock;
  uint8_t fw[3];
  uint8_t resetReason;  // the reset that started this boot
};

// Persisted to NVS as a raw blob, oldest boot first. Sized to fit the four NVS
// entries the budget in docs/dev/embedded-heap.md allows it.
struct History {
  uint16_t version;
  uint16_t count;
  BootEntry entries[kBoots];
};
static_assert(sizeof(History) == 64, "boot history outgrew its NVS budget");

// Closes the newest boot with the surviving RTC snapshot of the boot that just
// ended. A null `rtc`, or one never sampled past boot, leaves its last
// milestone figures in place.
void finish(History& h, const crash_record::Snapshot* rtc);

// Starts an entry for this boot, dropping the oldest when full.
void append(History& h, uint32_t fwVersion, esp_reset_reason_t reason);

// Uptime milestones that rewrite the running boot's entry. `next` starts at 0
// and advances past each milestone reported due.
bool milestoneDue(uint32_t nowMs, uint8_t& next);

// At a due milestone, records the running boot as having reached `nowMs`, with
// heap figures from `now`. True when `h` changed and must be written.
bool milestoneUpdate(History& h, uint8_t& next, uint32_t nowMs,
                     const crash_record::Snapshot* now);

// True when `len` bytes loaded into `h` are a history this firmware wrote.
bool loaded(const History& h, size_t len);

// Entries that have ended, so print a line: all but the running boot.
size_t endedCount(const History& h);

// One `[boots]` line for an ended entry `i`, ended by entry `i + 1`'s reset.
void formatLine(char* out, size_t cap, const History& h, size_t i);

// Call once in setup, before crash_record::boot() re-arms the RTC snapshot:
// closes the previous boot, opens this one, writes NVS, prints the history.
void boot(uint32_t fwVersion);

// Loop task, every iteration. Writes NVS only when a milestone comes due.
void tick(uint32_t nowMs);

}  // namespace lamp::boot_history

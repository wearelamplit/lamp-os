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

// True when a loaded blob is one this firmware wrote.
bool valid(const History& h);

// Closes the newest boot with the surviving RTC snapshot of the boot that just
// ended. A null `rtc` leaves its last milestone figures in place.
void finish(History& h, const crash_record::Snapshot* rtc);

// Starts an entry for this boot, dropping the oldest when full.
void append(History& h, uint32_t fwVersion, esp_reset_reason_t reason);

// Refreshes the running boot's figures from this boot's RTC snapshot.
void update(History& h, const crash_record::Snapshot& now);

// Uptime milestones that rewrite the running boot's entry. `next` starts at 0
// and advances past each milestone reported due.
bool milestoneDue(uint32_t nowMs, uint8_t& next);

// One `[boots]` line for entry `i`, ended by entry `i + 1`'s reset. Only
// entries below count - 1 have ended. Returns the snprintf result.
int formatLine(char* out, size_t cap, const History& h, size_t i);

// Call once in setup, before crash_record::boot() re-arms the RTC snapshot:
// closes the previous boot, opens this one, writes NVS, prints the history.
void boot(uint32_t fwVersion);

// Loop task, every iteration. Writes NVS only when a milestone comes due.
void tick(uint32_t nowMs);

}  // namespace lamp::boot_history

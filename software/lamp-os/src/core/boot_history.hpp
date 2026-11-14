#pragma once

#include <esp_system.h>

#include <cstddef>
#include <cstdint>

#include "core/crash_record.hpp"

// The last few boots, kept in NVS on every build and printed to serial at boot.
// See docs/dev/embedded-heap.md "Reading the boot history".
namespace lamp::boot_history {

constexpr size_t kBoots = 5;
constexpr uint16_t kVersion = 2;
constexpr uint32_t kUptimeExact = 1u << 31;

// The last setup step a boot finished, in setup order. Unknown marks an entry
// an older firmware wrote. The value is stored, so append only.
enum class Stage : uint8_t {
  Unknown,
  Boot,
  Variant,
  Config,
  Wifi,
  Ble,
  Webapp,
  Leds,
  Behaviors,
  Mesh,
  Ota,
  Loop,
};

const char* stageName(Stage stage);
// The stage named `name`, or Unknown.
Stage stageFromName(const char* name);

// BootEntry::flags.
// `stage` came from the RTC state at the next boot; unset, it is a lower bound.
constexpr uint8_t kStageExact = 1u << 0;
// The counters hold figures; unset, they were never taken.
constexpr uint8_t kCounted = 1u << 1;
// The boot ran an image the bootloader had not seen marked valid, and its
// last write still said so. The next boot running other firmware means the
// bootloader rolled it back.
constexpr uint8_t kUnverified = 1u << 2;

// One boot. `uptime` holds seconds; kUptimeExact marks a figure taken from the
// RTC snapshot at the next boot. Unmarked, it is a lower bound: the last
// milestone reached, or a saturated snapshot. The counters saturate.
struct BootEntry {
  uint32_t uptime;
  uint16_t minFreeHeap;
  uint16_t minLargestBlock;
  uint8_t fw[3];
  uint8_t resetReason;  // the reset that started this boot
  uint16_t refusedLocal;    // local transients refused for heap
  uint16_t connects;        // BLE client connects
  uint16_t connMinLargest;  // smallest largest block while connected; 0 none
  uint8_t stage;            // a Stage
  uint8_t flags;
};

// Persisted to NVS as a raw blob, oldest boot first. Its NVS entries are in
// the budget in docs/dev/embedded-heap.md.
struct History {
  uint16_t version;
  uint16_t count;
  BootEntry entries[kBoots];
};
static_assert(sizeof(BootEntry) == 20, "BootEntry layout is stored");
static_assert(sizeof(History) == 104, "boot history outgrew its NVS budget");

// The version 1 layout, read once to migrate it.
struct BootEntryV1 {
  uint32_t uptime;
  uint16_t minFreeHeap;
  uint16_t minLargestBlock;
  uint8_t fw[3];
  uint8_t resetReason;
};
struct HistoryV1 {
  uint16_t version;
  uint16_t count;
  BootEntryV1 entries[kBoots];
};
static_assert(sizeof(HistoryV1) == 64, "the version 1 layout is fixed");

// The running boot's figures that are not in the crash record's snapshot,
// kept in RTC memory across a reset. A layout change takes a new magic.
struct Live {
  uint8_t stage;  // a Stage
  uint8_t fw[3];  // the firmware that wrote it
  uint16_t refusedLocal;
  uint16_t connects;
  uint16_t connMinLargest;
  uint8_t resetReason;  // the reset that started the boot
  uint8_t stored;       // the boot's entry reached NVS
};

// Fills `out` from a stored blob of `len` bytes in this layout, or in version
// 1, migrated. False when the blob is neither.
bool decode(const void* blob, size_t len, History& out);

// Closes the newest boot with the surviving RTC state of the boot that just
// ended. A null `rtc`, or one never sampled past boot, leaves its last
// milestone figures in place; a null `live`, or one another firmware wrote,
// leaves its stage and counters. A boot whose entry never reached NVS gets its
// entry back from `live` before the fold.
void finish(History& h, const crash_record::Snapshot* rtc, const Live* live);

// Starts an entry for this boot, dropping the oldest when full.
void append(History& h, uint32_t fwVersion, esp_reset_reason_t reason);

// The running boot finished setup.
void markSetupDone(History& h);

// The running boot's image is unverified, or was just marked valid.
void markUnverified(History& h);
void markVerified(History& h);

// The ended entry `i` ran an image that was rolled back at its end.
bool rolledBack(const History& h, size_t i);

// Uptime milestones that rewrite the running boot's entry. `next` starts at 0
// and advances past each milestone reported due.
bool milestoneDue(uint32_t nowMs, uint8_t& next);

// At a due milestone, records the running boot as having reached `nowMs`, with
// heap figures from `now` and counters from `live`. True when `h` changed and
// must be written.
bool milestoneUpdate(History& h, uint8_t& next, uint32_t nowMs,
                     const crash_record::Snapshot* now, const Live* live);

// Live state for a new boot of `fwVersion`, started by `reason`.
Live freshLive(uint32_t fwVersion, esp_reset_reason_t reason);

// True when a heap sample is due at `nowMs` while connected, `intervalMs` after
// the last one at `lastMs`, across the millis() wrap.
bool connSampleDue(bool connected, uint32_t lastMs, uint32_t nowMs, uint32_t intervalMs);

// Folds one heap sample taken while a client was connected.
void foldConnectedSample(Live& live, uint32_t largestBlock);

// Adds one, saturating.
void countUp(uint16_t& counter);

// Entries that have ended, so print a line: all but the running boot.
size_t endedCount(const History& h);

// Holds the widest line formatLine writes.
constexpr size_t kLineCap = 160;

// One `[boots]` line for an ended entry `i`, ended by entry `i + 1`'s reset.
void formatLine(char* out, size_t cap, const History& h, size_t i);

// Call once in setup, right after NVS is up and before crash_record::boot()
// re-arms the RTC snapshot: closes the previous boot, opens this one, writes
// NVS, prints the history. `unverified` is the running image's PENDING_VERIFY.
void boot(uint32_t fwVersion, bool unverified);

// The running image was marked valid. Writes NVS. Loop task only.
void imageVerified();

// Records that setup finished `stage`. Reaching Loop also writes NVS. Loop
// task only.
void reach(Stage stage);

// This boot's live state, or before boot() the previous boot's; null when the
// RTC state failed its magic check.
const Live* rtcLive();

// A local transient refused because the heap could not spare it. Loop task.
void noteLocalHeapRefusal();

// A BLE client connected. The NimBLE host task, the counter's only writer.
void noteClientConnect();

// Loop task, every iteration. Samples the heap at 1 Hz while a client is
// connected; writes NVS only when a milestone comes due.
void tick(uint32_t nowMs, bool clientConnected);

}  // namespace lamp::boot_history

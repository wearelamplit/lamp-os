#include "core/crash_record.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <nvs.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#endif

namespace lamp::crash_record {

bool isCrashReset(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:
      return true;
    default:
      return false;
  }
}

uint32_t uptimeMsFrom(int64_t microseconds) {
  const int64_t ms = microseconds / 1000;
  return ms >= int64_t{kUptimeSaturated} ? kUptimeSaturated : static_cast<uint32_t>(ms);
}

bool foldBoot(Record& rec, esp_reset_reason_t reason, const Snapshot* rtc,
              uint32_t fwVersion) {
  const bool panicked = rtc != nullptr && rtc->panicked;
  if (!isCrashReset(reason) && !panicked) return false;
  rec.count++;
  rec.fwVersion = fwVersion;
  rec.resetReason = static_cast<uint8_t>(reason);
  rec.snapshotValid = rtc != nullptr;
  rec.snap = rtc != nullptr ? *rtc : Snapshot{};
  if (rtc == nullptr) rec.snap.core = -1;
  return true;
}

void recordPanic(Snapshot& s, const char* reason, uint32_t pc,
                 const unsigned int* backtrace, unsigned int backtraceLen, int core,
                 uint32_t uptimeMs) {
  size_t i = 0;
  for (; reason != nullptr && reason[i] != '\0' && i < kReasonLen - 1; ++i) {
    s.reason[i] = reason[i];
  }
  s.reason[i] = '\0';
  s.pc = pc;
  for (size_t b = 0; b < kBacktraceLen; ++b) {
    s.backtrace[b] = b < backtraceLen ? backtrace[b] : 0;
  }
  s.core = static_cast<int8_t>(core);
  s.uptimeMs = uptimeMs;
  s.panicked = 1;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

constexpr uint32_t kRtcMagic = 0x4c43524au;
constexpr uint32_t kSampleIntervalMs = 1000;
constexpr const char* kNvsNamespace = "crash";
constexpr const char* kNvsKey = "last";

struct RtcState {
  uint32_t magic;
  Snapshot snap;
};

RTC_NOINIT_ATTR RtcState s_rtc;
Record s_last{};

void sample(uint32_t uptimeMs, size_t rosterCount) {
  Snapshot& s = s_rtc.snap;
  const uint32_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (largest < s.minLargestBlock) s.minLargestBlock = largest;
  s.minFreeHeap = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
  if (rosterCount > s.rosterPeak) s.rosterPeak = static_cast<uint16_t>(rosterCount);
  s.uptimeMs = uptimeMs;
}

}  // namespace

void boot(uint32_t fwVersion) {
  const bool rtcValid = s_rtc.magic == kRtcMagic;
  if (rtcValid) s_rtc.snap.reason[kReasonLen - 1] = '\0';

  // The nvs_* API directly: a failed write returns an error to ignore, where
  // Preferences would log it through a path that faults on a starved heap.
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK) {
    size_t len = sizeof(Record);
    Record stored{};
    if (nvs_get_blob(h, kNvsKey, &stored, &len) == ESP_OK && len == sizeof(Record)) {
      s_last = stored;
    }
    if (foldBoot(s_last, esp_reset_reason(), rtcValid ? &s_rtc.snap : nullptr,
                 fwVersion) &&
        nvs_set_blob(h, kNvsKey, &s_last, sizeof(Record)) == ESP_OK) {
      nvs_commit(h);
    }
    nvs_close(h);
  }

  s_rtc = RtcState{};
  s_rtc.magic = kRtcMagic;
  s_rtc.snap.core = -1;
  s_rtc.snap.minLargestBlock = UINT32_MAX;
  sample(0, 0);
}

void tick(uint32_t nowMs, size_t (*rosterCount)()) {
  static uint32_t s_nextMs = 0;
  if (static_cast<int32_t>(nowMs - s_nextMs) < 0) return;
  s_nextMs = nowMs + kSampleIntervalMs;
  sample(uptimeMsFrom(esp_timer_get_time()), rosterCount());
}

void capturePanic(const char* reason, const void* pc,
                  const unsigned int* backtrace, unsigned int backtraceLen,
                  int core) {
  recordPanic(s_rtc.snap, reason, reinterpret_cast<uint32_t>(pc), backtrace, backtraceLen,
              core, uptimeMsFrom(esp_timer_get_time()));
}

const Record& last() { return s_last; }

const Snapshot* rtcSnapshot() { return s_rtc.magic == kRtcMagic ? &s_rtc.snap : nullptr; }

#endif

}  // namespace lamp::crash_record

#include "core/boot_history.hpp"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <nvs.h>
#endif

namespace lamp::boot_history {

namespace {

constexpr uint32_t kMilestonesS[] = {60, 3600, 86400};

uint16_t saturate16(uint32_t v) { return v > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(v); }

void takeFigures(BootEntry& e, const crash_record::Snapshot& s) {
  e.uptime = s.uptimeMs / 1000;
  e.minFreeHeap = saturate16(s.minFreeHeap);
  e.minLargestBlock = saturate16(s.minLargestBlock);
}

const char* resetName(uint8_t reason) {
  static constexpr const char* kNames[] = {
      "unknown", "power", "ext", "restart", "panic", "intwdt", "taskwdt", "wdt",
      "sleep", "brownout", "sdio", "usb", "jtag", "efuse", "glitch", "lockup"};
  return reason < sizeof(kNames) / sizeof(kNames[0]) ? kNames[reason] : "?";
}

void formatUptime(char* out, size_t cap, uint32_t s) {
  if (s < 60) {
    std::snprintf(out, cap, "%lus", static_cast<unsigned long>(s));
  } else if (s < 3600) {
    std::snprintf(out, cap, "%lum%02lus", static_cast<unsigned long>(s / 60),
                  static_cast<unsigned long>(s % 60));
  } else if (s < 86400) {
    std::snprintf(out, cap, "%luh%02lum", static_cast<unsigned long>(s / 3600),
                  static_cast<unsigned long>(s % 3600 / 60));
  } else {
    std::snprintf(out, cap, "%lud%02luh", static_cast<unsigned long>(s / 86400),
                  static_cast<unsigned long>(s % 86400 / 3600));
  }
}

}  // namespace

bool valid(const History& h) { return h.version == kVersion && h.count <= kBoots; }

void finish(History& h, const crash_record::Snapshot* rtc) {
  if (h.count == 0 || rtc == nullptr) return;
  BootEntry& e = h.entries[h.count - 1];
  takeFigures(e, *rtc);
  e.uptime |= kUptimeExact;
}

void append(History& h, uint32_t fwVersion, esp_reset_reason_t reason) {
  if (h.count == kBoots) {
    std::memmove(&h.entries[0], &h.entries[1], sizeof(BootEntry) * (kBoots - 1));
    --h.count;
  }
  BootEntry& e = h.entries[h.count++];
  e = BootEntry{};
  e.fw[0] = static_cast<uint8_t>(fwVersion >> 16);
  e.fw[1] = static_cast<uint8_t>(fwVersion >> 8);
  e.fw[2] = static_cast<uint8_t>(fwVersion);
  e.resetReason = static_cast<uint8_t>(reason);
}

void update(History& h, const crash_record::Snapshot& now) {
  if (h.count == 0) return;
  takeFigures(h.entries[h.count - 1], now);
}

bool milestoneDue(uint32_t nowMs, uint8_t& next) {
  constexpr size_t kCount = sizeof(kMilestonesS) / sizeof(kMilestonesS[0]);
  if (next >= kCount || nowMs / 1000 < kMilestonesS[next]) return false;
  while (next < kCount && nowMs / 1000 >= kMilestonesS[next]) ++next;
  return true;
}

int formatLine(char* out, size_t cap, const History& h, size_t i) {
  const BootEntry& e = h.entries[i];
  char up[16];
  formatUptime(up, sizeof(up), e.uptime & ~kUptimeExact);
  // An entry no milestone or RTC fold has touched holds no heap figures yet.
  char heap[32] = "free - big -";
  if (e.uptime != 0) {
    std::snprintf(heap, sizeof(heap), "free %u big %u", e.minFreeHeap, e.minLargestBlock);
  }
  return std::snprintf(out, cap, "[boots] -%u fw%u.%u.%u up %s%s %s end %s",
                       static_cast<unsigned>(h.count - 1 - i), e.fw[0], e.fw[1], e.fw[2],
                       (e.uptime & kUptimeExact) ? "" : ">", up, heap,
                       resetName(h.entries[i + 1].resetReason));
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

constexpr const char* kNvsNamespace = "crash";
constexpr const char* kNvsKey = "boots";

History s_history{};
uint8_t s_nextMilestone = 0;

void save() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  if (nvs_set_blob(h, kNvsKey, &s_history, sizeof(History)) == ESP_OK) nvs_commit(h);
  nvs_close(h);
}

}  // namespace

void boot(uint32_t fwVersion) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
    size_t len = sizeof(History);
    if (nvs_get_blob(h, kNvsKey, &s_history, &len) != ESP_OK || len != sizeof(History) ||
        !valid(s_history)) {
      s_history = History{};
    }
    nvs_close(h);
  }
  s_history.version = kVersion;
  finish(s_history, crash_record::rtcSnapshot());
  append(s_history, fwVersion, esp_reset_reason());
  save();

  char line[96];
  for (size_t i = 0; i + 1 < s_history.count; ++i) {
    formatLine(line, sizeof(line), s_history, i);
    Serial.println(line);
  }
}

void tick(uint32_t nowMs) {
  if (!milestoneDue(nowMs, s_nextMilestone)) return;
  const crash_record::Snapshot* now = crash_record::rtcSnapshot();
  if (now == nullptr) return;
  update(s_history, *now);
  save();
}

#endif

}  // namespace lamp::boot_history

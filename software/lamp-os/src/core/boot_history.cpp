#include "core/boot_history.hpp"

#include <cstdio>
#include <cstring>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_heap_caps.h>
#include <nvs.h>
#endif

namespace lamp::boot_history {

namespace {

constexpr uint32_t kMilestonesS[] = {60, 3600, 86400};

uint16_t saturate16(uint32_t v) { return v > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(v); }

void packFw(uint8_t (&fw)[3], uint32_t version) {
  fw[0] = static_cast<uint8_t>(version >> 16);
  fw[1] = static_cast<uint8_t>(version >> 8);
  fw[2] = static_cast<uint8_t>(version);
}

void takeCounters(BootEntry& e, const Live& live) {
  e.refusedLocal = live.refusedLocal;
  e.connects = live.connects;
  e.connMinLargest = live.connMinLargest;
  e.flags |= kCounted;
}

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

constexpr const char* kStageNames[] = {"?",    "boot", "variant", "config", "wifi",      "ble",
                                       "webapp", "leds", "behaviors", "mesh", "ota", "loop"};
static_assert(sizeof(kStageNames) / sizeof(kStageNames[0]) == static_cast<size_t>(Stage::Loop) + 1,
              "one name per stage");

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

const char* stageName(Stage stage) {
  const size_t i = static_cast<size_t>(stage);
  return i < sizeof(kStageNames) / sizeof(kStageNames[0]) ? kStageNames[i] : "?";
}

Stage stageFromName(const char* name) {
  for (size_t i = 1; name != nullptr && i < sizeof(kStageNames) / sizeof(kStageNames[0]); ++i) {
    if (std::strcmp(name, kStageNames[i]) == 0) return static_cast<Stage>(i);
  }
  return Stage::Unknown;
}

bool decode(const void* blob, size_t len, History& out) {
  if (len == sizeof(History)) {
    History h;
    std::memcpy(&h, blob, sizeof(h));
    if (h.version != kVersion || h.count > kBoots) return false;
    out = h;
    return true;
  }
  if (len == sizeof(HistoryV1)) {
    HistoryV1 v1;
    std::memcpy(&v1, blob, sizeof(v1));
    if (v1.version != 1 || v1.count > kBoots) return false;
    out = History{};
    out.version = kVersion;
    out.count = v1.count;
    for (size_t i = 0; i < v1.count; ++i) {
      BootEntry& e = out.entries[i];
      e.uptime = v1.entries[i].uptime;
      e.minFreeHeap = v1.entries[i].minFreeHeap;
      e.minLargestBlock = v1.entries[i].minLargestBlock;
      std::memcpy(e.fw, v1.entries[i].fw, sizeof(e.fw));
      e.resetReason = v1.entries[i].resetReason;
    }
    return true;
  }
  return false;
}

size_t endedCount(const History& h) { return h.count == 0 ? 0 : h.count - 1; }

void finish(History& h, const crash_record::Snapshot* rtc, const Live* live) {
  if (h.count == 0 || (live != nullptr && !live->stored)) return;
  BootEntry& e = h.entries[h.count - 1];
  if (live != nullptr && std::memcmp(live->fw, e.fw, sizeof(e.fw)) == 0) {
    takeCounters(e, *live);
    e.stage = live->stage;
    e.flags |= kStageExact;
  }
  if (rtc == nullptr || !crash_record::loopSampled(*rtc)) return;
  takeFigures(e, *rtc);
  if (rtc->uptimeMs != crash_record::kUptimeSaturated) e.uptime |= kUptimeExact;
}

void append(History& h, uint32_t fwVersion, esp_reset_reason_t reason) {
  if (h.count == kBoots) {
    std::memmove(&h.entries[0], &h.entries[1], sizeof(BootEntry) * (kBoots - 1));
    --h.count;
  }
  BootEntry& e = h.entries[h.count++];
  e = BootEntry{};
  packFw(e.fw, fwVersion);
  e.resetReason = static_cast<uint8_t>(reason);
  e.stage = static_cast<uint8_t>(Stage::Boot);
}

void markSetupDone(History& h) {
  if (h.count == 0) return;
  h.entries[h.count - 1].stage = static_cast<uint8_t>(Stage::Loop);
}

Live freshLive(uint32_t fwVersion) {
  Live live{};
  live.stage = static_cast<uint8_t>(Stage::Boot);
  packFw(live.fw, fwVersion);
  return live;
}

bool connSampleDue(bool connected, uint32_t lastMs, uint32_t nowMs, uint32_t intervalMs) {
  return connected && nowMs - lastMs >= intervalMs;
}

void foldConnectedSample(Live& live, uint32_t largestBlock) {
  const uint16_t v = largestBlock == 0 ? 1 : saturate16(largestBlock);
  if (live.connMinLargest == 0 || v < live.connMinLargest) live.connMinLargest = v;
}

void countUp(uint16_t& counter) {
  if (counter < UINT16_MAX) ++counter;
}

bool milestoneDue(uint32_t nowMs, uint8_t& next) {
  constexpr size_t kCount = sizeof(kMilestonesS) / sizeof(kMilestonesS[0]);
  if (next >= kCount || nowMs / 1000 < kMilestonesS[next]) return false;
  while (next < kCount && nowMs / 1000 >= kMilestonesS[next]) ++next;
  return true;
}

bool milestoneUpdate(History& h, uint8_t& next, uint32_t nowMs,
                     const crash_record::Snapshot* now, const Live* live) {
  if (h.count == 0 || now == nullptr || !milestoneDue(nowMs, next)) return false;
  BootEntry& e = h.entries[h.count - 1];
  takeFigures(e, *now);
  e.uptime = nowMs / 1000;
  if (live != nullptr) takeCounters(e, *live);
  return true;
}

void formatLine(char* out, size_t cap, const History& h, size_t i) {
  const BootEntry& e = h.entries[i];
  char up[16];
  formatUptime(up, sizeof(up), e.uptime & ~kUptimeExact);
  // An entry no milestone or RTC fold has touched holds no heap figures yet.
  char heap[32] = "free - big -";
  if (e.uptime != 0) {
    std::snprintf(heap, sizeof(heap), "free %u big %u", e.minFreeHeap, e.minLargestBlock);
  }
  int n = std::snprintf(out, cap, "[boots] -%u fw%u.%u.%u up %s%s %s end %s",
                        static_cast<unsigned>(h.count - 1 - i), e.fw[0], e.fw[1], e.fw[2],
                        (e.uptime & kUptimeExact) ? "" : ">", up, heap,
                        resetName(h.entries[i + 1].resetReason));
  const Stage stage = static_cast<Stage>(e.stage);
  if (stage == Stage::Unknown || n < 0 || static_cast<size_t>(n) >= cap) return;
  if (stage < Stage::Loop) {
    n += std::snprintf(out + n, cap - n, " setup %s%s",
                       (e.flags & kStageExact) ? "" : ">", stageName(stage));
    if (n < 0 || static_cast<size_t>(n) >= cap) return;
  }
  if ((e.flags & kCounted) == 0) {
    std::snprintf(out + n, cap - n, " refused - conn - connbig -");
    return;
  }
  char connBig[8] = "-";
  if (e.connMinLargest != 0) std::snprintf(connBig, sizeof(connBig), "%u", e.connMinLargest);
  std::snprintf(out + n, cap - n, " refused %u conn %u connbig %s", e.refusedLocal, e.connects,
                connBig);
}

namespace {

constexpr uint32_t kLiveMagic = 0x4c424856u;

struct LiveState {
  uint32_t magic;
  Live live;
};

#if defined(ARDUINO) || defined(ESP_PLATFORM)
RTC_NOINIT_ATTR LiveState s_live;
#else
LiveState s_live{kLiveMagic, freshLive(0)};
#endif

}  // namespace

const Live* rtcLive() { return s_live.magic == kLiveMagic ? &s_live.live : nullptr; }

void noteLocalHeapRefusal() { countUp(s_live.live.refusedLocal); }

void noteClientConnect() { countUp(s_live.live.connects); }

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

constexpr const char* kNvsNamespace = "crash";
constexpr const char* kNvsKey = "boots";

constexpr uint32_t kConnSampleIntervalMs = 1000;

History s_history{};
uint8_t s_nextMilestone = 0;
uint32_t s_lastConnSampleMs = 0;

bool save() {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_set_blob(h, kNvsKey, &s_history, sizeof(History)) == ESP_OK &&
                  nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace

void boot(uint32_t fwVersion) {
  nvs_handle_t h;
  if (nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
    alignas(History) uint8_t blob[sizeof(History)];
    size_t len = sizeof(blob);
    if (nvs_get_blob(h, kNvsKey, blob, &len) != ESP_OK || !decode(blob, len, s_history)) {
      s_history = History{};
    }
    nvs_close(h);
  }
  s_history.version = kVersion;
  finish(s_history, crash_record::rtcSnapshot(), rtcLive());
  s_live.magic = kLiveMagic;
  s_live.live = freshLive(fwVersion);
  append(s_history, fwVersion, esp_reset_reason());
  s_live.live.stored = save() ? 1 : 0;

  char line[kLineCap];
  for (size_t i = 0; i < endedCount(s_history); ++i) {
    formatLine(line, sizeof(line), s_history, i);
    Serial.println(line);
  }
}

void reach(Stage stage) {
  s_live.live.stage = static_cast<uint8_t>(stage);
  if (stage != Stage::Loop) return;
  markSetupDone(s_history);
  if (save()) s_live.live.stored = 1;
}

void tick(uint32_t nowMs, bool clientConnected) {
  if (connSampleDue(clientConnected, s_lastConnSampleMs, nowMs, kConnSampleIntervalMs)) {
    s_lastConnSampleMs = nowMs;
    foldConnectedSample(s_live.live, heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
  }
  if (milestoneUpdate(s_history, s_nextMilestone, nowMs, crash_record::rtcSnapshot(),
                      rtcLive()) &&
      save()) {
    s_live.live.stored = 1;
  }
}

#endif

}  // namespace lamp::boot_history

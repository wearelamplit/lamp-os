#include "util/loop_watchdog.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#ifdef LAMP_DEBUG
#include "util/log_rate_limiter.hpp"
#endif
#endif

namespace lamp::loop_watchdog {

void LongestGap::mark(uint32_t nowMs) {
  const uint32_t gap = nowMs - lastMarkMs_;
  lastMarkMs_ = nowMs;
  const uint32_t before = longest_.peek();
  if (longest_.observe(gap) > before) unreported_ = true;
}

uint32_t LongestGap::takeReport() {
  unreported_ = false;
  return longest_.peek();
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

constexpr uint32_t kRtcMagic = 0x4c574447u;

struct RtcState {
  uint32_t magic;
  uint8_t streak;
};

RTC_NOINIT_ATTR RtcState s_rtc;
TaskHandle_t s_loopTask = nullptr;
bool s_streakCleared = false;

#ifdef LAMP_DEBUG
LongestGap s_unfed;
LongestGap s_iteration;
LogRateLimiter s_unfedLog(/*windowMs=*/1000, /*linesPerWindow=*/1);
LogRateLimiter s_iterationLog(/*windowMs=*/1000, /*linesPerWindow=*/1);

void report(LongestGap& gap, LogRateLimiter& log, uint32_t nowMs,
            const char* what) {
  if (gap.reportDue() && log.admit(nowMs).print) {
    Serial.printf("[loopwd] longest %s %u ms\n", what,
                  (unsigned)gap.takeReport());
  }
}
#endif

}  // namespace

void begin() {
  if (s_loopTask != nullptr) return;
  const uint8_t streak =
      bootStreak(s_rtc.magic == kRtcMagic, s_rtc.streak,
                 esp_reset_reason() == ESP_RST_TASK_WDT);
  s_rtc.magic = kRtcMagic;
  s_rtc.streak = streak;
  if (!watchesBoot(streak)) {
    Serial.printf("[loopwd] %u watchdog resets in a row; loop unwatched until "
                  "the next restart\n",
                  (unsigned)streak);
    return;
  }
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (esp_task_wdt_add(self) != ESP_OK) {
#ifdef LAMP_DEBUG
    Serial.println("[loopwd] subscribe FAILED; loop unwatched");
#endif
    return;
  }
  s_loopTask = self;
#ifdef LAMP_DEBUG
  s_unfed.start(millis());
  s_iteration.start(millis());
#endif
}

void feed() {
  if (!feedsFrom(xTaskGetCurrentTaskHandle(), s_loopTask)) return;
  esp_task_wdt_reset();
  const uint32_t nowMs = millis();
  if (!s_streakCleared && nowMs >= kStableUptimeMs) {
    s_rtc.streak = 0;
    s_streakCleared = true;
  }
#ifdef LAMP_DEBUG
  s_unfed.mark(nowMs);
  report(s_unfed, s_unfedLog, nowMs, "unfed span");
#endif
}

void startIteration() {
  feed();
#ifdef LAMP_DEBUG
  if (!feedsFrom(xTaskGetCurrentTaskHandle(), s_loopTask)) return;
  const uint32_t nowMs = millis();
  s_iteration.mark(nowMs);
  report(s_iteration, s_iterationLog, nowMs, "loop iteration");
#endif
}

#endif

}  // namespace lamp::loop_watchdog

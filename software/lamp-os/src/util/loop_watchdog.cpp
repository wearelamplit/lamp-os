#include "util/loop_watchdog.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
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

bool BootGuard::atBoot(bool stateValid, bool taskWdtReset) {
  streak = bootStreak(stateValid, streak, stableSeen != 0, taskWdtReset);
  stableSeen = 0;
  return watchesBoot(streak);
}

void BootGuard::markUptime(uint32_t uptimeMs) {
  if (reachedStable(uptimeMs)) stableSeen = 1;
}

bool markIteration(BootGuard& guard, uint32_t uptimeMs, const void* current,
                   const void* subscribed) {
  guard.markUptime(uptimeMs);
  return feedsFrom(current, subscribed);
}

bool takeDueReport(LongestGap& gap, LogRateLimiter& log, uint32_t nowMs,
                   uint32_t& longestMs) {
  if (!gap.reportDue() || !log.admit(nowMs).print) return false;
  longestMs = gap.takeReport();
  return true;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

constexpr uint32_t kRtcMagic = 0x4c574447u;

struct RtcState {
  uint32_t magic;
  BootGuard guard;
};

RTC_NOINIT_ATTR RtcState s_rtc;
TaskHandle_t s_loopTask = nullptr;

#ifdef LAMP_DEBUG
LongestGap s_unfed;
LongestGap s_iteration;
LogRateLimiter s_unfedLog(/*windowMs=*/1000, /*linesPerWindow=*/1);
LogRateLimiter s_iterationLog(/*windowMs=*/1000, /*linesPerWindow=*/1);

void report(LongestGap& gap, LogRateLimiter& log, uint32_t nowMs,
            const char* what) {
  uint32_t longestMs = 0;
  if (takeDueReport(gap, log, nowMs, longestMs)) {
    Serial.printf("[loopwd] longest %s %u ms\n", what, (unsigned)longestMs);
  }
}
#endif

}  // namespace

void begin() {
  if (s_loopTask != nullptr) return;
  const bool stateValid = s_rtc.magic == kRtcMagic;
  s_rtc.magic = kRtcMagic;
  if (!s_rtc.guard.atBoot(stateValid,
                          esp_reset_reason() == ESP_RST_TASK_WDT)) {
    Serial.printf("[loopwd] %u watchdog resets in a row; loop unwatched "
                  "until a reset that is not a task-watchdog reset within "
                  "60 s of boot\n",
                  (unsigned)s_rtc.guard.streak);
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
#ifdef LAMP_DEBUG
  const uint32_t nowMs = millis();
  s_unfed.mark(nowMs);
  report(s_unfed, s_unfedLog, nowMs, "unfed span");
#endif
}

void startIteration() {
  if (!markIteration(s_rtc.guard, millis(), xTaskGetCurrentTaskHandle(),
                     s_loopTask)) {
    return;
  }
  feed();
#ifdef LAMP_DEBUG
  const uint32_t nowMs = millis();
  s_iteration.mark(nowMs);
  report(s_iteration, s_iterationLog, nowMs, "loop iteration");
#endif
}

#endif

}  // namespace lamp::loop_watchdog

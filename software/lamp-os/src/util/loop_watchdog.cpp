#include "util/loop_watchdog.hpp"

#include <atomic>

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sdkconfig.h>

#include "util/uptime.hpp"
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

Mode BootGuard::atBoot(bool stateValid, bool taskWdtReset) {
  streak = bootStreak(stateValid, streak, reached, taskWdtReset);
  reached = Reached::None;
  return modeFor(streak);
}

void BootGuard::markUptime(uint32_t uptimeMs) {
  if (reachedSettled(uptimeMs)) {
    reached = Reached::Settled;
  } else if (uptimeMs >= backoffWindowMs(streak) && reached == Reached::None) {
    reached = Reached::Window;
  }
}

bool markIteration(BootGuard& guard, uint32_t uptimeMs, const void* current,
                   const void* subscribed) {
  guard.markUptime(uptimeMs);
  return feedsFrom(current, subscribed);
}

bool takeDueReport(LongestGap& gap, LogRateLimiter& log, uint64_t nowMs,
                   uint32_t& longestMs) {
  if (!gap.reportDue() || !log.admit(nowMs).print) return false;
  longestMs = gap.takeReport();
  return true;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

static_assert(kLoopTimeoutMs == CONFIG_ESP_TASK_WDT_TIMEOUT_S * 1000u,
              "the loop timeout is the task watchdog's configured default");

#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0
constexpr bool kCheckIdle0 = true;
#else
constexpr bool kCheckIdle0 = false;
#endif
#ifdef CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1
constexpr bool kCheckIdle1 = true;
#else
constexpr bool kCheckIdle1 = false;
#endif

constexpr uint32_t kRtcMagic = 0x4c574447u;

struct RtcState {
  uint32_t magic;
  BootGuard guard;
};

RTC_NOINIT_ATTR RtcState s_rtc;
TaskHandle_t s_loopTask = nullptr;
// Written by the loop task, read by any task that feeds.
std::atomic<TaskHandle_t> s_subscribed{nullptr};
Mode s_mode = Mode::Normal;
Phase s_phase = Phase::Setup;
bool s_wide = false;
bool s_subscribeFailed = false;
RetryClock s_subscribeRetry;

#ifdef LAMP_DEBUG
LongestGap s_unfed;
LongestGap s_iteration;
LogRateLimiter s_unfedLog(/*windowMs=*/1000, /*linesPerWindow=*/1);
LogRateLimiter s_iterationLog(/*windowMs=*/1000, /*linesPerWindow=*/1);

void report(LongestGap& gap, LogRateLimiter& log, const char* what) {
  uint32_t longestMs = 0;
  if (takeDueReport(gap, log, uptimeMs(), longestMs)) {
    Serial.printf("[loopwd] longest %s %u ms\n", what, (unsigned)longestMs);
  }
}
#endif

void apply() {
  esp_task_wdt_config_t config = {
      .timeout_ms = timeoutMs(s_phase, s_wide, s_mode == Mode::Backoff),
      .idle_core_mask = idleCoreMask(kCheckIdle0, kCheckIdle1),
      .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&config);
}

void subscribe() {
  if (s_subscribed.load() != nullptr) return;
  s_subscribeRetry.tried(millis());
  if (esp_task_wdt_add(s_loopTask) != ESP_OK) {
    if (!s_subscribeFailed) Serial.println("[loopwd] subscribe FAILED; retrying");
    s_subscribeFailed = true;
    return;
  }
  s_subscribed.store(s_loopTask);
  if (s_subscribeFailed) Serial.println("[loopwd] subscribed");
}

}  // namespace

void beginSetup() {
  if (s_loopTask != nullptr) return;
  s_loopTask = xTaskGetCurrentTaskHandle();
  const bool stateValid = s_rtc.magic == kRtcMagic;
  s_rtc.magic = kRtcMagic;
  s_mode = s_rtc.guard.atBoot(stateValid, esp_reset_reason() == ESP_RST_TASK_WDT);
  s_phase = Phase::Setup;
  apply();
  subscribe();
  if (s_mode == Mode::Backoff) {
    Serial.printf("[loopwd] %u watchdog resets in a row; timeout %u s until the loop "
                  "runs %u s\n",
                  (unsigned)s_rtc.guard.streak, (unsigned)(kBackoffTimeoutMs / 1000),
                  (unsigned)(backoffWindowMs(s_rtc.guard.streak) / 1000));
  }
}

void beginLoop() {
  s_phase = Phase::Loop;
  apply();
  subscribe();
#ifdef LAMP_DEBUG
  s_unfed.start(millis());
  s_iteration.start(millis());
#endif
}

void widen() {
  s_wide = true;
  apply();
}

void restore() {
  s_wide = false;
  apply();
}

void feed() {
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (s_loopTask == nullptr || self != s_loopTask) return;
  const uint32_t nowMs = millis();
  if (s_subscribed.load() == nullptr && s_subscribeRetry.due(nowMs)) {
    subscribe();
  }
  if (!feedsFrom(self, s_subscribed.load())) return;
  esp_task_wdt_reset();
#ifdef LAMP_DEBUG
  if (s_phase != Phase::Loop) return;
  s_unfed.mark(nowMs);
  report(s_unfed, s_unfedLog, "unfed span");
#endif
}

void startIteration() {
  const uint32_t nowMs = millis();
  if (rearmDue(s_mode, s_rtc.guard, nowMs)) {
    s_mode = Mode::Normal;
    apply();
    Serial.printf("[loopwd] loop ran %u s; timeout back to %u s\n",
                  (unsigned)(backoffWindowMs(s_rtc.guard.streak) / 1000),
                  (unsigned)(timeoutMs(s_phase, s_wide, false) / 1000));
  }
  feed();
  if (!markIteration(s_rtc.guard, nowMs, xTaskGetCurrentTaskHandle(),
                     s_subscribed.load())) {
    return;
  }
#ifdef LAMP_DEBUG
  s_iteration.mark(nowMs);
  report(s_iteration, s_iterationLog, "loop iteration");
#endif
}

#endif

}  // namespace lamp::loop_watchdog

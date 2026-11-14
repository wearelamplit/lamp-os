#include "util/loop_watchdog.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <esp_timer.h>
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
  streak = bootStreak(stateValid, streak, stableSeen != 0, taskWdtReset);
  stableSeen = 0;
  return modeFor(streak);
}

void BootGuard::markUptime(uint32_t uptimeMs) {
  if (reachedStable(uptimeMs)) stableSeen = 1;
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
// Written by whichever of the loop task and the backoff timer subscribes.
std::atomic<TaskHandle_t> s_subscribed{nullptr};
Once s_subscribeOnce;
Mode s_mode = Mode::Normal;
Phase s_phase = Phase::Setup;
bool s_wide = false;
esp_timer_handle_t s_backoffTimer = nullptr;

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
      .timeout_ms = timeoutMs(s_phase, s_wide),
      .idle_core_mask = idleCoreMask(kCheckIdle0, kCheckIdle1),
      .trigger_panic = true,
  };
  esp_task_wdt_reconfigure(&config);
}

// True when this call subscribed the loop task.
bool subscribe() {
  if (!s_subscribeOnce.claim()) return false;
  if (esp_task_wdt_add(s_loopTask) != ESP_OK) {
    Serial.println("[loopwd] subscribe FAILED; loop unwatched");
    return false;
  }
  s_subscribed.store(s_loopTask);
  return true;
}

void onBackoffEnd(void*) {
  if (subscribe()) Serial.println("[loopwd] backoff over; loop watched");
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
  if (s_mode == Mode::Normal) {
    subscribe();
    return;
  }
  Serial.printf("[loopwd] %u watchdog resets in a row; loop unwatched for %u s "
                "or until it runs a minute\n",
                (unsigned)s_rtc.guard.streak, (unsigned)(kBackoffMs / 1000));
  esp_timer_create_args_t args = {};
  args.callback = onBackoffEnd;
  args.name = "loopwd";
  if (esp_timer_create(&args, &s_backoffTimer) != ESP_OK ||
      esp_timer_start_once(s_backoffTimer, uint64_t{kBackoffMs} * 1000u) != ESP_OK) {
    Serial.println("[loopwd] backoff timer FAILED; loop watched now");
    subscribe();
  }
}

void beginLoop() {
  s_phase = Phase::Loop;
  apply();
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
  if (!feedsFrom(xTaskGetCurrentTaskHandle(), s_subscribed.load())) return;
  esp_task_wdt_reset();
#ifdef LAMP_DEBUG
  if (s_phase != Phase::Loop) return;
  s_unfed.mark(millis());
  report(s_unfed, s_unfedLog, "unfed span");
#endif
}

void startIteration() {
  const uint32_t nowMs = millis();
  if (rearmDue(s_mode, nowMs)) {
    s_mode = Mode::Normal;
    if (subscribe()) {
      esp_timer_stop(s_backoffTimer);
      Serial.println("[loopwd] loop ran a minute; watched again");
    }
  }
  if (!markIteration(s_rtc.guard, nowMs, xTaskGetCurrentTaskHandle(),
                     s_subscribed.load())) {
    return;
  }
  feed();
#ifdef LAMP_DEBUG
  s_iteration.mark(nowMs);
  report(s_iteration, s_iterationLog, "loop iteration");
#endif
}

#endif

}  // namespace lamp::loop_watchdog

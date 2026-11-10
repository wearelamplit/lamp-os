#include "util/loop_watchdog.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include <Arduino.h>
#include <esp_task_wdt.h>

#ifdef LAMP_DEBUG
#include "util/log_rate_limiter.hpp"
#endif
#endif

namespace lamp::loop_watchdog {

bool FeedGap::feed(uint32_t nowMs) {
  const uint32_t gap = nowMs - lastFeedMs_;
  lastFeedMs_ = nowMs;
  const uint32_t before = longest_.peek();
  return longest_.observe(gap) > before;
}

#if defined(ARDUINO) || defined(ESP_PLATFORM)

namespace {

TaskHandle_t s_loopTask = nullptr;

#ifdef LAMP_DEBUG
FeedGap s_gap;
bool s_gapUnreported = false;
LogRateLimiter s_gapLog(/*windowMs=*/5000, /*linesPerWindow=*/1);
#endif

}  // namespace

void begin() {
  if (s_loopTask != nullptr) return;
  const TaskHandle_t self = xTaskGetCurrentTaskHandle();
  if (esp_task_wdt_add(self) != ESP_OK) {
#ifdef LAMP_DEBUG
    Serial.println("[loopwd] subscribe FAILED; loop unwatched");
#endif
    return;
  }
  s_loopTask = self;
#ifdef LAMP_DEBUG
  s_gap.start(millis());
#endif
}

void feed() {
  if (s_loopTask == nullptr || xTaskGetCurrentTaskHandle() != s_loopTask) return;
  esp_task_wdt_reset();
#ifdef LAMP_DEBUG
  const uint32_t nowMs = millis();
  if (s_gap.feed(nowMs)) s_gapUnreported = true;
  if (s_gapUnreported && s_gapLog.admit(nowMs).print) {
    s_gapUnreported = false;
    Serial.printf("[loopwd] longest unfed span %u ms\n",
                  (unsigned)s_gap.longestMs());
  }
#endif
}

#endif

}  // namespace lamp::loop_watchdog

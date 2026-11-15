#include "core/setup_stage.hpp"

#include <Arduino.h>
#include <esp_attr.h>

#include "util/loop_watchdog.hpp"

namespace lamp::setup_stage {

#ifdef LAMP_DEBUG
namespace {

constexpr uint32_t kHangMagic = 0x4c534847u;

struct BenchHang {
  uint32_t magic;
  uint8_t stage;
  uint8_t boots;
};

RTC_NOINIT_ATTR BenchHang s_hang;
uint32_t s_lastStageMs = 0;

void maybeHang(boot_history::Stage stage) {
  if (s_hang.magic != kHangMagic || s_hang.boots == 0 ||
      s_hang.stage != static_cast<uint8_t>(stage)) {
    return;
  }
  --s_hang.boots;
  Serial.printf("[setup] bench hang at %s, %u more\n", boot_history::stageName(stage),
                (unsigned)s_hang.boots);
  Serial.flush();
  volatile bool spin = true;
  while (spin) {
  }
}

}  // namespace

void armBenchHang(boot_history::Stage stage, uint8_t boots) {
  s_hang.magic = kHangMagic;
  s_hang.stage = static_cast<uint8_t>(stage);
  s_hang.boots = boots;
}
#endif

void reach(boot_history::Stage stage) {
  boot_history::reach(stage);
  loop_watchdog::feed();
#ifdef LAMP_DEBUG
  const uint32_t nowMs = millis();
  Serial.printf("[setup] %s +%u ms\n", boot_history::stageName(stage),
                (unsigned)(nowMs - s_lastStageMs));
  s_lastStageMs = nowMs;
  maybeHang(stage);
#endif
}

}  // namespace lamp::setup_stage

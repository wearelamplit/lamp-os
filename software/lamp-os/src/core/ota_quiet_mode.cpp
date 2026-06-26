#include "ota_quiet_mode.hpp"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
#include "components/network/ble_control.hpp"
#endif

namespace lamp {
namespace ota_quiet_mode {

// Refcount, not bool. A single lamp commonly RECEIVES one OTA while
// simultaneously DISTRIBUTING another to a third lamp during fleet
// gossip. Each state machine calls enterQuiet/exitQuiet independently;
// with a bool, the first session to exit knocks the lamp out of quiet
// mode while the second is still active and the strip flips back to
// the normal behavior pipeline mid-OTA. Tracking by count keeps quiet
// mode latched as long as ANY session is in flight.
static std::atomic<int> s_quietCount{0};

// Tracks whether the 0→1 transition tore down the radio so the 1→0
// transition resumes it. The mesh path (EspNow) tears down; the
// BLE-pushed OTA path doesn't. If two concurrent OTAs are EspNow + BLE,
// the first-in caller's flag wins for the duration — that's correct:
// the radio stays torn down until both have exited, since either OTA
// benefits from the quiet radio.
static std::atomic<bool> s_torndown{false};

void enterQuiet(bool tearDownRadio) {
  const int prev = s_quietCount.fetch_add(1, std::memory_order_acq_rel);
  if (prev != 0) return;  // Already active — second-in is a no-op
                          // beyond bumping the count.

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (tearDownRadio) {
    ::ble_control::pauseRadioForOta();
    ::ble_control::disconnectGattClientsForOta();
    s_torndown.store(true, std::memory_order_release);
  }
#else
  (void)tearDownRadio;
#endif
}

void exitQuiet() {
  const int prev = s_quietCount.fetch_sub(1, std::memory_order_acq_rel);
  if (prev > 1) return;   // Other session still active.
  if (prev <= 0) {
    // Over-exit — clamp back to zero. Shouldn't happen with paired
    // enter/exit but guards against a runaway sub.
    s_quietCount.store(0, std::memory_order_release);
    return;
  }

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  if (s_torndown.exchange(false, std::memory_order_acq_rel)) {
    ::ble_control::resumeRadioAfterOta();
  }
#endif
}

bool isQuiet() {
  return s_quietCount.load(std::memory_order_acquire) > 0;
}

}  // namespace ota_quiet_mode
}  // namespace lamp

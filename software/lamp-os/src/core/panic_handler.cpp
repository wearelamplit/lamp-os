// Custom Arduino-ESP32 panic handler. Arduino-ESP32 already wraps
// esp_panic_handler internally; we register via set_arduino_panic_handler()
// so we get called with a pre-parsed backtrace before the system reboots.
//
// This is critical for our bug: we get `_invalid_pc_placeholder` resets that
// silently bypass the normal panic backtrace. With this hook we always print
// something useful to UART before reset, even when the regular ESP-IDF panic
// pipeline can't.
//
// Output uses ets_printf (interrupt-safe, direct UART) since interrupts are
// disabled at this point.

#include <Arduino.h>
#include <esp32-hal.h>
#include <rom/ets_sys.h>

static void lamp_panic_handler(arduino_panic_info_t* info, void* arg) {
  (void)arg;
  ets_printf("\n\n[LAMP PANIC]\n");
  ets_printf("  reason: %s\n", info->reason ? info->reason : "(null)");
  ets_printf("  core:   %d\n", info->core);
  ets_printf("  pc:     %p\n", info->pc);
  ets_printf("  bt_len: %u%s%s\n",
             info->backtrace_len,
             info->backtrace_corrupt ? " (CORRUPT)" : "",
             info->backtrace_continues ? " (truncated)" : "");
  for (unsigned int i = 0; i < info->backtrace_len; i++) {
    ets_printf("  [%u] 0x%08x\n", i, info->backtrace[i]);
  }
  ets_printf("[/LAMP PANIC]\n\n");
}

void lamp_register_panic_handler() {
  set_arduino_panic_handler(lamp_panic_handler, nullptr);
}

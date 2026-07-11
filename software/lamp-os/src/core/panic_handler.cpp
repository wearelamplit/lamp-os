// Custom Arduino-ESP32 panic hook, registered via set_arduino_panic_handler().
// Dumps a tagged [LAMP PANIC] block (reason/core/pc/backtrace) over UART with
// ets_printf (interrupt-safe direct UART; interrupts are off at panic time).
// pc may be _invalid_pc_placeholder when ESP-IDF cannot determine the fault address.
// Coredump-to-flash is disabled on this build (SPIFFS occupies that partition;
// see platformio.ini), so this hook is the only crash-diagnostic path before reset.

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

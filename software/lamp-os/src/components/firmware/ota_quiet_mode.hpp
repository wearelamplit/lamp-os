#pragma once

#include <atomic>

namespace lamp {
namespace ota_quiet_mode {

// Single global flag that suspends the lamp's behavior + framebuffer stack
// + live-control BLE writes for the duration of an OTA. Entered by the
// firmware receiver on State::Streaming entry and by the firmware
// distributor on first OFFER; exited on abort, failure tombstone, or
// success (just before reboot).
//
// `tearDownRadio` controls whether we also stop adv/scan/webapp + kick any
// connected GATT client. The mesh OTA path passes true (the phone isn't
// the transport — kicking it just stops it from competing with chunk RX
// for radio time). The BLE-pushed OTA path passes false (the phone IS the
// chunk transport — kicking it kills the session).
//
// When quiet is active:
//   - Compositor::tick() short-circuits behaviors / expression draw but
//     keeps frameBuffer->flush() alive.
//   - lamp.cpp's crowdDim micro-fade pending-apply branch defers so it
//     doesn't snap-apply 5 minutes of accumulated brightness on resume.
//   - ble_control silently drops writes to live-control characteristics
//     (color, shade, brightness, expression, page, settings, wifi-op,
//     wisp-op, remote-op). The OTA characteristics (CHAR_FW_CONTROL,
//     CHAR_FW_CHUNK) still accept writes — they need to so the BLE-pushed
//     OTA path keeps working.
//
// All state lives in this header / .cpp pair — no dependency on the
// firmware module beyond the bool flag at entry. Keeps the dependency
// direction clean (firmware → quiet_mode, not the other way).

void enterQuiet(bool tearDownRadio);
void exitQuiet();

// Cross-core safe — acquire-load. Compositor + write handlers both
// query this hot path.
bool isQuiet();

}  // namespace ota_quiet_mode
}  // namespace lamp

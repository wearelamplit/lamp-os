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
// `tearDownRadio` controls whether adv/scan/webapp also stop and any
// connected GATT client is kicked. The mesh OTA path passes true (the phone
// isn't the transport, so kicking it just stops it from competing with chunk
// RX for radio time). The BLE-pushed OTA path passes false (the phone IS the
// chunk transport, so kicking it kills the session).
//
// When quiet is active:
//   - Compositor::tick() short-circuits behaviors / expression draw but
//     keeps frameBuffer->flush() alive.
//   - lamp.cpp's crowdDim micro-fade pending-apply branch defers so it
//     doesn't snap-apply 5 minutes of accumulated brightness on resume.
//   - ble_control silently drops writes to live-control characteristics
//     (color, shade, brightness, expression, page, settings, wifi-op,
//     wisp-op, remote-op). The OTA characteristics (CHAR_FW_CONTROL,
//     CHAR_FW_CHUNK) still accept writes, which the BLE-pushed OTA path
//     needs.
//
// All state lives in this header / .cpp pair, with no dependency on the
// firmware module beyond the bool flag at entry. Keeps the dependency
// direction clean (firmware → quiet_mode, not the other way).

// `visible` gates only the on-strip OTA indicator (compositor.cpp); it is
// independent of tearDownRadio, which gates the coex radio pause. Firmware
// OTA passes true (dim + progress band + pulse); FS OTA passes false (base
// settles and holds, radio pause unaffected).
void enterQuiet(bool tearDownRadio, bool visible);
void exitQuiet();

// Cross-core safe (acquire-load). Compositor + write handlers both
// query this hot path.
bool isQuiet();

// The active session's visibility, latched at the 0->1 enterQuiet that
// started it. Meaningful only while isQuiet() is true.
bool visibleOtaActive();

}  // namespace ota_quiet_mode
}  // namespace lamp

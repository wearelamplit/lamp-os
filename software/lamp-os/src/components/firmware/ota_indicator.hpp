#pragma once

#include <cstdint>

#include "util/color.hpp"

// OTA visual indicator. Painted onto the SHADE and base surfaces during OTA
// quiet mode (compositor.cpp): a progress bar overlays the shade, and the
// base settles to its default color and holds instead of freezing on the
// pre-quiet frame.
//
// Shade render: all pixels = localBase at 20% dim; the leading N pixels = the
// peer's base color at full brightness, where N = (done/total) * pixelCount
// in 8.8 fixed-point (anti-aliased fractional edge pixel).
//
// Base render: on quiet entry (caller-signaled via quietEntered), eases from
// the frozen pre-quiet frame to baseFb->defaultColors, then holds static.
//
// Peer color latches on the first LampRoster hit; the shade progress bar
// falls back to localBase until resolved.
//
// Sender vs receiver: same shape; `paint()` reads the in-flight side from
// the global firmwareReceiver / firmwareDistributor. Shade is a no-op if
// neither is in progress (caller already gated on ota_quiet_mode::isQuiet());
// base still settles in that case.

namespace lamp {
class FrameBuffer;

namespace ota_indicator {

// Paint the indicator into fb->buffer, and settle baseFb->buffer, for the
// current OTA state.
// fb: shade destination, writes fb->buffer[0..pixelCount).
// localBase: this lamp's base color (caller passes per-FB defaultColors[0]).
// nowMs: millis() snapshot; also drives the base settle/pulse timing.
// baseFb: base destination. Null-safe (skipped if null or empty).
// quietEntered: true on the tick ota_quiet_mode::isQuiet() first goes
// false->true; (re)initializes the base settle from baseFb's current frame.
void paint(FrameBuffer* fb, const Color& localBase, uint32_t nowMs,
           FrameBuffer* baseFb, bool quietEntered);

// Silent sibling of paint(): no dim, no progress band. Settles
// shadeFb and baseFb to their own defaultColors and holds, so the lamp reads
// calm/at-rest instead of freezing on the pre-quiet frame. Used for FS OTA
// (ota_quiet_mode::visibleOtaActive() == false).
void paintSilent(FrameBuffer* shadeFb, FrameBuffer* baseFb, uint32_t nowMs,
                 bool quietEntered);

}  // namespace ota_indicator
}  // namespace lamp

#pragma once

#include <cstdint>

#include "util/color.hpp"

// OTA visual indicator. Painted into the framebuffer DURING OTA quiet mode
// (see ota_quiet_mode.hpp / compositor.cpp) so the strip shows a meaningful
// "I'm sending/receiving firmware right now" signal instead of freezing on
// whatever frame happened to be drawing when quiet entered.
//
// Pattern:
//   - All pixels = the lamp's own `localBase` color scaled to 20% brightness.
//   - The first N pixels = the OTHER lamp's base color (looked up via
//     NearbyLamps::findByMac on the active OTA peer), pulsing 20%↔100% on
//     a 1.5 s sine. N = chunks-done * pixelCount / totalChunks.
//   - Peer base color falls back to white (255,255,255,0) when the peer
//     isn't in NearbyLamps yet (we shouldn't be in OTA without HELLO
//     contact, but defensively).
//
// Sender vs receiver: same shape; `paint()` reads the in-flight side from
// the global firmwareReceiver / firmwareDistributor. If neither is in
// progress the call is a no-op (defensive — the caller already gated on
// ota_quiet_mode::isQuiet()).

namespace lamp {
class FrameBuffer;

namespace ota_indicator {

// Paint the indicator into fb->buffer for the current OTA state.
//   fb        — destination framebuffer; writes fb->buffer[0..pixelCount).
//   localBase — this lamp's base color (caller passes per-FB defaultColors[0]).
//   nowMs     — millis() snapshot used to drive the pulse phase.
void paint(FrameBuffer* fb, const Color& localBase, uint32_t nowMs);

}  // namespace ota_indicator
}  // namespace lamp

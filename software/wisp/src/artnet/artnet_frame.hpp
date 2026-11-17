// artnet_frame is a pure, host-portable ArtNet DMX frame builder for the wisp
// backwards-compat ArtNet bridge. Produces the exact 530-byte universe-1 frame
// pre-mesh lamp firmware decodes; those lamps reject any other packet length.
// Channels per fixture: shade RGBW, base RGBW, mode, parameter.
//
// No Arduino, no FreeRTOS. Caller owns the buffer; the builder only fills it in.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "paint/current_palette.hpp"

namespace wisp {

// Total bytes the lamp listener requires (it rejects anything else).
constexpr size_t kArtnetFrameSize = 530;

// Number of 10-channel fixtures emitted into universe 1. Matches the lamp
// firmware's lampNumber range [0, 7].
constexpr size_t kArtnetNumFixtures = 8;

// Fill `out` with one ArtNet DMX universe-1 frame for the palette's colours.
// Each fixture's two surfaces (shade, base) get a stable TupleSampler-derived
// pair sampled at a synthetic per-fixture MAC. Returns kArtnetFrameSize on
// success, 0 if `outLen < kArtnetFrameSize`.
//
// `seq` is the rolling sequence counter the caller maintains (0..255). Lamps
// don't strictly require it but ArtNet senders are expected to.
size_t buildArtnetDmxFrame(const std::vector<RGBW>& colors, uint8_t seq,
                           uint8_t* out, size_t outLen, PaletteStyle style);

}  // namespace wisp

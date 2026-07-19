#pragma once

#include <cstdint>

#include "led_types.hpp"

namespace lampos {
namespace led {

// Per-channel WS281x full-duty draw at 5 V, bench-calibrated. Undersized, the
// governor under-clamps and the supply can sag.
constexpr float kMaPerChannelFullDuty = 10.0f;

// Quiescent draw of a dark pixel's controller.
constexpr float kIdleMaPerPixel = 0.7f;

// Channels a strip actually transmits; a 3-channel strip never emits W.
inline constexpr uint8_t activeChannelCount(ByteOrder b) {
  return b == ByteOrder::GRBW ? 4 : 3;
}

}  // namespace led
}  // namespace lampos

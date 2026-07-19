#pragma once

#include <cstdint>

#include "color.hpp"

/**
 * Interested in bulding new luts for different types of easing?
 * @see https://gist.github.com/chaffneue/808f38f12eb4f2104b97dcd74af9e812
 */
namespace lamp {
/**
 * ease one color byte to another using a quadratic curve
 * @param  [in] start - the start pixel value
 * @param [in] end - the end pixel value
 * @param [in] duration - the duration of the change
 * @param [in] currentStep - the step in the duration of the change
 */
uint8_t ease(uint8_t start, uint8_t end, uint32_t duration, uint32_t currentStep);

/**
 * ease one color byte to another using a linear curve
 * @param  [in] start - the start pixel value
 * @param [in] end - the end pixel value
 * @param [in] duration - the duration of the change
 * @param [in] currentStep - the step in the duration of the change
 */
uint8_t easeLinear(uint8_t start, uint8_t end, uint32_t duration, uint32_t currentStep);

/**
 * fade all color bytes to another using a quadratic curve
 * @param  [in] start - the start pixel value
 * @param [in] end - the end pixel value
 * @param [in] steps - the duration of the change
 * @param [in] currentStep - the step in the duration of the change
 */
Color fade(Color start, Color end, uint32_t steps, uint32_t currentStep);

/**
 * fade all color byte to another using a linear curve
 * @param  [in] start - the start pixel value
 * @param [in] end - the end pixel value
 * @param [in] steps - the duration of the change
 * @param [in] currentStep - the step in the duration of the change
 */
Color fadeLinear(Color start, Color end, uint32_t steps, uint32_t currentStep);

// Inner-loop primitives used by per-pixel expression draw() math.
// Marked `inline` so the compiler inlines them across translation units.
//
// `computeLinearFactor` mirrors `easeLinear()`'s factor calc bit-for-bit
// (linear[i] == i * 511 by construction → factor computed analytically,
// no LUT lookup). `mixByteLinear` mirrors `easeLinear()`'s body with the
// same integer types, divisor, and start==end short-circuit.
// `mixColorLinear` is the obvious per-channel wrap.
inline uint32_t computeLinearFactor(uint32_t currentStep, uint32_t duration) {
  return static_cast<uint32_t>(
             static_cast<uint16_t>((currentStep * 511u / duration * 511u) / 511u)) *
         511u;
}

inline uint8_t mixByteLinear(uint8_t start, uint8_t end, uint32_t factor) {
  if (start == end) return end;
  return static_cast<uint8_t>(
      ((static_cast<uint32_t>(end) - static_cast<uint32_t>(start)) * factor) /
          262144u +
      start);
}

inline Color mixColorLinear(const Color& start, const Color& end, uint32_t factor) {
  return Color(mixByteLinear(start.r, end.r, factor),
               mixByteLinear(start.g, end.g, factor),
               mixByteLinear(start.b, end.b, factor),
               mixByteLinear(start.w, end.w, factor));
}
}  // namespace lamp
